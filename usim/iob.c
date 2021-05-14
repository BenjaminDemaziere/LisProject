/*
 * iob.c
 *
 * simple CADR i/o board simulation
 * support for mouse, keyboard, clock
 *
 * $Id: iob.c 83 2006-08-07 16:08:20Z brad $
 */

#include "usim.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>

#if defined(LINUX) || defined(OSX)
#include <sys/time.h>
#endif

#ifdef DISPLAY_SDL
#ifdef _WIN32
#include <SDL/SDL_keysym.h>
#else
#include "SDL/SDL_keysym.h"
#endif
#endif /* DISPLAY_SDL */

#include "ucode.h"

unsigned int iob_key_scan;
unsigned int iob_kbd_csr;

extern int get_u_pc();

void tv_post_60hz_interrupt(void);

/*
 CADR i/o board

 interrupt vectors:
 260 kdb/mouse
 264 serial
 270 chaos int
 274 clock
 400 ether xmit done
 404 ether rcv done
 410 ether collision

764100
0	0 read kbd
2	1 read kbd
4	2 read mouse y (12 bits)
6	3 read mouse x (12 bits)
10	4 click audio
12	5 kbd/mouse csr

csr - write
0 remote mouse enable
1 mouse int enable
2 kbd int enable
3 clock int enable

csr - read
0 remote mouse eable
1 mouse int enable
2 kbd int enable
3 clock int enable
4 mouse ready
5 kbd ready
6 clock ready
7 ser int enable

keyboard
; 		5-0	keycode
; 		7-6	shift
; 		9-8	top
; 		11-10	control
; 		13-12	meta
; 		14	shift-lock
; 		15	unused

*/

/*
764100
0 read kbd
1 read kbd
2 read mouse y (12 bits)
3 read mouse x (12 bits)
4 click audio
5 kbd/mouse csr

csr - write
0 remote mouse enable
1 mouse int enable
2 kbd int enable
3 clock int enable

csr - read
0 remote mouse eable
1 mouse int enable
2 kbd int enable
3 clock int enable
4 mouse ready
5 kbd ready
6 clock ready
7 ser int enable
*/

#define US_CLOCK_IS_WALL_CLOCK
#if defined(linux) || defined(osx)
#define USE_SIGVTARLM_FOR_60HZ
#endif

#ifdef _WIN32
#define USE_US_CLOCK_FOR_60HZ
#endif

unsigned long
get_us_clock()
{
	unsigned long v;
#ifdef US_CLOCK_IS_WALL_CLOCK
	static unsigned long last_hz60;
	static struct timeval tv;
	struct timeval tv2;
	unsigned long ds, du;

	if (tv.tv_sec == 0) {
		gettimeofday(&tv, 0);
		v = 0;
		last_hz60 = 0;
	} else {
		gettimeofday(&tv2, 0);

		if (tv2.tv_usec < tv.tv_usec) {
			tv2.tv_sec--;
			tv2.tv_usec += 1000*1000;
		}
		ds = tv2.tv_sec - tv.tv_sec;
		du = tv2.tv_usec - tv.tv_usec;

//		v = (ds * 100) + (du / 10000);
		v = (ds * 1000*1000) + du;
		if (0) printf("delta %lu\n", v);

#ifdef USE_US_CLOCK_FOR_60HZ
		hz60 = v / 16000;
		if (hz60 > last_hz60) {
			last_hz60 = hz60;
			tv_post_60hz_interrupt();
		}
#endif
	}
#else
	/* assume 200ns cycle, we want 1us */
	extern long cycles;
	v = cycles * (1000/200);
#endif

	return v;
}

static unsigned long cv;

unsigned int
get_us_clock_low(void)
{
	cv = get_us_clock();
	return cv & 0xffff;
}

unsigned int
get_us_clock_high(void)
{
	return cv >> 16;
}

unsigned int get_60hz_clock(void)
{
	return 0;
}


void
iob_unibus_read(int offset, int *pv)
{
	/* default, for now */
	*pv = 0;

	switch (offset) {
	case 0100:
		*pv = iob_key_scan & 0177777;
		traceio("unibus: kbd low %011o\n", *pv);
		iob_kbd_csr &= ~(1 << 5);
		break;
	case 0102:
		*pv = (iob_key_scan >> 16) & 0177777;
		iob_kbd_csr &= ~(1 << 5);
		traceio("unibus: kbd high %011o\n", *pv);
		break;
	case 0110:
		traceio("unibus: beep\n");
		fprintf(stderr,"\a"); /* alert - beep */
		break;
	case 0112:
		*pv = iob_kbd_csr;
		traceio("unibus: kbd csr %011o\n", *pv);
		break;
	case 0120:
		traceio("unibus: usec clock low\n");
		*pv = get_us_clock_low();
		break;
	case 0122:
		traceio("unibus: usec clock high\n");
		*pv = get_us_clock_high();
		break;
	case 0124:
		printf("unibus: 60hz clock\n");
		*pv = get_60hz_clock();
		break;
	default:
		break;
	}
}

void
iob_unibus_write(int offset, int v)
{
	switch (offset) {
	case 0100:
		traceio("unibus: kbd low\n");
		break;
	case 0102:
		traceio("unibus: kbd high\n");
		break;
	/*case 0104:
		traceio("unibus: mouse y\n");
		break;
	case 0106:
		traceio("unibus: mouse x\n");
		break;*/
	case 0110:
		traceio("unibus: beep\n");
		break;
	case 0112:
		traceio("unibus: kbd csr\n");
		iob_kbd_csr = 
			(iob_kbd_csr & ~017) | (v & 017);
		break;
	case 0120:
		traceio("unibus: usec clock\n");
		break;
	case 0122:
		traceio("unibus: usec clock\n");
		break;
	case 0124:
		printf("unibus: START 60hz clock\n");
		break;
	default:
		break;
	}
}

/*
 * create simulated mouse motion to keep SDL cursor
 * and microcode cursor in sync
 */

int tv_csr;

int
tv_xbus_read(int offset, unsigned int *pv)
{
	if (0) printf("tv register read, offset %o -> %o\n", offset, tv_csr);
	*pv = tv_csr;
	return 0;
}

int
tv_xbus_write(int offset, unsigned int v)
{
	if (0) printf("tv register write, offset %o, v %o\n", offset, v);
	if ((tv_csr & 4) != (v & 4)) {
#ifdef DISPLAY_SDL
		sdl_set_bow_mode((v & 4)>>2);
#endif
	}
	tv_csr = v;
	tv_csr &= ~(1 << 4);
	deassert_xbus_interrupt();
	return 0;
}

//xxx tv interrupt
// tv csr @ base, 1<<4 = interrupt flag
// writing back clears int
// 60hz

void
tv_post_60hz_interrupt(void)
{
	tv_csr |= 1 << 4;
	assert_xbus_interrupt();
}

void
iob_sdl_clock_event()
{
	iob_kbd_csr |= 1 << 6;
	assert_unibus_interrupt(0274);
}

void
sigalrm_handler(int arg)
{
	if (0) printf("sigalrm_handler()\n");
	tv_post_60hz_interrupt();
}

void
iob_poll(unsigned long cycles)
{
#ifndef USE_SIGVTARLM_FOR_60HZ
	/* assume 200ns cycle, we want 16ms */
	if ((cycles % ((16*1000*1000)/200)) == 0) {
		tv_post_60hz_interrupt();
	}
#endif
}


int
iob_init(void)
{
	kbd_init();


#ifdef USE_SIGVTARLM_FOR_60HZ
	{
		struct itimerval itimer;
		int usecs;

		signal(SIGVTALRM, sigalrm_handler);

		usecs = 16000;

		itimer.it_interval.tv_sec = 0;
		itimer.it_interval.tv_usec = usecs;
		itimer.it_value.tv_sec = 0;
		itimer.it_value.tv_usec = usecs;
		setitimer(ITIMER_VIRTUAL, &itimer, 0);
	}
#endif

	return 0;
}

