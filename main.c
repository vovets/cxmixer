#include <ioavr.h>
#include <intrinsics.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "queue.h"

static u8_t is_jumper_installed(void) {
    u8_t old_DDRB = DDRB;
    u8_t old_PORTB = PORTB;
    u8_t ret = 0;
    DDRB_DDB0 = 1;    // pb0 as output
    DDRB_DDB1 = 0; // pb1 as input
    PORTB_PORTB1 = 1;   // pullup on pb1

    PORTB_PORTB0 = 1;   // pb0 high
    __delay_cycles(10);
    if (PINB_PINB0 == 0)
        goto is_jumper_installed_end;

    PORTB_PORTB0 = 0;
    __delay_cycles(10);
    if (PINB_PINB0 != 0)
        goto is_jumper_installed_end;

    DDRB_DDB1 = 1;
    DDRB_DDB0 = 0;
    PORTB_PORTB0 = 1;

    PORTB_PORTB1 = 1;
    __delay_cycles(10);
    if (PINB_PINB0 == 0)
        goto is_jumper_installed_end;

    PORTB_PORTB1 = 0;
    __delay_cycles(10);
    if (PINB_PINB0 != 0)
        goto is_jumper_installed_end;

    ret=1;

is_jumper_installed_end:
    DDRB = old_DDRB;
    PORTB = old_PORTB;
    return ret;
}

static void setup_io(void) {
    DDRB_DDB0 = 1;
    DDRB_DDB1 = 1;
    DDRB_DDB2 = 0;
    PORTB_PORTB2 = 1;
    DDRB_DDB3 = 0;
    PORTB_PORTB3 = 1;
    DDRB_DDB4 = 1;
}

static void load_osccal(void) {
    OSCCAL = 0x6c;
}

static void state_init(void);
static void calibration_store(void);
static void calibration_load(void);

//global variables
static struct channel_queue_t channel_queue[2];
static u8_t pinb;
static u16_t counter;
static u8_t calibration_mode;
static u16_t period_current;
static u16_t frq;
static volatile u8_t output_completed;
static volatile u8_t frame_captured;
static u8_t output_enabled = 1;
static union {
    u16_t v;
    struct {
        u8_t l;
        u8_t h;
    } b;
} frame[4];
u8_t input_timer_high;
static union {
    u16_t v;
    struct {
        u8_t l;
        u8_t h;
    } b;
} output_timer;
static i8_t delta = 1;
static volatile u16_t ch0 = 1500;
static u16_t ch1 = 1500;
static u16_t period = 20000;
static u8_t frame_bits;

__eeprom struct eeprom_t calibration_data;

static void capture_frame(void);
static void calculate_channels(void);
static void produce_output(void);
static void wait(u16_t width);
static void setup_timer0(void);
static void wait_for_output_comletion(void);
static void produce_pulse(u8_t ch, u16_t width);
static void start_timer0(void);
static void stop_timer0(void);
static void start_timer1(void);
static void stop_timer1(void);

#pragma optimize=s none
void main(void) {
    load_osccal();
    setup_io();
    __enable_interrupt();
    while(1) {
        capture_frame();
        calculate_channels();
        produce_output();
        /* ch0 += delta; */
        /* if (ch0 < 1000 || ch0 > 2000) */
        /*     delta = -delta; */
    }
}

#pragma optimize=s none
void capture_frame(void) {
    input_timer_high = 0;
    pinb = 0;
    TIFR_TOV1 = 1;
    frame_bits = 0;
    PCMSK = (1<<CH0BIT) | (1<<CH1BIT);
    GIFR_PCIF = 1;
    start_timer1();
    GIMSK_PCIE = 1;
    while(!(frame_bits == 0x0f))
        ;
    stop_timer1();
}

void calculate_channels(void) {
    ch0 = frame[1].v - frame[0].v;
    ch1 = frame[3].v - frame[2].v;
}

static void produce_output() {
    produce_pulse(0, ch0);
    produce_pulse(1, ch1);
}

static void setup_timer0(void) {
    /* adjust timer such that overflov and compare match
       wont't happen simultaneously */
    enum {
        tstart = 128,
        tmin = 50
    };
    if (output_timer.b.l < tmin) {
        u8_t adjust = tmin - output_timer.b.l;
        TCNT0 = tstart + adjust;
        OCR0A = OCR0B = adjust;
        output_timer.b.l = tmin;
    } else {
        OCR0A = OCR0B = 0;
        TCNT0 = tstart;
    }
    ++output_timer.b.h;
}

#pragma optimize=s none
static void wait_for_output_comletion(void) {
    while (!output_completed)
        ;
}

static void produce_pulse(u8_t ch, u16_t width) {
    output_timer.v = width;
    setup_timer0();
    /* turn on ch on compare match */
    if (ch) {
        TCCR0A_COM0B1 = 1;
        TCCR0A_COM0B0 = 1;
    } else {
        TCCR0A_COM0A1 = 1;
        TCCR0A_COM0A0 = 1;
    }
    output_completed = 0;
    start_timer0();
    wait_for_output_comletion();
}

static void wait(u16_t width) {
    output_timer.v = width;
    setup_timer0();
    output_completed = 0;
    start_timer0();
    wait_for_output_comletion();
}

static void start_timer0() {
    GTCCR_TSM = 1;
    GTCCR_PSR0 = 1;
    TCCR0B_CS01 = 1;
    TIFR_TOV0 = 1;
    TIMSK_TOIE0 = 1;
    GTCCR_TSM = 0;
}

static void stop_timer0() {
    TCCR0B_CS01 = 0;
}

static void start_timer1() {
    GTCCR_TSM = 1;
    GTCCR_PSR1 = 1;
    TCCR1_CS12 = 1;
    TIFR_TOV1 = 1;
    TIMSK_TOIE1 = 1;
    GTCCR_TSM = 0;
}

static void stop_timer1() {
    TCCR1_CS12 = 0;
}

/* interrupt handlers */

#pragma vector=TIM1_OVF_vect
__interrupt void timer1_ovf(void);

#pragma vector=PCINT0_vect
__interrupt void pcint(void);

#pragma vector=TIM0_OVF_vect
__interrupt void timer0_ovf(void);

#pragma vector=TIM0_COMPA_vect
__interrupt void timer0_compa(void);

__interrupt void timer1_ovf(void) {
    ++input_timer_high;
}

__interrupt void timer0_ovf(void) {
    --output_timer.b.h;
    if (!output_timer.b.h) {
        OCR0A = output_timer.b.l;
        OCR0B = output_timer.b.l;
        /* put outputs low on compare match */
        TCCR0A = (1<<COM0A1) | (1<<COM0B1);
        TIMSK_OCIE0A = 1;
        TIMSK_TOIE0 = 0;
        TIFR_OCF0A = 1;
    }
}

__interrupt void timer0_compa(void) {
    TIMSK_OCIE0A = 0;
    stop_timer0();
    output_completed = 1;
}

__interrupt void pcint(void) {
    u8_t tof = TIFR_TOV1;
    u8_t timer = TCNT1;
    if (tof || !timer) {
        ++input_timer_high;
        TIFR_TOV1 = 1;
    }
    u8_t new_pinb = PINB;
    u8_t old_pinb = pinb;
    pinb = new_pinb;
    if (old_pinb & (1<<CH0BIT)) {
        /* was high */
        if (!(new_pinb & (1<<CH0BIT))) {
            /* falling edge */
            if (frame_bits & 0x01) {
                frame[1].b.h = input_timer_high;
                frame[1].b.l = timer;
                frame_bits |= 0x02;
            }
        }
    } else {
        if (new_pinb & (1<<CH0BIT)) {
            /* rising edge */
            if (!(frame_bits & 0x01)) {
                frame[0].b.h = input_timer_high;
                frame[0].b.l = timer;
                frame_bits |= 0x01;
            }
        }
    }

    if (old_pinb & (1<<CH1BIT)) {
        /* was high */
        if (!(new_pinb & (1<<CH1BIT))) {
            /* falling edge */
            if (frame_bits & 0x01) {
                frame[3].b.h = input_timer_high;
                frame[3].b.l = timer;
                frame_bits |= 0x08;
            }
        }
    } else {
        if (new_pinb & (1<<CH1BIT)) {
            /* rising edge */
            if (frame_bits & 0x01) {
                frame[2].b.h = input_timer_high;
                frame[2].b.l = timer;
                frame_bits |= 0x04;
            }
        }
    }

    if (frame_bits == 0x03) {
        PCMSK = 0;
        GIMSK_PCIE = 0;
        GIFR_PCIF = 0;
    }
}
