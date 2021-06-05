#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "r61505_spi.h"

void main(void) {
    lcd_init(0, 30000000, 1, 13);
    for(int x =0; x < 320; x++) {
        for(int y = 0; y < 240; y++) {
            lcd_drawPixel(x, y, 0xff00);
        }
    }
}