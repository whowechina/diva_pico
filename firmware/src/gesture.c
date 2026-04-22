/*
 * Extract gesture information from raw electrode touch bitmask.
 *
 * I'm using a region clustering + tracking approach.
 * Regions are contiguous (or gap-bridged) sets of active electrodes in one frame.
 * Clusters are living, persistent objects that are driven by regions.
 * Gestures are recognized from cluster movement and reported as joystick axes.
 *
 * WHowe <github.com/whowechina>
 */

#include "gesture.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#define ELEC_COUNT 32

#define MAX_REGIONS 12 // Per-frame extracted regions.
#define MAX_CLUSTERS 12 // Living clusters tracked across frames.

#define REGION_GAP_SIZE 2 // Maximum gap size (in electrodes) allowed inside one region.
#define MATCH_MAX_DIST_Q4 80 // Max cluster-to-region match distance in Q4 units.

#define DEBOUNCE_SET_FRAMES 2
#define DEBOUNCE_RESET_FRAMES 10

#define LATCH_FRAMES 100

#define SEQ_MIN_STEPS 2 // Required consecutive +/-1 index moves to confirm direction.
#define SEQ_TIMEOUT_FRAMES 100 // Max frames allowed between sequential steps.
#define DEATH_HOLD_FRAMES 50 // Cluster death delay.

#define AXIS_CENTER 0x80
#define AXIS_OFFSET 100 // Signed offset from center when steering left/right.

static uint16_t total_zone_num = 32;

// A region is a contiguous (or gap-bridged) set of active electrodes in one frame.
// Q4 units are used for more precise center position and distance calculation.
typedef struct {
    int start;
    int end;
    int width;
    int center_q4;
} region_t;

// A cluster is a living, persistent object that follows a region across frames.
typedef struct {
    bool alive;
    int id;
    int miss;
    int width;
    int slot;
    int center_q4;
    int prev_center_q4;
    int last_idx;
    int seq_age;
    int seq_len;
    int seq_dir;
    int dir;
} cluster_t;

static cluster_t clusters[MAX_CLUSTERS];
static int next_id;

static uint32_t db_state;
static int db_cnt[ELEC_COUNT];

static bool debug_cluster;

static int hold_left_dir;
static int hold_right_dir;
static int hold_left_frames;
static int hold_right_frames;

static uint32_t debounce_mask(uint32_t raw)
{
    for (int i = 0; i < ELEC_COUNT; i++) {
        uint32_t bit = (raw >> i) & 1u;
        bool state = ((db_state >> i) & 1u) != 0u;
        int threshold = state ? DEBOUNCE_RESET_FRAMES : DEBOUNCE_SET_FRAMES;
        if (bit != state) {
            if (db_cnt[i] < threshold) {
                db_cnt[i]++;
            }
            if (db_cnt[i] >= threshold) {
                db_state ^= (1u << i);
                db_cnt[i] = 0;
            }
        } else {
            db_cnt[i] = 0;
        }
    }
    return db_state;
}

static int extract_regions(uint32_t mask, region_t *regions, int max_regions)
{
    int count = 0;
    int i = 0;

    while (i < ELEC_COUNT) {
        if (((mask >> i) & 1u) == 0u) {
            i++;
            continue;
        }

        int start = i;
        int end = i;
        i++;

        while (i < ELEC_COUNT) {
            if (((mask >> i) & 1u) != 0u) {
                end = i;
                i++;
            } else {
                int j = i;
                while ((j < ELEC_COUNT) && (((mask >> j) & 1u) == 0u)) {
                    j++;
                }
                int gap = j - i;
                if ((gap <= REGION_GAP_SIZE) && (j < ELEC_COUNT)) {
                    i = j;
                } else {
                    break;
                }
            }
        }

        if (count < max_regions) {
            regions[count].start = start;
            regions[count].end = end;
            regions[count].width = end - start + 1;
            regions[count].center_q4 = (start + end) * 8;
            count++;
        }
    }

    return count;
}

static int alloc_cluster(void)
{
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (!clusters[i].alive) {
            return i;
        }
    }
    return -1;
}

static int q4_to_idx(int q4)
{
    if (q4 <= 0) {
        return 0;
    }
    if (q4 >= 31 * 16) {
        return 31;
    }
    return (q4 + 8) / 16;
}

static int sign(int x)
{
    return (x > 0) - (x < 0);
}

static void update_seq(cluster_t *c)
{
    int idx = q4_to_idx(c->center_q4);
    if (idx == c->last_idx) {
        if (c->seq_age < 0x7FFFFFFF) {
            c->seq_age++;
        }
        if (c->seq_age > SEQ_TIMEOUT_FRAMES) {
            c->dir = 0;
        }
        return;
    }

    int diff = idx - c->last_idx;
    int ad = abs(diff);
    int sd = sign(diff);

    if (ad == 1 && c->seq_age <= SEQ_TIMEOUT_FRAMES) {
        if (sd == c->seq_dir) {
            if (c->seq_len < 0x7FFFFFFF) {
                c->seq_len++;
            }
        } else {
            c->seq_dir = sd;
            c->seq_len = 1;
        }
    } else {
        c->seq_dir = 0;
        c->seq_len = 0;
    }

    c->last_idx = idx;
    c->seq_age = 0;

    if (c->seq_len >= SEQ_MIN_STEPS) {
        c->dir = c->seq_dir;
    }
}

static void assign_slot_if_needed(cluster_t *c)
{
    if (c->slot != -1 || c->dir == 0) {
        return;
    }

    bool left_used = false;
    bool right_used = false;
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (!clusters[i].alive) {
            continue;
        }
        if (clusters[i].slot == 0) {
            left_used = true;
        } else if (clusters[i].slot == 1) {
            right_used = true;
        }
    }

    int mid_q4 = (total_zone_num * 16) / 2;

    if (c->center_q4 < mid_q4) {
        if (!left_used) {
            c->slot = 0;
        } else if (!right_used) {
            c->slot = 1;
        }
    } else {
        if (!right_used) {
            c->slot = 1;
        } else if (!left_used) {
            c->slot = 0;
        }
    }
}

static void match_and_update(const region_t *regions, int region_count)
{
    bool region_used[MAX_REGIONS] = {0};
    bool cluster_used[MAX_CLUSTERS] = {0};

    while (true) {
        int best = 0x7FFFFFFF;
        int best_cluster = -1;
        int best_region = -1;

        for (int i = 0; i < MAX_CLUSTERS; i++) {
            if (!clusters[i].alive || cluster_used[i]) {
                continue;
            }
            for (int j = 0; j < region_count; j++) {
                if (region_used[j]) {
                    continue;
                }
                int d = abs(clusters[i].center_q4 - regions[j].center_q4);
                if (d < best) {
                    best = d;
                    best_cluster = i;
                    best_region = j;
                }
            }
        }

        if (best_cluster < 0 || best_region < 0 || best > MATCH_MAX_DIST_Q4) {
            break;
        }

        cluster_t *c = &clusters[best_cluster];
        const region_t *reg = &regions[best_region];

        c->miss = 0;
        c->prev_center_q4 = c->center_q4;
        c->center_q4 = reg->center_q4;
        c->width = reg->width;
        update_seq(c);
        assign_slot_if_needed(c);

        cluster_used[best_cluster] = true;
        region_used[best_region] = true;
    }

    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (!clusters[i].alive || cluster_used[i]) {
            continue;
        }
        clusters[i].miss++;
        if (clusters[i].miss > DEATH_HOLD_FRAMES) {
            memset(&clusters[i], 0, sizeof(clusters[i]));
        }
    }

    for (int j = 0; j < region_count; j++) {
        if (region_used[j]) {
            continue;
        }
        int ci = alloc_cluster();
        if (ci < 0) {
            break;
        }
        cluster_t *c = &clusters[ci];
        memset(c, 0, sizeof(*c));
        c->alive = true;
        c->id = next_id++;
        c->slot = -1;
        c->center_q4 = regions[j].center_q4;
        c->prev_center_q4 = regions[j].center_q4;
        c->width = regions[j].width;
        c->last_idx = q4_to_idx(regions[j].center_q4);
    }
}

static void choose_dirs(int *left_dir, int *right_dir)
{
    int l = 0;
    int r = 0;
    int lscore = 0;
    int rscore = 0;

    int moving_count = 0;
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (clusters[i].alive && clusters[i].dir != 0) {
            moving_count++;
        }
    }

    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (!clusters[i].alive || clusters[i].dir == 0) {
            continue;
        }
        if (clusters[i].slot == 0 && clusters[i].seq_len >= lscore) {
            lscore = clusters[i].seq_len;
            l = clusters[i].dir;
        } else if (clusters[i].slot == 1 && clusters[i].seq_len >= rscore) {
            rscore = clusters[i].seq_len;
            r = clusters[i].dir;
        }
    }

    if (moving_count >= 3) {
        bool has_neg = false;
        bool has_pos = false;
        for (int i = 0; i < MAX_CLUSTERS; i++) {
            if (!clusters[i].alive || clusters[i].dir == 0) {
                continue;
            }
            if (clusters[i].dir < 0) {
                has_neg = true;
            } else if (clusters[i].dir > 0) {
                has_pos = true;
            }
        }
        if (has_neg && has_pos) {
            if (l == 0) {
                l = -1;
            }
            if (r == 0) {
                r = 1;
            }
        }
    }

    *left_dir = l;
    *right_dir = r;
}

static void hold_axis(int in_l, int in_r, int *out_l, int *out_r)
{
    if (in_l != 0) {
        hold_left_dir = in_l;
        hold_left_frames = LATCH_FRAMES;
    } else if (hold_left_frames > 0) {
        hold_left_frames--;
    } else {
        hold_left_dir = 0;
    }

    if (in_r != 0) {
        hold_right_dir = in_r;
        hold_right_frames = LATCH_FRAMES;
    } else if (hold_right_frames > 0) {
        hold_right_frames--;
    } else {
        hold_right_dir = 0;
    }

    *out_l = hold_left_dir;
    *out_r = hold_right_dir;
}

static void debug_print(void)
{
    if (!debug_cluster) {
        return;
    }

    bool printed = false;
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (!clusters[i].alive) {
            continue;
        }
        const char *tag = "*";
        if (clusters[i].dir > 0) {
            tag = ">>";
        } else if (clusters[i].dir < 0) {
            tag = "<<";
        }
        if (printed) {
            printf("  ");
        }
        printed = true;
        printf("{%d %d %d %s %u s%u}",
               clusters[i].id,
               q4_to_idx(clusters[i].center_q4),
               clusters[i].width,
               tag,
               (unsigned)(DEATH_HOLD_FRAMES - ((clusters[i].miss > DEATH_HOLD_FRAMES) ? DEATH_HOLD_FRAMES : clusters[i].miss)),
               (unsigned)((clusters[i].slot < 0) ? 9 : clusters[i].slot));
    }
    if (printed) {
        printf("\n");
    }
}

void gesture_init(uint16_t zone_num)
{
    total_zone_num = zone_num;

    memset(clusters, 0, sizeof(clusters));
    next_id = 1;

    db_state = 0;
    memset(db_cnt, 0, sizeof(db_cnt));

    hold_left_dir = 0;
    hold_right_dir = 0;
    hold_left_frames = 0;
    hold_right_frames = 0;
}

void gesture_set_debug_cluster(bool enable)
{
    debug_cluster = enable;
}

static int axis_from_dir(int dir)
{
    return AXIS_CENTER + dir * AXIS_OFFSET;
}

void gesture_process(uint32_t mask, uint8_t *axis_a, uint8_t *axis_b)
{
    uint32_t filtered_mask = debounce_mask(mask);

    region_t regions[MAX_REGIONS];
    int region_num = extract_regions(filtered_mask, regions, count_of(regions));

    match_and_update(regions, region_num);

    int l = 0;
    int r = 0;
    choose_dirs(&l, &r);
    hold_axis(l, r, &l, &r);

    *axis_a = axis_from_dir(l);
    *axis_b = axis_from_dir(r);

    debug_print();
}
