#include "slide.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SLIDE_MIN_CLUSTER_WIDTH 1
#define SLIDE_MAX_CLUSTERS 8
#define SLIDE_CLUSTER_GAP 4
#define SLIDE_TRACK_COUNT 2
#define SLIDE_TRACK_HOLD_FRAMES 40
#define SLIDE_TRIGGER_RANGE_Q4 32
#define SLIDE_STOP_REBASE_FRAMES 192
#define SLIDE_TOUCH_HOLD_FRAMES 64

#define SLIDE_DEBOUNCE_SET 10
#define SLIDE_DEBOUNCE_RESET 50

#define SLIDE_NEUTRAL_AXIS 0x80
#define SLIDE_AXIS_DELTA 0x50

typedef struct {
    uint8_t start;
    uint8_t end;
    uint8_t width;
    int16_t center_q4;
} cluster_t;

typedef struct {
    bool active;
    uint8_t miss;
    uint8_t age;
    uint8_t width;
    uint8_t still_frames;
    int8_t step_dir;
    int16_t anchor_q4;
    int16_t pos_q4;
    int16_t last_pos_q4;
    int16_t min_q4;
    int16_t max_q4;
} track_t;

typedef struct {
    uint8_t count;
    uint8_t pos[SLIDE_MAX_CLUSTERS];
    uint8_t len[SLIDE_MAX_CLUSTERS];
    int8_t dir[SLIDE_MAX_CLUSTERS];
} debug_snapshot_t;

static track_t tracks[SLIDE_TRACK_COUNT];

static uint32_t debounce_state;
static uint8_t  debounce_cnt[32];

static slide_gesture_t stable_gesture;
static uint16_t hold_frames;
static uint16_t latch_frames = 100;

static bool debug_cluster;
static debug_snapshot_t debug_last;

static uint8_t axis_from_dir(int8_t dir)
{
    if (dir < 0) {
        return (uint8_t)(SLIDE_NEUTRAL_AXIS - SLIDE_AXIS_DELTA);
    }
    if (dir > 0) {
        return (uint8_t)(SLIDE_NEUTRAL_AXIS + SLIDE_AXIS_DELTA);
    }
    return SLIDE_NEUTRAL_AXIS;
}

static uint32_t debounce_mask(uint32_t raw)
{
    for (uint8_t i = 0; i < 32; i++) {
        uint32_t bit = (raw >> i) & 1u;
        bool state = (debounce_state >> i) & 1u;
        if (!state) {
            if (bit) {
                if (debounce_cnt[i] < SLIDE_DEBOUNCE_SET) {
                    debounce_cnt[i]++;
                }
                if (debounce_cnt[i] >= SLIDE_DEBOUNCE_SET) {
                    debounce_state |= (1u << i);
                }
            } else {
                debounce_cnt[i] = 0;
            }
        } else {
            if (!bit) {
                if (debounce_cnt[i] < SLIDE_DEBOUNCE_RESET) {
                    debounce_cnt[i]++;
                }
                if (debounce_cnt[i] >= SLIDE_DEBOUNCE_RESET) {
                    debounce_state &= ~(1u << i);
                }
            } else {
                debounce_cnt[i] = 0;
            }
        }
    }
    return debounce_state;
}

static int16_t abs16(int16_t x)
{
    return (x < 0) ? (int16_t)(-x) : x;
}

static int8_t dir_from_displacement(int16_t delta_q4)
{
    if (delta_q4 >= SLIDE_TRIGGER_RANGE_Q4) {
        return 1;
    }
    if (delta_q4 <= -SLIDE_TRIGGER_RANGE_Q4) {
        return -1;
    }
    return 0;
}

static int8_t track_dir(const track_t *t)
{
    if (!t->active) {
        return 0;
    }
    return dir_from_displacement((int16_t)(t->pos_q4 - t->anchor_q4));
}

static uint16_t track_ttl(const track_t *t)
{
    if (!t->active) {
        return 0;
    }
    return (t->miss >= SLIDE_TRACK_HOLD_FRAMES) ? 0u : (uint16_t)(SLIDE_TRACK_HOLD_FRAMES - t->miss);
}

static uint8_t q4_to_idx(int16_t q4)
{
    if (q4 <= 0) {
        return 0;
    }
    if (q4 >= (31 * 16)) {
        return 31;
    }
    return (uint8_t)((q4 + 8) / 16);
}

static bool track_triggered(const track_t *t)
{
    if (!t->active) {
        return false;
    }
    return (t->max_q4 - t->min_q4) >= SLIDE_TRIGGER_RANGE_Q4;
}

static uint8_t extract_clusters(uint32_t mask, cluster_t *clusters, uint8_t max_clusters)
{
    uint8_t count = 0;
    uint8_t i = 0;

    while (i < 32) {
        if (((mask >> i) & 1u) == 0u) {
            i++;
            continue;
        }

        uint8_t start = i;
        uint8_t end = i;
        i++;

        /* extend cluster, bridging gaps <= SLIDE_CLUSTER_GAP */
        while (i < 32) {
            if (((mask >> i) & 1u) != 0u) {
                end = i;
                i++;
            } else {
                /* measure gap length */
                uint8_t j = i;
                while ((j < 32) && (((mask >> j) & 1u) == 0u)) {
                    j++;
                }
                uint8_t gap = (uint8_t)(j - i);
                if ((gap <= SLIDE_CLUSTER_GAP) && (j < 32)) {
                    /* bridge the gap */
                    i = j;
                } else {
                    break;
                }
            }
        }

        uint8_t width = (uint8_t)(end - start + 1);

        if (width < SLIDE_MIN_CLUSTER_WIDTH) {
            continue;
        }

        if (count < max_clusters) {
            clusters[count].start = start;
            clusters[count].end = end;
            clusters[count].width = width;
            clusters[count].center_q4 = (int16_t)((start + end) * 8);
            count++;
        }
    }

    return count;
}

static void sort_clusters(cluster_t *clusters, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
            if (clusters[j].center_q4 < clusters[i].center_q4) {
                cluster_t t = clusters[i];
                clusters[i] = clusters[j];
                clusters[j] = t;
            }
        }
    }
}

static void update_track(track_t *t, bool matched, const cluster_t *c)
{
    if (matched) {
        int16_t pos_q4 = c->center_q4;

        if (!t->active) {
            t->active = true;
            t->miss = 0;
            t->age = 0;
            t->width = c->width;
            t->still_frames = 0;
            t->step_dir = 0;
            t->anchor_q4 = pos_q4;
            t->pos_q4 = pos_q4;
            t->last_pos_q4 = pos_q4;
            t->min_q4 = pos_q4;
            t->max_q4 = pos_q4;
            return;
        }

        int16_t delta_q4 = (int16_t)(pos_q4 - t->pos_q4);
        int8_t step_dir = (delta_q4 > 0) ? 1 : ((delta_q4 < 0) ? -1 : 0);

        t->miss = 0;
        t->age++;
        t->last_pos_q4 = t->pos_q4;
        t->pos_q4 = pos_q4;
        t->width = c->width;

        if (step_dir != 0) {
            if ((t->step_dir != 0) && (step_dir != t->step_dir)) {
                t->anchor_q4 = t->last_pos_q4;
            }
            t->step_dir = step_dir;
            t->still_frames = 0;
        } else {
            if (t->still_frames < 0xFF) {
                t->still_frames++;
            }
            if (t->still_frames >= SLIDE_STOP_REBASE_FRAMES) {
                t->anchor_q4 = t->pos_q4;
                t->step_dir = 0;
            }
        }

        if (pos_q4 < t->min_q4) {
            t->min_q4 = pos_q4;
        }
        if (pos_q4 > t->max_q4) {
            t->max_q4 = pos_q4;
        }
        return;
    }

    if (!t->active) {
        return;
    }

    if (t->miss < 0xFF) {
        t->miss++;
    }

    if (t->miss > SLIDE_TRACK_HOLD_FRAMES) {
        memset(t, 0, sizeof(*t));
    } else {
        t->last_pos_q4 = t->pos_q4;
        if (t->still_frames < 0xFF) {
            t->still_frames++;
        }
    }
}

static slide_gesture_t classify_gesture(void)
{
    track_t *a = &tracks[0];
    track_t *b = &tracks[1];

    if (a->active && b->active && (a->pos_q4 > b->pos_q4)) {
        track_t tmp = *a;
        *a = *b;
        *b = tmp;
    }

    bool a_ready = a->active && (a->age > 0) && track_triggered(a);
    bool b_ready = b->active && (b->age > 0) && track_triggered(b);

    if (a_ready && b_ready) {
        int8_t da = track_dir(a);
        int8_t db = track_dir(b);

        if (da == db && da < 0) {
            return SLIDE_GESTURE_DUAL_LEFT;
        }
        if (da == db && da > 0) {
            return SLIDE_GESTURE_DUAL_RIGHT;
        }
        if (da > 0 && db < 0) {
            return SLIDE_GESTURE_DUAL_CONVERGE;
        }
        if (da < 0 && db > 0) {
            return SLIDE_GESTURE_DUAL_DIVERGE;
        }

        return SLIDE_GESTURE_NONE;
    }

    if (a_ready || b_ready) {
        track_t *t = a_ready ? a : b;
        int8_t d = track_dir(t);
        if (d < 0) {
            return SLIDE_GESTURE_SINGLE_LEFT;
        }
        if (d > 0) {
            return SLIDE_GESTURE_SINGLE_RIGHT;
        }
    }

    return SLIDE_GESTURE_NONE;
}

static void apply_latch(slide_gesture_t instant, bool touching)
{
    if (instant != SLIDE_GESTURE_NONE) {
        stable_gesture = instant;
        hold_frames = latch_frames;
        return;
    }

    if (touching && hold_frames > SLIDE_TOUCH_HOLD_FRAMES) {
        hold_frames = SLIDE_TOUCH_HOLD_FRAMES;
    }

    if (hold_frames > 0) {
        hold_frames--;
    } else {
        stable_gesture = SLIDE_GESTURE_NONE;
    }
}

static bool snapshot_equals(const debug_snapshot_t *a, const debug_snapshot_t *b)
{
    if (a->count != b->count) {
        return false;
    }
    for (uint8_t i = 0; i < a->count; i++) {
        if (a->pos[i] != b->pos[i] || a->len[i] != b->len[i] || a->dir[i] != b->dir[i]) {
            return false;
        }
    }
    return true;
}

static bool snapshot_contains(const debug_snapshot_t *s, uint8_t pos, uint8_t len)
{
    for (uint8_t i = 0; i < s->count; i++) {
        if (s->pos[i] == pos && s->len[i] == len) {
            return true;
        }
    }
    return false;
}

static int8_t nearest_track_info(int16_t center_q4, uint16_t *ttl, bool *matched)
{
    int16_t best = 0x7FFF;
    int8_t best_dir = 0;
    uint16_t best_ttl = 0;
    bool best_match = false;

    for (uint8_t i = 0; i < SLIDE_TRACK_COUNT; i++) {
        if (!tracks[i].active) {
            continue;
        }
        int16_t d = abs16((int16_t)(tracks[i].pos_q4 - center_q4));
        if (d < best) {
            best = d;
            best_dir = track_dir(&tracks[i]);
            best_ttl = track_ttl(&tracks[i]);
            best_match = (d <= 24);
        }
    }

    if (ttl) {
        *ttl = best_ttl;
    }
    if (matched) {
        *matched = best_match;
    }
    return best_dir;
}

static void debug_print_clusters(const cluster_t *clusters, uint8_t count)
{
    if (!debug_cluster) {
        memset(&debug_last, 0, sizeof(debug_last));
        return;
    }

    debug_snapshot_t now = {0};
    uint16_t ttl[SLIDE_MAX_CLUSTERS] = {0};

    for (uint8_t i = 0; (i < count) && (i < SLIDE_MAX_CLUSTERS); i++) {
        bool matched = false;
        int8_t dir = nearest_track_info(clusters[i].center_q4, &ttl[i], &matched);
        now.pos[now.count] = q4_to_idx(clusters[i].center_q4);
        now.len[now.count] = clusters[i].width;
        now.dir[now.count] = matched ? dir : 0;
        now.count++;
    }

    bool changed = !snapshot_equals(&now, &debug_last);
    bool has_dead = false;
    for (uint8_t i = 0; i < debug_last.count; i++) {
        if (!snapshot_contains(&now, debug_last.pos[i], debug_last.len[i])) {
            has_dead = true;
            break;
        }
    }

    if (!changed && !has_dead) {
        return;
    }

    bool printed = false;
    for (uint8_t i = 0; i < now.count; i++) {
        const char *tag = "*";
        if (now.dir[i] > 0) {
            tag = ">>";
        } else if (now.dir[i] < 0) {
            tag = "<<";
        }
        if (printed) {
            printf("  ");
        }
        printed = true;
        printf("(%u %u %s %u)", now.pos[i], now.len[i], tag, (unsigned)ttl[i]);
    }

    for (uint8_t i = 0; i < debug_last.count; i++) {
        if (snapshot_contains(&now, debug_last.pos[i], debug_last.len[i])) {
            continue;
        }
        if (printed) {
            printf("  ");
        }
        printed = true;
        printf("(%u %u X %u)", debug_last.pos[i], debug_last.len[i], 0u);
    }

    printf("\n");
    debug_last = now;
}

void slide_reset(void)
{
    memset(tracks, 0, sizeof(tracks));
    debounce_state = 0;
    memset(debounce_cnt, 0, sizeof(debounce_cnt));
    memset(&debug_last, 0, sizeof(debug_last));
    stable_gesture = SLIDE_GESTURE_NONE;
    hold_frames = 0;
}

void slide_set_latch(uint16_t frames)
{
    latch_frames = (frames == 0) ? 1 : frames;
}

void slide_set_debug_cluster(bool enable)
{
    debug_cluster = enable;
    if (!enable) {
        memset(&debug_last, 0, sizeof(debug_last));
    }
}

void slide_process(uint32_t mask, slide_result_t *out)
{
    cluster_t clusters[SLIDE_MAX_CLUSTERS];
    bool used[SLIDE_MAX_CLUSTERS] = {0};
    int8_t idx0 = -1;
    int8_t idx1 = -1;

    uint8_t count = extract_clusters(debounce_mask(mask), clusters, SLIDE_MAX_CLUSTERS);
    sort_clusters(clusters, count);

    if (tracks[0].active && count > 0) {
        int16_t best = 0x7FFF;
        int8_t best_idx = -1;
        for (uint8_t i = 0; i < count; i++) {
            int16_t d = abs16((int16_t)(clusters[i].center_q4 - tracks[0].pos_q4));
            if (d < best) {
                best = d;
                best_idx = (int8_t)i;
            }
        }
        if (best_idx >= 0) {
            idx0 = best_idx;
            used[(uint8_t)best_idx] = true;
        }
    }

    if (tracks[1].active && count > 0) {
        int16_t best = 0x7FFF;
        int8_t best_idx = -1;
        for (uint8_t i = 0; i < count; i++) {
            if (used[i]) {
                continue;
            }
            int16_t d = abs16((int16_t)(clusters[i].center_q4 - tracks[1].pos_q4));
            if (d < best) {
                best = d;
                best_idx = (int8_t)i;
            }
        }
        if (best_idx >= 0) {
            idx1 = best_idx;
            used[(uint8_t)best_idx] = true;
        }
    }

    for (uint8_t i = 0; i < count; i++) {
        if (used[i]) {
            continue;
        }
        if (idx0 < 0) {
            idx0 = (int8_t)i;
            used[i] = true;
            continue;
        }
        if (idx1 < 0) {
            idx1 = (int8_t)i;
            used[i] = true;
            continue;
        }
        break;
    }

    update_track(&tracks[0], idx0 >= 0, (idx0 >= 0) ? &clusters[(uint8_t)idx0] : NULL);
    update_track(&tracks[1], idx1 >= 0, (idx1 >= 0) ? &clusters[(uint8_t)idx1] : NULL);

    apply_latch(classify_gesture(), count > 0);
    debug_print_clusters(clusters, count);

    out->gesture = stable_gesture;
    out->left_x = SLIDE_NEUTRAL_AXIS;
    out->right_x = SLIDE_NEUTRAL_AXIS;
    out->cluster_count = count;

    switch (stable_gesture) {
        case SLIDE_GESTURE_SINGLE_LEFT:
            out->left_x = axis_from_dir(-1);
            break;
        case SLIDE_GESTURE_SINGLE_RIGHT:
            out->left_x = axis_from_dir(1);
            break;
        case SLIDE_GESTURE_DUAL_LEFT:
            out->left_x = axis_from_dir(-1);
            out->right_x = axis_from_dir(-1);
            break;
        case SLIDE_GESTURE_DUAL_RIGHT:
            out->left_x = axis_from_dir(1);
            out->right_x = axis_from_dir(1);
            break;
        case SLIDE_GESTURE_DUAL_CONVERGE:
            out->left_x = axis_from_dir(1);
            out->right_x = axis_from_dir(-1);
            break;
        case SLIDE_GESTURE_DUAL_DIVERGE:
            out->left_x = axis_from_dir(-1);
            out->right_x = axis_from_dir(1);
            break;
        default:
            break;
    }
}
