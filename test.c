#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "r61505_spi.h"

uint16_t buf[320 * 240];

void main(void) {
    for(int i = 0; i < 320*240; i++)
        buf[i] = 0xfa00;

    lcd_init(0, 48000000, 1, 13);
    lcd_drawBlock16(0, 0, 320, 240, buf);
}