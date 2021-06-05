#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "r61505_spi.h"

uint16_t buf[320 * 240];

void main(void) {
    memset(buf, 0x0000, sizeof(buf));

    lcd_init(0, 48000000, 1, 13);
    lcd_drawBlock(0, 0, 320, 240, buf);
}