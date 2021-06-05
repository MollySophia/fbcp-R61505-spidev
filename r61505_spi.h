#ifndef _R61505_SPI_H_
#define _R61505_SPI_H_

int lcd_init(bool flipped, int spi_freq, int channel, int cs);
void lcd_drawPixel(uint16_t x, uint16_t y, uint16_t color);
void lcd_drawBlock(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *bitmap);
#endif