/* Name: main.c
 * Project: HID-Test
 * Author: Christian Starkjohann
 * Creation Date: 2006-02-02
 * Tabsize: 4
 * Copyright: (c) 2006 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt) or proprietary (CommercialLicense.txt)
 * This Revision: $Id$
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "usbdrv.h"
#include "oddebug.h"

/* ----------------------- hardware I/O abstraction ------------------------ */

#define MORSE_PIN PINC
#define MORSE_BIT 2

#define MORSE_PORT PORTC
#define MORSE_DDR DDRC

static void hardwareInit(void)
{
uchar	i, j;

    MORSE_PORT = 0xff;   /* activate all pull-ups */
    MORSE_DDR = 0;       /* all pins input */
    PORTD = 0xfa;   /* 1111 1010 bin: activate pull-ups except on USB lines */
    DDRD = 0x07;    /* 0000 0111 bin: all pins input except USB (-> USB reset) */
	j = 0;
	while(--j){     /* USB Reset by device only required on Watchdog Reset */
		i = 0;
		while(--i); /* delay >10ms for USB reset */
	}
    DDRD = 0x02;    /* 0000 0010 bin: remove USB reset condition */
    /* configure timer 0 for a rate of 12M/(1024 * 256) = 45.78 Hz (~22ms) */
    TCCR0 = 5;      /* timer 0 prescaler: 1024 */
}

/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

static uchar    reportBuffer[2] = {0};    /* buffer for HID reports */
static uchar    idleRate;           /* in 4 ms units */

const PROGMEM char usbHidReportDescriptor[35] = {   /* USB report descriptor */
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,                    // USAGE (Keyboard)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
    0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x08,                    //   REPORT_COUNT (8)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
    0x19, 0x00,                    //   USAGE_MINIMUM (Reserved (no event indicated))
    0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
    0xc0                           // END_COLLECTION
};
/* We use a simplifed keyboard report descriptor which does not support the
 * boot protocol. We don't allow setting status LEDs and we only allow one
 * simultaneous key press (except modifiers). We can therefore use short
 * 2 byte input reports.
 * The report descriptor has been created with usb.org's "HID Descriptor Tool"
 * which can be downloaded from http://www.usb.org/developers/hidpage/.
 * Redundant entries (such as LOGICAL_MINIMUM and USAGE_PAGE) have been omitted
 * for the second INPUT item.
 */

/* Keyboard usage values, see usb.org's HID-usage-tables document, chapter
 * 10 Keyboard/Keypad Page for more codes.
 */
#define KEY_SPACE     0x2c
static inline uchar    keyPressed(void)
{
    return ! (MORSE_PIN & (1<<MORSE_BIT));
}
static inline void buildReport()
{
    reportBuffer[1] = keyPressed()?KEY_SPACE:0;
}

uchar	usbFunctionSetup(uchar data[8])
{
usbRequest_t    *rq = (void *)data;

    usbMsgPtr = reportBuffer;
    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS){    /* class request type */
        if(rq->bRequest == USBRQ_HID_GET_REPORT){  /* wValue: ReportType (highbyte), ReportID (lowbyte) */
            /* we only have one report type, so don't look at wValue */
            buildReport();
            return sizeof(reportBuffer);
        }else if(rq->bRequest == USBRQ_HID_GET_IDLE){
            usbMsgPtr = &idleRate;
            return 1;
        }else if(rq->bRequest == USBRQ_HID_SET_IDLE){
            idleRate = rq->wValue.bytes[1];
        }
    }else{
        /* no vendor specific requests implemented */
    }
	return 0;
}

/* ------------------------------------------------------------------------- */

int	main(void)
{
uchar   key, lastKey = 0, keyDidChange = 0;
uchar   idleCounter = 0;

	wdt_enable(WDTO_2S);
    hardwareInit();
	odDebugInit();
	usbInit();
	sei();
    DBG1(0x00, 0, 0);
	for(;;){	/* main event loop */
		wdt_reset();
		usbPoll();
        key = keyPressed();
        if(lastKey != key){
            lastKey = key;
            keyDidChange = 1;
        }
        if(TIFR & (1<<TOV0)){   /* 22 ms timer */
            TIFR = 1<<TOV0;
            if(idleRate != 0){
                if(idleCounter > 4){
                    idleCounter -= 5;   /* 22 ms in units of 4 ms */
                }else{
                    idleCounter = idleRate;
                    keyDidChange = 1;
                }
            }
        }
        if(keyDidChange && usbInterruptIsReady()){
            keyDidChange = 0;
            /* use last key and not current key status in order to avoid lost
               changes in key status. */
            buildReport();
            usbSetInterrupt(reportBuffer, sizeof(reportBuffer));
        }
	}
	return 0;
}

/* ------------------------------------------------------------------------- */
