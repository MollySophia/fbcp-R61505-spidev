#ifndef _R61505_SPI_H_
#define _R61505_SPI_H_

typedef enum {
    MODE_DATA = 0,
    MODE_COMMAND
} DATA_MODE;

int lcd_init(bool flipped, int spi_freq, int channel, int cs);
void lcd_drawPixel(uint16_t x, uint16_t y, uint16_t color);
#endif