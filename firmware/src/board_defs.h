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

#define RGB_BUTTON_MAP { 3, 2, 1, 0, 4 }
#define BUTTON_DEF { 12, 11, 10, 9, 13, 1, 0} // Main * 4, Start, Aux * 2

/* A0, A1, A2, ... */
#define HALL_KEY_MUX_ADDRS { 19, 20 }
#define HALL_KEY_MUX_MAP { 3, 2, 1, 0 }
#define HALL_KEY_MUX_EN 18
#define HALL_KEY_ADC_CHN 0

#else

#endif
