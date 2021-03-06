/*
    ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <ch.h>
#include <hal.h>

#include <chrtclib.h>
#include <shell.h>
#include <chprintf.h>

#include <myusb.h>

#include "yx32b.h"


/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

#define SHELL_WA_SIZE   THD_WA_SIZE(2048)
#define TEST_WA_SIZE    THD_WA_SIZE(256)

static void cmd_mem(BaseSequentialStream *chp, int argc, char *argv[]) {
    size_t n, size;

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: mem\r\n");
        return;
    }
    n = chHeapStatus(NULL, &size);
    chprintf(chp, "core free memory : %u bytes\r\n", chCoreStatus());
    chprintf(chp, "heap fragments   : %u\r\n", n);
    chprintf(chp, "heap free total  : %u bytes\r\n", size);
}

static void cmd_threads(BaseSequentialStream *chp, int argc, char *argv[]) {
    static const char *states[] = {THD_STATE_NAMES};
    Thread *tp;

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: threads\r\n");
        return;
    }
    chprintf(chp, "    addr    stack prio refs     state time\r\n");
    tp = chRegFirstThread();
    do {
        chprintf(chp, "%.8lx %.8lx %4lu %4lu %9s %lu\r\n",
                 (uint32_t)tp, (uint32_t)tp->p_ctx.r13,
                 (uint32_t)tp->p_prio, (uint32_t)(tp->p_refs - 1),
                 states[tp->p_state], (uint32_t)tp->p_time);
        tp = chRegNextThread(tp);
    } while (tp != NULL);
}

/*
static void cmd_write(BaseSequentialStream *chp, int argc, char *argv[]) {
    static uint8_t buf[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: write\r\n");
        return;
    }

    while (chnGetTimeout((BaseChannel *)chp, TIME_IMMEDIATE) == Q_TIMEOUT) {
        chSequentialStreamWrite(&SDU1, buf, sizeof buf - 1);
    }
    chprintf(chp, "\r\n\nstopped\r\n");
}
*/

static void cmd_lcd_init(BaseSequentialStream *chp, int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    chprintf(chp, "\r\nTry to init LCD\r\n");
    uint16_t status = lcdInit();
    chprintf(chp, "LCD status 0x%04x\r\n", status);
}

static void cmd_set_utime(BaseSequentialStream *chp, int argc, char *argv[]) {
    time_t utime;
    if ((argc != 1) || (utime = atoi(argv[0])) == 0) {
        chprintf(chp, "Usage: set_utime <unix time value>\r\n");
        return;
    }
    rtcSetTimeUnixSec(&RTCD1, utime);
    chprintf(chp, "Set new time %d\r\n", utime);
}

static const ShellCommand commands[] = {
    {"mem", cmd_mem},
    {"threads", cmd_threads},
    // {"write", cmd_write},
    {"init", cmd_lcd_init},
    {"set_utime", cmd_set_utime},
    {NULL, NULL}
};

static const ShellConfig shell_cfg1 = {
    (BaseSequentialStream *)&SDU1,
    commands
};

/*===========================================================================*/
/* Generic code.                                                             */
/*===========================================================================*/

/*
 * Red LED blinker thread, times are in milliseconds.
 */
static WORKING_AREA(waThread1, 128);
static msg_t Thread1(void *arg)
{

    (void)arg;
    chRegSetThreadName("blinker");
    while (!chThdShouldTerminate()) {
        systime_t time = myusbState() == USB_ACTIVE ? 250 : 500;
        // palClearPad(IOPORT3, GPIOC_LED);
        chThdSleepMilliseconds(time);
        // palSetPad(IOPORT3, GPIOC_LED);
        chThdSleepMilliseconds(time);
    }
    return 0;
}


RTCTime timespec;
uint32_t prev_time;
char buf[128];

/*
 * Application entry point.
 */
int main(void)
{
    Thread *shelltp = NULL;

    /*
     * System initializations.
     * - HAL initialization, this also initializes the configured device drivers
     *   and performs the board-specific initializations.
     * - Kernel initialization, the main() function becomes a thread and the
     *   RTOS is active.
     */
    halInit();
    chSysInit();


    palClearPad(GPIOA, GPIOA_LCD_BL);

    sdStart(&SD1, NULL);
    // sdWrite(&SD1, "Hello world!\r\n", 14);

    myusbInit();

    /*
     * Shell manager initialization.
     */
    shellInit();

    /*
     * Creates the blinker thread.
     */
    chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);

    // chThdSleepMilliseconds(100);
    int lcdStat = lcdInit();
    if (lcdStat == 0) {
        palSetPad(GPIOA, GPIOA_LCD_BL);
    }
    lcdTest();

    /*
     * Normal main() thread activity, in this demo it does nothing except
     * sleeping in a loop and check the button state.
     */
    while (TRUE) {
        // sdWrite(&SD1, "Next line\r\n", 11);
        if (!shelltp && (SDU1.config->usbp->state == USB_ACTIVE))
            shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO);
        else if (chThdTerminated(shelltp)) {
            chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
            shelltp = NULL;           /* Triggers spawning of a new shell.        */
        }

        rtcGetTime(&RTCD1, &timespec);
        if (prev_time != timespec.tv_sec) {
            prev_time = timespec.tv_sec;
            chsnprintf(buf, sizeof(buf), "%10d", timespec.tv_sec);
            lcdStr(20, 20, buf, COLOR_RED, COLOR_BLACK);
            struct tm tim;
            rtcGetTimeTm(&RTCD1, &tim);
            chsnprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
                tim.tm_year + 1900,
                tim.tm_mon + 1,
                tim.tm_mday,
                tim.tm_hour,
                tim.tm_min,
                tim.tm_sec
            );
            lcdStr(20, 28, buf, COLOR_GREEN, COLOR_BLACK);
        }
        chThdSleepMilliseconds(50);
    }
}

