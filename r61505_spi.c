#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include "r61505_spi.h"
#include <linux/spi/spidev.h>

#define GPIO_OUT 0
#define GPIO_IN 1

static struct spi_ioc_transfer xfer;
static int file_spi = -1, file_cs = -1;
static int cs_pin = -1;
static unsigned char data_buf[3];

static void export_gpio(int pin, int direction);
static void spi_cs(int value);
static void spi_write(int size, unsigned char *data);
static void lcd_writeCMD(unsigned char cmd);
static void lcd_writeDATA16(uint16_t data);
static void lcd_writeREG(uint16_t index, uint16_t data);
static void lcd_setBlock(unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end);

static void export_gpio(int pin, int direction) {
    char tmp[64];
    int file_gpio, rc;
    file_gpio = open("/sys/class/gpio/export", O_WRONLY);
    sprintf(tmp, "%d", pin);
    rc = write(file_gpio, tmp, strlen(tmp));
    close(file_gpio);

    sprintf(tmp, "/sys/class/gpio/gpio%d/direction", pin);
    file_gpio = open(tmp, O_WRONLY);
    if(direction == GPIO_OUT) {
        rc = write(file_gpio, "out", 3);
    }
    else {
        rc = write(file_gpio, "in", 2);
    }

    close(file_gpio);
}

static void spi_cs(int value) {
    if(cs_pin != -1) {
        if(file_cs == -1) {
            char tmp[64];
            sprintf(tmp, "/sys/class/gpio/gpio%d/value", cs_pin);
            file_cs = open(tmp, O_WRONLY);
        }
        write(file_cs, (value == 1 ? "1" : "0"), 1);
    }
}

static void spi_write(int size, unsigned char *data) {
    xfer.tx_buf = (unsigned long)data;
    xfer.len = size;
    ioctl(file_spi, SPI_IOC_MESSAGE(1), &xfer);
}

static void lcd_writeCMD(unsigned char cmd) {
    data_buf[0] = 0x70;
    data_buf[1] = 0x00;
    data_buf[2] = cmd;

    spi_cs(0);
    spi_write(3, data_buf);
    spi_cs(1);
}

static void lcd_writeDATA16(uint16_t data) {
    data_buf[0] = 0x72;
    data_buf[1] = (data >> 8) & 0xff;
    data_buf[2] = data & 0xff;

    spi_cs(0);
    spi_write(3, data_buf);
    spi_cs(1);
}

static void lcd_writeREG(uint16_t index, uint16_t data) {
    lcd_writeCMD(index);
    lcd_writeDATA16(data);
}

int lcd_init(bool flipped, int spi_freq, int channel, int cs) {
    cs_pin = cs;
    int rc, spi_mode = SPI_MODE_0;
    char dev_name[32];
    sprintf(dev_name, "/dev/spidev%d.0", channel);
    file_spi = open(dev_name, O_RDWR);
    if(file_spi < 0) {
        printf("Failed to open SPIdev.\n");
        return -1;
    }

    rc = ioctl(file_spi, SPI_IOC_WR_MODE, &spi_mode);
    if(rc < 0) printf("Failed to set SPI mode.\n");

    rc = ioctl(file_spi, SPI_IOC_WR_MAX_SPEED_HZ, &spi_freq);
    if(rc < 0) printf("Failed to set SPI speed.\n");

    memset(&xfer, 0, sizeof(xfer));
    xfer.speed_hz = spi_freq;
    xfer.cs_change = 0;
    xfer.delay_usecs = 0;
    xfer.bits_per_word = 8;

    export_gpio(cs_pin, GPIO_OUT);

    lcd_writeREG(0x0000,0x0000);
    lcd_writeREG(0x0000,0x0000);
    lcd_writeREG(0x0000,0x0000);
    lcd_writeREG(0x0000,0x0001);
    lcd_writeREG(0x00A4,0x0001);  //CALB=1      //NVM Control 4
        usleep(10000);
    lcd_writeREG(0x0060,0x2700);  //NL          //Driver Output Control
    lcd_writeREG(0x0008,0x0808);  //FP & BP     //Display Control 2

    lcd_writeREG(0x0030,0x0214);                //Gamma Control 1
    lcd_writeREG(0x0031,0x3715);                //Gamma Control 2
    lcd_writeREG(0x0032,0x0604);                //Gamma Control 3
    lcd_writeREG(0x0033,0x0E16);                //Gamma Control 4
    lcd_writeREG(0x0034,0x2211);                //Gamma Control 5
    lcd_writeREG(0x0035,0x1500);                //Gamma Control 6
    lcd_writeREG(0x0036,0x8507);                //Gamma Control 7
    lcd_writeREG(0x0037,0x1407);                //Gamma Control 8
    lcd_writeREG(0x0038,0x1403);                //Gamma Control 9
    lcd_writeREG(0x0039,0x0020);                //Gamma Control 10

    lcd_writeREG(0x0090,0x0015);  //DIVI & RTNI //Panel Interface Control 1
    lcd_writeREG(0x0010,0x0410);  //BT,AP       //Power Control 1
    lcd_writeREG(0x0011,0x0237);  //VC,DC0,DC1  //Power Control 2

    lcd_writeREG(0x0029,0x0046);  //VCM1        //NVM Data Read 2
    lcd_writeREG(0x002A,0x0046);  //VCMSEL,VCM2 //NVM Data Read 3
    lcd_writeREG(0x0007,0x0000);                //Display Control 1

    lcd_writeREG(0x0012,0x0189);  //VRH,VCMR,PSON=0,PON=0   //Power Control 3
    lcd_writeREG(0x0013,0x1100);  //VDV                     //Power Control 4
        usleep(150000);
    lcd_writeREG(0x0012,0x01B9);  //PSON=1,PON=1            //Power Control 3
    lcd_writeREG(0x0001,0x0000);  //Other mode settings     //Driver Output Control
    lcd_writeREG(0x0002,0x0200);  //BC0=1--Line inversion   //LCD Driving Wave Control
    lcd_writeREG(0x0003,0x1038);                //Entry Mode
    lcd_writeREG(0x0009,0x0001);                //Display Control 3
    lcd_writeREG(0x000A,0x0000);                //Display Control 4
    lcd_writeREG(0x000D,0x0000);                
    lcd_writeREG(0x000E,0x0030);  //VCOM equalize
    lcd_writeREG(0x0050,0x0000);  //Display window area setting
    lcd_writeREG(0x0051,0x00EF);
    lcd_writeREG(0x0052,0x0000);
    lcd_writeREG(0x0053,0x013F);
    lcd_writeREG(0x0061,0x0001);
    lcd_writeREG(0x006A,0x0000);
    lcd_writeREG(0x0080,0x0000);
    lcd_writeREG(0x0081,0x0000);
    lcd_writeREG(0x0082,0x005F);
    lcd_writeREG(0x0092,0x0100);
    lcd_writeREG(0x0093,0x0701);
        usleep(80000);
    lcd_writeREG(0x0007,0x0100);  //BASEE=1--Display On
    lcd_writeREG(0x0020,0x0000);
    lcd_writeREG(0x0021,0x0000);
	    usleep(10000);

    return 0;
}

static void lcd_setBlock(unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end) {
    lcd_writeREG(0x0020, y_start);
    lcd_writeREG(0x0021, x_start);

    lcd_writeREG(0x0052, x_start);
    lcd_writeREG(0x0053, x_end);
    lcd_writeREG(0x0050, y_start);
    lcd_writeREG(0x0051, y_end);

    lcd_writeCMD(0x22);
}

void lcd_drawPixel16(uint16_t x, uint16_t y, uint16_t color) {
    lcd_setBlock(x, x, y, y);
    lcd_writeDATA16(color);
}

void lcd_drawBlock16(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *bitmap) {
    unsigned char tmp = 0x72, data[512];
    int pos = 0;
    lcd_setBlock(x, x + width, y, y + height);
    spi_cs(0);
    spi_write(1, &tmp);
    for(int i = 0; i < width * height; i++) {
        data[pos] = (bitmap[i] >> 8) & 0xff;
        data[pos + 1] = bitmap[i] & 0xff;
        pos += 2;
        if(pos == 512) {
            spi_write(512, data);
            pos = 0;
        }
    }
    spi_write(pos, data);
    spi_cs(1);
}

void lcd_drawBlock8(int x, int y, int width, int height, unsigned char *bitmap) {
    unsigned char tmp = 0x72, data[512];
    int pos = 0;
    lcd_setBlock(x, x + width, y, y + height);
    spi_cs(0);
    spi_write(1, &tmp);
    for(int i = 0; i < width * height; i++) {
        data[pos] = bitmap[i];
        pos += 2;
        if(pos == 512) {
            spi_write(512, data);
            pos = 0;
        }
    }
    spi_write(pos, data);
    spi_cs(1);
}

int lcd_drawTile(int x, int y, int tileWidth, int tileHeight, unsigned char *tile, int pitch) {
    if(file_spi < 0) 
        return -1;
    if(tileWidth * tileHeight > 2048)
        return -1;
    

}


