/*
 * Diva Controller Board Definitions
 * WHowe <github.com/whowechina>
 */

#if defined BOARD_DIVA_PICO

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define I2C_FREQ 433*1000

#define RGB_PIN 2
#define RGB_ORDER GRB // or RGB

#define RGB_BUTTON_MAP { 2, 0, 1, 3, 4 }
#define BUTTON_DEF { 11, 9, 10, 12, 13, 1, 0}

#define NKRO_KEYMAP "awsdz2swx3dec4frv5gtb6hyn7jum8ki90olp,."
#else

#endif
