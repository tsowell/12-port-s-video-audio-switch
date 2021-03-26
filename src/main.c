#define F_CPU 8000000UL

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

/* Programmatically-generated header containing bitmap data for ribbon of logos
 * generated from PNG files as well as addresses, display names, and ribbon
 * indexes for each input.  This is where inputs[] is defined!
 */
#include "ribbon.h"

/* Font used for for the uptime scroll.  Also programmatically-generated. */
#include "font.h"


static void
spi_init(void)
{
	/* Set /SS, SCK, and MOSI to output. */
	DDRB |= (1 << PB2) | (1 << PB1) | (1 << PB0);
	/* The AVR SPI system will not drive /SS when it is
	 * set as an output, so set it here in order to select
	 * the device. */
	PORTB &= ~(1 << PB0);

	/* Enable SPI master mode, double speed. */
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPI2X);
}

static void
spi_send(unsigned char data)
{
	SPDR = data;
	while (!(SPSR & (1 << SPIF))) {
	}
}


static void
vfd_wait_busy(void)
{
	/* Poll the VFD's busy pin. */
	while (!(PINC & (1 << PC0))) {
	}
}

static void
vfd_wait_notbusy(void)
{
	/* Poll the VFD's busy pin. */
	while ((PINC & (1 << PC0))) {
	}
}

static void
vfd_init(void)
{
	/* Set the VFD reset pin to output. */
	DDRC = (1 << PC1);
	/* Reset VFD with a falling edge on the reset line,
	 * followed by a rising edge 2 ms later.*/
	PORTC = (1 << PC1); /* Reset high. */
	spi_init();
	PORTC = 0x00; /* Reset low. */
	_delay_ms(2);
	PORTC = (1 << PC1); /* Reset high. */

	/* Wait for the VFD to initialize. */
	vfd_wait_busy();
	vfd_wait_notbusy();
	_delay_us(2);
}

static void
vfd_write_byte(unsigned char data)
{
	vfd_wait_notbusy();
	spi_send(data);
}

static void
vfd_write(const unsigned char *data, int len)
{
	while (len--)
		vfd_write_byte(*(data++));
}

static void
vfd_write_bit_image(uint16_t left, uint16_t top,
                    uint16_t width, uint16_t height, const uint8_t *data)
{
	vfd_write_byte(0x1f);
	vfd_write_byte(0x28);
	vfd_write_byte(0x64);
	vfd_write_byte(0x21);
	vfd_write_byte(left & 0x0ff);
	vfd_write_byte(left >> 8);
	vfd_write_byte(top & 0x0ff);
	vfd_write_byte(top >> 8);
	vfd_write_byte(width & 0x0ff);
	vfd_write_byte(width >> 8);
	vfd_write_byte(height & 0x0ff);
	vfd_write_byte(height >> 8);
	vfd_write_byte(1); /* display information (fixed) */
	vfd_write(data, width * height / 8);
}

static void
vfd_brightness(unsigned char n)
{
	vfd_write_byte(0x1f);
	vfd_write_byte(0x58);
	vfd_write_byte(n);
}


/* EEPROM location to store ribbon position. */
#define EEPROM_POS_ADDRESS        (void *)0x000
/* EEPROM location to store uptime data. */
#define EEPROM_BANK0_ADDRESS      (void *)0x100
/* EEPROM location to store write validation. */
#define EEPROM_BANK0_GOOD_ADDRESS (void *)0x002
/* EEPROM location to store duplicate uptime data. */
#define EEPROM_BANK1_ADDRESS      (void *)0x200
/* EEPROM location to store duplicate write validation. */
#define EEPROM_BANK1_GOOD_ADDRESS (void *)0x003

static void
eeprom_write_uptime(const uint32_t *uptimes)
{
	/* Write uptimes to EEPROM.  Store two copies for redundancy.  Each
	 * copy has a flag at EEPROM_BANKN_GOOD_ADDRESS that indicates that the
	 * write was completed.  This is intended to protect against data loss
	 * in the event that the controller is powered down part of the way
	 * through this function. */
	cli();
	eeprom_update_byte(EEPROM_BANK0_GOOD_ADDRESS, 0);
	eeprom_update_block(uptimes, EEPROM_BANK0_ADDRESS,
	                    sizeof(uint32_t) * NUM_INPUTS);
	eeprom_update_byte(EEPROM_BANK0_GOOD_ADDRESS, 1);

	eeprom_update_byte(EEPROM_BANK1_GOOD_ADDRESS, 0);
	eeprom_update_block(uptimes, EEPROM_BANK1_ADDRESS,
	                    sizeof(uint32_t) * NUM_INPUTS);
	eeprom_update_byte(EEPROM_BANK1_GOOD_ADDRESS, 1);
	sei();
}

static void
eeprom_read_uptime(uint32_t *uptimes)
{
	/* Read uptime from EEPROM.  Only read a copy if the flag at
	 * EEPROM_BANKN_GOOD_ADDRESS indicates a complete write. */
	uint8_t bank0_good = 0, bank1_good = 0;

	cli();

	bank0_good = eeprom_read_byte(EEPROM_BANK0_GOOD_ADDRESS);
	bank1_good = eeprom_read_byte(EEPROM_BANK1_GOOD_ADDRESS);

	if (bank0_good) {
		eeprom_read_block(uptimes, EEPROM_BANK0_ADDRESS,
		                  sizeof(uint32_t) * NUM_INPUTS);
	}
	else if (bank1_good) {
		eeprom_read_block(uptimes, EEPROM_BANK1_ADDRESS,
		                  sizeof(uint32_t) * NUM_INPUTS);
	}
	else {
		sei();
		return;
	}

	if (!bank0_good) {
		eeprom_update_byte(EEPROM_BANK0_GOOD_ADDRESS, 0);
		eeprom_update_block(uptimes, EEPROM_BANK0_ADDRESS,
				    sizeof(uint32_t) * NUM_INPUTS);
		eeprom_update_byte(EEPROM_BANK0_GOOD_ADDRESS, 1);
	}
	if (!bank1_good) {
		eeprom_update_byte(EEPROM_BANK1_GOOD_ADDRESS, 0);
		eeprom_update_block(uptimes, EEPROM_BANK1_ADDRESS,
				    sizeof(uint32_t) * NUM_INPUTS);
		eeprom_update_byte(EEPROM_BANK1_GOOD_ADDRESS, 1);
	}
	sei();
}


/* UI state.  Primarily progresses through
 *   S_MENU => S_STOPPED => S_SELECTED => S_CENTERED.
 * with STATE_DELAY delays in between.
 * In the special case that Info is selected in the S_CENTERED state, progress
 * through S_WAITINFOSCROLL => S_INFOSCROLL
 * In any state, rotary encoder movement causes immediate transition to S_MENU.
 * */
volatile static enum {
	S_CENTERED,
	/* The idle state - input is selected, logo is centered.  If Info is
	 * selected, move on to S_WAITINFOSCROLL. */
	S_MENU,
	/* Display visible portion of ribbon, highlight nearest logo, process
	 * rotary encoder movement.  Any movement in any state brings the
	 * device back to S_MENU. */
	S_STOPPED,
	/* Velocity has reached 0 in S_MENU.  Pause before moving on to
	 * S_SELECTED. */
	S_SELECTED,
	/* Center the nearest logo and clear the rest of the ribbon before
	 * moving on to S_CENTERED. */
	S_WAITINFOSCROLL,
	/* Info has been selected.  Pause before scrolling uptimes. */
	S_INFOSCROLL
	/* Scroll uptimes. */
} state = S_STOPPED;

/* Scroll the uptime buffer 1 row every SCROLL_DELAY ticks. */
#define SCROLL_DELAY 4000
/* Ticks to wait before transitioning to the next state in the absense of
 * rotary encoder movement. */
#define STATE_DELAY 32000

/* The last time a state transtition occurred. */
volatile static uint16_t last_ticks = 0;

/* The currently-selected input. */
volatile static int8_t input = 0;


/* Constants and variables for tracking movement on the ribbon.  Motion of the
 * rotary encoder changes velocity, which determines the amount of period
 * change to the position.  Velocity decays over time. */

/* Add velocity to position every POS_INTERVAL ticks. */
#define POS_INTERVAL 500
/* Absolute velocity decreases by 3 every VELOCITY_INTERVAL ticks. */
#define VELOCITY_INTERVAL 4000
#define VELOCITY_DECAY 3

/* pos_ticks counts up to POS_INTERVAL. */
volatile static uint16_t pos_ticks = 0;
/* velocity_ticks counts up to VELOCITY_INTERVAL. */
volatile static uint16_t velocity_ticks = 0;

/* Position on the ribbon.  0 <= pos < ribbon_width
 * 0 is the left-most column. */
volatile static int16_t pos = 0;
/* Velocity of ribbon movement.  -20 < velocity < 20 Positive
 * velocity moves position to the right, negative velocity
 * moves position to the left. */
volatile static int8_t velocity = 0;


enum enc_dir { ENC_DIR_LEFT, ENC_DIR_RIGHT };
volatile static enum enc_dir enc_dir_1 = ENC_DIR_LEFT;
volatile static enum enc_dir enc_dir_0 = ENC_DIR_RIGHT;

/* Shortcuts for setting interrupt trigger conditions in EICRA. */
#define ENC_INT_1_RISE ((1 << ISC11) | (1 << ISC10))
#define ENC_INT_1_FALL ((1 << ISC11)               )
#define ENC_INT_0_RISE ((1 << ISC01) | (1 << ISC00))
#define ENC_INT_0_FALL ((1 << ISC01)               )

static void
encoder_init()
{
	/* Set up encoder ports and interrupts.  This needs to be called every
	 * time the encoder value changes to configure the interrupt triggers
	 * and directions. */

	/* Configure PD0 and PD1 as inputs */
	DDRD &= ~(1 << PD0);
	DDRD &= ~(1 << PD1);

	/* Enable the pull-up resistors */
	PORTD |= (1 << PD0) | (1 << PD1);

	/* PD1:PD0 goes 00 => 01 => 11 => 10 => 00 from left to right, so... */

	/* PP  Configure interrupts based on current encoder value.
	 * DD  enc_dir_1 and enc_dir_0 are passed from the ISRs to
	 * 10  encoder_interrupt().
	 * --
	 * 00: PD1 rise means left, PD0 rise means right
	 * 01: PD0 fall means left, PD1 rise means right
	 * 11: PD1 fall means left, PD0 fall means right
	 * 10: PD0 rise means left, PD1 fall means right
	 * */
	uint8_t d = PIND & 0x03;
        if (d == 0x00) {
		EICRA = ENC_INT_1_RISE | ENC_INT_0_RISE;
		enc_dir_1 = ENC_DIR_LEFT;
		enc_dir_0 = ENC_DIR_RIGHT;
	}
	else if (d == 0x01) {
		EICRA = ENC_INT_1_RISE | ENC_INT_0_FALL;
		enc_dir_1 = ENC_DIR_RIGHT;
		enc_dir_0 = ENC_DIR_LEFT;
	}
	else if (d == 0x03) {
		EICRA = ENC_INT_1_FALL | ENC_INT_0_FALL;
		enc_dir_1 = ENC_DIR_LEFT;
		enc_dir_0 = ENC_DIR_RIGHT;
	}
	else if (d == 0x02) {
		EICRA = ENC_INT_1_FALL | ENC_INT_0_RISE;
		enc_dir_1 = ENC_DIR_RIGHT;
		enc_dir_0 = ENC_DIR_LEFT;
	}

	/* Clear interrupt flags. */
        EIFR |= (1 << INTF1) | (1 << INTF0);
	/* Enable interrupts on both encoder pins. */
	EIMSK |= (1 << INT0) | (1 << INT1);
}

static void
encoder_interrupt(enum enc_dir dir)
{
	/* Called when a rotary encoder interrupt has occurred.  The interrupt
	 * vector supplies the direction set by encoder_init(). */

	/* Transition to the menu state and update pos and velocity. */
	if (dir == ENC_DIR_LEFT) {
		state = S_MENU;

		if (velocity == 0) {
			pos += -1;
			if (pos < 0)
				pos = ribbon_width + pos;
		}
		if (velocity > -20)
			velocity -= VELOCITY_DECAY;
        }
        else {
		state = S_MENU;

		if (velocity == 0) {
			pos += 1;
			pos = pos % ribbon_width;
		}
		if (velocity < 20)
			velocity += VELOCITY_DECAY;
        }
	/* Reinitialize encoder interrupts based on new encoder value. */
	encoder_init();
}

ISR(INT0_vect)
{
        encoder_interrupt(enc_dir_0);
}

ISR(INT1_vect)
{
        encoder_interrupt(enc_dir_1);
}


/* Uptimes for each input.  The first input (info) represents the total uptime.
 * These are incremented by the timer ISR, written to EEPROM periodically, and
 * read back from EEPROM at boot.  */
volatile static uint32_t uptimes[NUM_INPUTS] = { 0 };
/* Set when times are updated by the timer ISR so that the uptime scroll can be
 * redisplayed. */
volatile static uint8_t uptimes_dirty = 1;

/* These are used to dim the display after a certain amount of time is spent
 * on the same input. */
#define DIM_AFTER_MINUTES 2
volatile static uint8_t minutes_this_input = 0;
volatile static uint8_t last_input = 0;

static void
init_uptime_counter()
{
	/* Fire an interrupt every 1 second. */
	TCCR3A = 0;
	TCCR3B |= _BV(WGM32) | _BV(CS32); /* CTC, CLK / 256*/
	TCNT3 = 0;
	/* Timer compare interrupt */
	OCR3A = 31250;
	TIMSK3 |= 1 << OCIE3A;
}

ISR(TIMER3_COMPA_vect)
{
	static uint8_t seconds = 0;
	seconds++;
	if (seconds >= 60) {
		seconds = 0;
		uptimes[0]++;
		if ((state == S_CENTERED) && (inputs[input].address != 0xff)) {
			minutes_this_input++;
			uptimes[input]++;
		}
		uptimes_dirty = 1;
	}
}


static void
render_uptime_line(uint8_t *dst, const char *s)
{
	int c, i;
	for (c = 0; c < 9; c++) {
		if (!s[c])
			break;
		/* Characters are 5 pixels wide.  Only use /[0-9][A-Z]dhm/
		 * font[] is ordered:
		 * ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789dhms */
		for (i = 0; i < 5; i++) {
			if ((s[c] >= '0') && (s[c] <= '9'))
				dst[c*5+i] = font[s[c]-'0'][i];
			else if ((s[c] >= 'A') && (s[c] <= 'Z'))
				dst[c*5+i] = font[s[c]-'A'+10][i];
			else if (s[c] == 'd')
				dst[c*5+i] = font[36][i];
			else if (s[c] == 'h')
				dst[c*5+i] = font[37][i];
			else if (s[c] == 'm')
				dst[c*5+i] = font[38][i];
		}
	}
}

static void
render_uptime(uint8_t *dst, const uint32_t *uptimes)
{
	char s[9];
	uint32_t time;
	int t;
	for (t = 0; t < NUM_INPUTS; t++) {
		render_uptime_line(&dst[80+t*2*40], inputs[t].abbrev);
		time = uptimes[inputs[t].id];
		if ((time / 60) >= 100) {
			sprintf(s, "%2id%2ih",
			        (short)((time / 60) / 24),
				(short)((time % (24 * 60)) / 60));
		}
		else {
			sprintf(s, "%2ih%2im",
			        (short)(time / 60),
				(short)(time % 60));
		}
		render_uptime_line(&dst[80+t*2*40+40], s);
	}
}


static void
blit_ribbon(uint8_t *buf, uint16_t edge0, uint16_t edge1, uint8_t blank)
{
	/* Render the ribbon.  If blank evaluates to true, only render the part
	 * of the ribbon between edge0 and edge1. */
	int16_t px, rx;
	uint8_t *dst, *src;

	/* One column at a time. */
	for (px = 0; px < 140; px++) {
		/* rx is the column for ribbon_pixel to render.  Wrap the
		 * column if <0 or >ribbon_width */
		rx = pos - 70 + px;
		if (rx < 0)
			rx += ribbon_width;
		else
			rx %= ribbon_width;
		dst = &buf[px * 4];
		src = &ribbon_pixel[rx * 4];
		if ((input >= 0) && (rx >= edge0) && (rx < edge1)) {
			/* Render selected logo between edge0 and edge1.  This
			 * is drawn with light pixels on a dark background.
			 * The 0x80 and 0x01 masks below round off the corners
			 * of the black background. */
			if (!blank && (rx == edge0))
				*dst++ = 0x80 | *src++;
			else if (!blank && (rx == (edge1 - 1)))
				*dst++ = 0x80 | *src++;
			else
				*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			if (!blank && (rx == edge0))
				*dst++ = 0x01 | *src++;
			else if (!blank && (rx == (edge1 - 1)))
				*dst++ = 0x01 | *src++;
			else
				*dst++ = *src++;
		}
		else {
			if (blank
			    && ((rx < edge0) || (rx >= edge1))) {
				/* If blank is enabled, don't render outside of
				 * edge0 and edge1. */
				*dst++ = 0;
				*dst++ = 0;
				*dst++ = 0;
				*dst++ = 0;
			}
			else {
				/* Render the area outside of edge0 and edge1
				 * inverted - dark pixels on a light
				 * background. */
				*dst++ = ~(*src++);
				*dst++ = ~(*src++);
				*dst++ = ~(*src++);
				*dst++ = ~(*src++);
			}
		}
	}


}

static void
blit_uptime(uint8_t *buf, const uint8_t *utbuf,
            uint16_t edge0, uint16_t edge1,
	    uint8_t tline, uint8_t trow)
{
	/* Blit 4 lines of text worth of pixels from the uptime buffer into a
	 * window in the main buffer.  tline is the line of text to draw first.
	 * trow is the row within that line to draw first.*/

	/* This is a bit of a mess and should probably just be rewritten if it
	 * ever needs to be changed. */

	uint8_t r;
	int16_t px, rx;
	uint8_t *dst;
	const uint8_t *src;

	/* r is the line of text offset to render. */
	for (r = 0; r < 4; r++) {
		/* px is the column offset in both buf and utbuf. */
		for (px = 0; px < 40; px++) {
			/* rx is the first column of buf to blit into. */
			rx = edge0 - pos + 70 + 2;
			dst = &buf[(rx + px) * 4 + r];
			/* The + 2 is for the two lines of padding in the
			 * uptime buffer. */
			src = &utbuf[((tline + r) %
			             (2 * (NUM_INPUTS) + 2)) * 40 + px];

			if (trow == 0) {
				/* If we started with the 0th column we can do
				 * a straight copy. */
				*dst = *src;
			}
			else {
				/* Otherwise we need to shift the pixels up and
				 * merge them with part of the next line. */
				if (r < 4) {
					if ((tline + r) == (2 * NUM_INPUTS) + 2 - 1) {
						/* The last line in the uptime
						 * buffer needs to be shifted
						 * and combined with the first
						 * line in the uptime buffer in
						 * order to wrap properly. */
						*dst = (utbuf[px] >> (8 - trow)) | (*src << trow);
					}
					else {
						/* Other lines just need to be
						 * shifted and merged with the
						 * first part of the next line.
						 * */
						*dst = (*(src+40) >> (8 - trow)) | (*src << trow);
					}
				}
				else {
					/* And the final line is just the first
					 * part of the next line. */
					*dst = *src << trow;
				}
			}
			/* Finally, clear the first and last row of pixels of
			 * the window to form top and bottom margins. */
			if (r == 0) {
				*dst &= 0x7f;
			}
			else if (r == 3) {
				*dst &= 0xfe;
			}
		}
	}

}

/* An unused multiplexer address. */
#define UNUSED_INPUT 0x17

static int8_t
nearest_input(int16_t pos)
{
	uint8_t i;
	pos = pos % ribbon_width;
	for (i = 0; i < 12; i++) {
		if (inputs[i].address) {
			if ((pos >= (inputs[i].begin))
			    && (pos <= (inputs[i].end))) {
				return i;
			}
		}
	}
	return -1;
}

int
main(void)
{
	uint16_t my_ticks;
	uint8_t blank;
	/* Main video buffer.  Both this and the uptime buffer are in the same
	 * format used by the VFD interface - column-major order, each byte is
	 * 8 consecutive vertical pixels. */
	uint8_t buf[140 * 4] = { 0 };
	/* Uptime buffer, blitted into the video buffer with vertical scrolling
	 * when Info input is selected.  The buffer is 40 pixels wide, and each
	 * line of text takes 40 bytes.  The buffer starts with two empty lines
	 * for spacing, followed by two lines for each input. */
	uint8_t utbuf[80 + 2 * 40 * (NUM_INPUTS)] = { 0 };

	/* edge0 and edge1 are the left and right column boundaries of the
	 * current logo in the ribbon. */
	int16_t edge0 = inputs[0].begin, edge1 = inputs[0].end;


	/* Set multiplexer address pins to outputs. */
	DDRA = 0x1f;
	/* Select an unsed input. */
	PORTA = UNUSED_INPUT;

	vfd_init();
	vfd_brightness(0x08);

	encoder_init();

	sei();

	/* Load previous state */
	pos = eeprom_read_word(EEPROM_POS_ADDRESS);
	if ((pos < 0) || (pos >= ribbon_width)) {
		pos = 0;
	}
	eeprom_read_uptime((uint32_t *)uptimes);

	/* Set up general purpose counter for timing pos, velocity updates and
	 * state transition delays. */
	TCCR1A = 0;
	TCCR1B = _BV(CS12); /* CLK / 256*/
	TCNT1 = 0;

	init_uptime_counter();

	/* Enable writes to clock prescalar. */
	CLKPR = _BV(CLKPCE);
	/* Set clock prescalar division factor to 1. */
	CLKPR = 0;

	while (1) {
		/* The main loop starts with an empty video buffer. */
		memset(buf, 0, 140 * 4);

		/* If any of the uptimes have changed, redraw the whole
		 * vertical uptime buffer. */
		if (uptimes_dirty) {
			uptimes_dirty = 0;
			render_uptime(utbuf, (uint32_t *)uptimes);
			eeprom_write_uptime((uint32_t *)uptimes);
		}

		my_ticks = TCNT1;

		if (state == S_MENU) {
			vfd_brightness(0x08);
			/* Apply velocity to position and decay to velocity. */
			if ((my_ticks - pos_ticks) >= POS_INTERVAL) {
				pos += velocity;
				if (pos < 0)
					pos += ribbon_width;
				else
					pos %= ribbon_width;
				pos_ticks = my_ticks;
			}
			if ((my_ticks - velocity_ticks) >= VELOCITY_INTERVAL) {
				if (velocity < 0)
					velocity++;
				else if (velocity > 0)
					velocity--;
				velocity_ticks = my_ticks;
			}
		}

		/* Find the nearest input to the current position on the
		 * ribbon.  This is used in a few states below, and edge0 and
		 * edge1 are important for rendering the UI. */
		input = nearest_input(pos);
		if (input >= 0) {
			edge0 = inputs[input].begin;
			edge1 = inputs[input].end;
		}

		if ((velocity == 0) && (state == S_MENU)) {
			/* Due to lack of rotary encoder movement, velocity has
			 * decayed to 0. */
			state = S_STOPPED;
			last_ticks = my_ticks;
		}
		else if ((state == S_STOPPED) &&
		    ((my_ticks - last_ticks) >= STATE_DELAY)) {
			/* After a delay there is still no movement, so we have
			 * a selection. */
			state = S_SELECTED;
			last_ticks = my_ticks;
		}
		else if ((state == S_SELECTED)) {
			if ((pos <= (inputs[input].center + 1))
			    && (pos >= (inputs[input].center - 1))) {
				/* Move on to S_CENTERED if the nearest input
				 * is centered in the display. */
				pos = inputs[input].center;
				state = S_CENTERED;
				eeprom_update_word(EEPROM_POS_ADDRESS, pos);
				last_ticks = my_ticks;
			}
			else if ((my_ticks - last_ticks) >= 40) {
				/* Otherwise, automatically scroll left or
				 * right until the nearest input is centered in
				 * the display. */
				uint8_t diff = abs(pos - inputs[input].center);
				if ((pos < 100) && (inputs[input].center >= (ribbon_width - 100))) {
					pos -= diff / 3 + 1;
				}
				else if (pos < inputs[input].center) {
					pos += diff / 3 + 1;
				}
				else {
					pos -= diff / 3 + 1;
				}
				last_ticks = my_ticks;
			}
		}
		else if ((inputs[input].address == 0xff)
		    && ((state == S_CENTERED))) {
			/* If the info logo is centered, pause before
			 * displaying the uptimes. */
			state = S_WAITINFOSCROLL;
			last_ticks = my_ticks;
		}
		else if ((state == S_WAITINFOSCROLL)
		    && ((my_ticks - last_ticks) >= STATE_DELAY)) {
			/* Time to display the uptimes. */
			state = S_INFOSCROLL;
			last_ticks = my_ticks;
		}
		else if (state == S_CENTERED) {
			/* If another logo is cented, latch the corresponding
			 * address onto the multiplexer address bus. */
			if ((input >= 1) && (input < NUM_INPUTS))
				PORTA = inputs[input].address;
			else
				PORTA = UNUSED_INPUT;

			/* Dim the display after after the same logo has been
			 * centered for a while. */
			if (last_input != input) {
				minutes_this_input = 0;
				last_input = input;
			}

			if (minutes_this_input >= DIM_AFTER_MINUTES)
				vfd_brightness(0x01);
		}


		/* In these states, only render the selected logo part of the
		 * ribbon. */
		blank = ((state == S_SELECTED)
		         || (state == S_WAITINFOSCROLL)
		         || (state == S_INFOSCROLL)
		         || (state == S_CENTERED));
		/* Blit the visible portion of the ribbon to the video buffer.
		 * */
		blit_ribbon(buf, edge0, edge1, blank);

		if (state == S_INFOSCROLL) {
			/* Scroll and blit the uptime buffer into a window of
			 * the video buffer. */
			uint8_t tline = 0;
			uint8_t trow = 0;
			if ((my_ticks - last_ticks) >= SCROLL_DELAY) {
				last_ticks = my_ticks;
				trow++;
				if (trow >= 8) {
					tline++;
					trow = 0;
				}
				if (tline >= (2 * (NUM_INPUTS) + 2))
					tline = 0;
			}
			blit_uptime(buf, utbuf, edge0, edge1, tline, trow);
		}

		/* Write out the video buffer to the VFD! */
		vfd_write_bit_image(0, 0, 140, 32, buf);
	}
	return 0;
}

