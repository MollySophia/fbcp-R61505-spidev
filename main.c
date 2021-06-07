#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/uinput.h>
#include "r61505_spi.h"

#define LCD_WIDTH 320
#define LCD_HEIGHT 240

static unsigned char *screen, *altscreen;
static unsigned char *fb;
static int fbPitch, screenSize, bpp;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;

static int lcdPitch;
static int fbfd;
static int tileWidth, tileHeight;
static bool running, showFPS = false, lcdFlip = false, background = false;
static int spiChannel = 1, spiFreq = 33000000, csPin = -1;

static uint64_t nanoClock() {
	uint64_t ns;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	ns = time.tv_nsec + (time.tv_sec * 1000000000LL);
	return ns;
} /* nanoClock() */

static void nanoSleep(uint64_t ns) {
    struct timespec ts;

	if (ns <= 100LL || ns > 999999999LL) return;
	ts.tv_sec = 0;
	ts.tv_nsec = ns;
	nanosleep(&ts, NULL);
} /* NanoSleep() */

static int initDisplay(bool lcdFlip, int spiChannel, int spiFreq, int csPin) {
    fbfd = open("/dev/fb0", O_RDWR);
    if(fbfd) {
        ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
        ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
        screenSize = finfo.smem_len;
        bpp = finfo.smem_len / vinfo.xres / vinfo.yres * 8;
        fbPitch = (vinfo.xres * bpp) / 8;
        fb = (unsigned char *)mmap(0, screenSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    }
    else {
        printf("Failed to open /dev/fb0.\n");
        return -1;
    }

    if(lcd_init(lcdFlip, spiFreq, spiChannel, csPin) != 0) {
        printf("Failed to init lcd.\n");
        return -1;
    }

    lcdPitch = LCD_WIDTH * 2;
    screen = malloc(lcdPitch * LCD_HEIGHT);
    altscreen = malloc(lcdPitch * LCD_HEIGHT);

    return 0;
}

static int findChangedRegion(unsigned char *src, unsigned char *dst, int width,
        int height, int pitch, int tileWidth, int tileHeight, uint32_t *regions)
{
    int x, y, xc, yc, dy;
    int xCount, yCount;
    uint32_t *s, *d, rowBits;
    int totalChanged = 0;

    xCount = (height + tileHeight - 1) / tileHeight;
    yCount = (width + tileWidth - 1) / tileWidth;

    for(yc = 0; yc < yCount; yc++) {
        rowBits = 0;
        for(xc = 0; xc < xCount; xc++) {
            s = (uint32_t *)&src[((yc * tileHeight) * pitch) + (xc * tileWidth * 2)];
            d = (uint32_t *)&dst[((yc * tileHeight) * pitch) + (xc * tileWidth * 2)];

            if((yc + 1) * tileHeight > height)
                dy = height - (yc * tileHeight);
            else
                dy = tileHeight;
            
            for(y = 0; y < dy; y++) {
                for(x = 0; x < tileWidth / 2; x++) {
                    if(s[x] != d[x]) {
                        rowBits |= (1 << xc);
                        totalChanged++;
                        y = tileHeight;
                        x = tileWidth;
                    } 
                } //for x
            } // for y
        }// for xc
        regions[yc] = rowBits;
    }// for yc
    return totalChanged;
}

static void fbCapture(void) {
    if(vinfo.xres >= LCD_WIDTH * 2) { //shrink by 1/4
        if(bpp == 16) {
            uint32_t *s, *d, magic, u32_1, u32_2;
            int x, y;
            magic = 0xf7def7de;

            for(y = 0; y < LCD_HEIGHT; y++) {
                s = (uint32_t *)(&fb[y * 2 * fbPitch]);
                d = (uint32_t *)(&screen[y * lcdPitch]);
                for(x = 0; x < LCD_WIDTH; x ++) {
                    u32_1 = s[0];
                    u32_2 = s[1];
                    u32_1 = (u32_1 & magic) >> 1;
                    u32_2 = (u32_2 & magic) >> 1;
                    u32_1 += (u32_1 << 16);
                    u32_2 += (u32_2 >> 16);
                    u32_1 = (u32_1 >> 16) | (u32_2 << 16);
                    d[0] = u32_1;
                    d++;
                    s += 2;
                }//for x
            }//for y
        }
        else { //convert to RGB565
            uint32_t u32_1, u32_2, *src, magic = 0xf7def7de;;
            uint16_t u16, *dest;
            int x, y;

            for(y = 0; y < LCD_HEIGHT; y++) {
                src = (uint32_t *)&fb[fbPitch * y * 2];
                dest = (uint16_t *)&screen[lcdPitch * y];
                for(x = 0; x < LCD_WIDTH; x++) {
                    u32_1 = src[0];
                    u32_2 = src[1];
                    u32_1 = (u32_1 & magic) >> 1;
                    u32_2 = (u32_2 & magic) >> 1;
                    u32_1 += (u32_1 << 16);
                    u32_2 += (u32_2 >> 16);
                    u32_1 = (u32_1 >> 16) | (u32_2 << 16);
                    u16 = ((u32_1 >> 3) & 0x1f) | ((u32_1 >> 5) & 0x7e0) | ((u32_1 >> 8) & 0xf800);
                    dest[0] = u16;
                    src += 2;
                    dest++;
                }
            }
        }
    }
    else { //1:1
        if(bpp == 16) {
            memcpy(screen, fb, fbPitch * vinfo.yres);
        }
        else {
            uint32_t u32, *src;
            uint16_t u16, *dest;
            int x, y;

            for(y = 0; y < LCD_HEIGHT; y++) {
                src = (uint32_t *)&fb[fbPitch * y];
                dest = (uint16_t *)&screen[lcdPitch * y];
                for(x = 0; x < LCD_WIDTH; x++) {
                    u32 = *src++;
                    u16 = ((u32 >> 3) & 0x1f) | ((u32 >> 5) & 0x7e0) | ((u32 >> 8) & 0xf800);
                    *dest++ = u16;
                }
            }
        }
    }
}

static void copyLoop(void) {
    int changed;
    uint32_t flags, regions[32], *pRegions;
    int i, j, k, x, y, count;

    fbCapture();

    //lcd_drawBlock16(0, 0, 320, 240, screen);
    
    changed = findChangedRegion(screen, altscreen, LCD_WIDTH, LCD_HEIGHT, lcdPitch, tileWidth, tileHeight, regions);
    if(changed) {
        k = 0;
        for(i = 0; i < LCD_HEIGHT; i+= tileHeight) {
            if(regions[k++]) {
                j = tileHeight;
                if(i + j > LCD_HEIGHT) 
                    j = LCD_HEIGHT - i;
                memcpy(&altscreen[i * lcdPitch], (void*)&screen[i * lcdPitch], j * lcdPitch);
            }
        }

        pRegions = regions;
        count = 0;
        
        for(y = 0; y < LCD_HEIGHT; y += tileHeight) {
            flags = *pRegions++;
            for(x = 0; x < LCD_WIDTH; x += tileWidth) {
                if(flags & 1) {
                    lcd_drawBlock8(x, y, tileWidth, tileHeight, &altscreen[(y * lcdPitch) + (x * 2)]);
                    count++;
                    if(count == changed / 2)
                        nanoSleep(4000LL);
                }
                flags >>= 1;
            }
        }
    }
}

static int ParseOpts(int argc, char *argv[]) {
    int i = 1;

    while (i < argc)
    {
        /* if it isn't a cmdline option, we're done */
        if (0 != strncmp("--", argv[i], 2))
            break;
        /* GNU-style separator to support files with -- prefix
         * example for a file named "--baz": ./foo --bar -- --baz
         */
        if (0 == strcmp("--", argv[i]))
        {
            i += 1;
            break;
        }
        /* test for each specific flag */
        if (0 == strcmp("--spi_bus", argv[i])) {
            spiChannel = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--background", argv[i])) {
            background = true;
            i++;
        } else if (0 == strcmp("--spi_freq", argv[i])) {
            spiFreq = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--flip", argv[i])) {
            i++; 
            lcdFlip = true;
        } else if (0 == strcmp("--showfps",argv[i])) {        
            showFPS = true;
            i++;
        } else if(0 == strcmp("--lcd_cs", argv[i])) {
            csPin = atoi(argv[i+1]);
            i += 2;
        } 
        else {
            fprintf(stderr, "Unknown parameter '%s'\n", argv[i]);
            exit(1);
        }   
    }       
    return i;

} /* ParseOpts() */

static void ShowHelp(void) {
    printf(
        "Options:\n"
        " --spi_bus <integer>      defaults to 1\n"
        " --spi_freq <integer>     defaults to 33000000\n"
        " --lcd_cs <pin number>    defaults to 13\n"
        " --flip                   flips display 180 degrees\n"
        " --showfps                Show framerate\n"
	" --background             suppress printf output if running as a bkgd process\n"
    );
} /* ShowHelp() */

void *copyThread(void *arg) {
    int64_t ns;
    uint64_t time, frameDelta, targetTime, oldTime;
    float fps;
    int videoFrames = 0;

    frameDelta = 1000000000 / 60;
    targetTime = oldTime = nanoClock() + frameDelta;

    while(running) {
        copyLoop();
        videoFrames++;
        time = nanoClock();
        ns = targetTime - time;
        if(showFPS && (time - oldTime) > 1000000000LL) {
            fps = (float)videoFrames;
            fps = fps * 1000000000.0;
            fps = fps / (float)(time - oldTime);
            if(!background)
                printf("%02.1f FPS\n", fps);
            videoFrames = 0;
            oldTime = time;
        }

        if(ns < 0) {
            while(ns < 0) {
                ns += frameDelta;
                targetTime += frameDelta;
            }
            nanoSleep(4000LL);
        }
        else {
            nanoSleep(ns);
        }
        targetTime += frameDelta;
    }
    return NULL;
}

void shutdown(void) {
    running = 0; // tell background thread to stop
    nanoSleep(50000000LL); // wait 50ms for work to finish
    //spilcdShutdown();
} 

void signal_handler(int signum) {
	printf("Ctrl-C hit; exiting...\n");
	shutdown();
	exit(0); // quit the program quietly
} /* signal_handler() */

int main(int argc, char **argv) {
    int i;
    pthread_t tinfo;

    if(argc < 2) {
        ShowHelp();
        return 0;
    }

    showFPS = false;
    spiChannel = 1;
    spiFreq = 33000000;
    lcdFlip = false;
    background = false;

    tileWidth = 64;
    tileHeight = 30;

    ParseOpts(argc, argv);

    if(initDisplay(lcdFlip, spiChannel, spiFreq, csPin) != 0) {
        fprintf(stderr, "Failed to init LCD\n");
        return 0;
    }

    printf("/dev/fb0: %dx%d, %dbpp\n", vinfo.xres, vinfo.yres, bpp);
    printf("R: %d G: %d B: %d\n", vinfo.red, vinfo.green, vinfo.blue);

    if (vinfo.xres > 640)
		printf("Warning: the framebuffer is too large and will not be copied properly; sipported sizes are 640x480 and 320x240\n");
	if (bpp == 32)
		printf("Warning: the framebuffer bit depth is 32-bpp, ideally it should be 16-bpp for fastest results\n");

    signal(SIGINT, signal_handler);


    uint64_t llTime;
	int iFrames = 0;
    llTime = nanoClock() + 1000000000LL;
    while (nanoClock() < llTime) // run for 1 second
    {	// force total redraw each frame
        memset(altscreen, 0xff, lcdPitch * LCD_HEIGHT);
        copyLoop();
        iFrames++;
    }
    if (!background)
    {
        printf("Perf test: worst case framerate = %d FPS\n", iFrames);
        if (iFrames < 25)
            printf("<25FPS indicates there is something not configured correctly with your SW/HW\n");
    }


    running = true;
    pthread_create(&tinfo, NULL, copyThread, NULL);

    if(!background) {
        printf("Press ENTER to quit\n");
        getchar();
    } else {
        while(1);
    }

    shutdown();

    return 0;
}