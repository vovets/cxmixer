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

static void enable_timer1(void) {
    TCCR1_CS12 = 1;
}

static void setup_interrupts(void) {
    PCMSK |= (1<<CH0BIT)+(1<<CH1BIT);
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
static u8_t output_enabled = 1;
static union {
    u16_t v;
    struct {
        u8_t low;
        u8_t high;
    } b;
} output_timer_u;
static u8_t input_in_sync;
static i8_t delta = 1;
static u16_t input_timer;
static volatile u16_t ch0 = 1500;
static u16_t ch1 = 1500;
static u16_t period = 20000;

__eeprom struct eeprom_t calibration_data;

static void produce_output(u16_t ch0, u16_t ch1, u16_t period);
static void wait(u16_t width);
static void setup_timer0(void);
static void wait_for_output_comletion(void);
static void produce_pulse_ch0(u16_t width);
static void start_timer0(void);
static void stop_timer0(void);

void main(void) {
    load_osccal();
    setup_io();
    __enable_interrupt();
    while(1) {
        produce_output(ch0, ch1, period);
        ch0 += delta;
        if (ch0 < 1000 || ch0 > 2000)
            delta = -delta;
    }
}

static void produce_output(u16_t ch0, u16_t ch1, u16_t period) {
    produce_pulse_ch0(ch0);
    wait(period - ch0);
}

static void setup_timer0(void) {
    /* adjust timer such that overflov and compare match
       wont't happen simultaneously */
    enum {
        tstart = 128,
        tmin = 50
    };
    if (output_timer_u.b.low < tmin) {
        u8_t adjust = tmin - output_timer_u.b.low;
        TCNT0 = tstart + adjust;
        OCR0A = adjust;
        output_timer_u.b.low = tmin;
    } else {
        OCR0A = 0;
        TCNT0 = tstart;
    }
    ++output_timer_u.b.high;
}

#pragma optimize=s none
static void wait_for_output_comletion(void) {
    while (!output_completed)
        ;
}

static void produce_pulse_ch0(u16_t width) {
    output_timer_u.v = width;
    setup_timer0();
    /* turn on ch on compare match */
    TCCR0A_COM0A1 = 1;
    TCCR0A_COM0A0 = 1;
    output_completed = 0;
    start_timer0();
    wait_for_output_comletion();
}

static void wait(u16_t width) {
    output_timer_u.v = width;
    setup_timer0();
    output_completed = 0;
    start_timer0();
    wait_for_output_comletion();
}

static void start_timer0() {
    GTCCR_TSM = 1;
    GTCCR_PSR0 = 1;
    TCCR0B_CS01 = 1;
    TIMSK_TOIE0 = 1;
    GTCCR_TSM = 0;
}

static void stop_timer0() {
    TCCR0B_CS01 = 0;
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
    input_timer += 256;
}

__interrupt void timer0_ovf(void) {
    --output_timer_u.b.high;
    if (!output_timer_u.b.high) {
        OCR0A = output_timer_u.b.low;
        TCCR0A_COM0A1 = 1;
        TCCR0A_COM0A0 = 0;
        TIMSK_OCIE0A = 1;
        TIMSK_TOIE0 = 0;
        TIFR_OCF0A = 1;
    }
}

__interrupt void timer0_compa(void) {
    TIMSK_OCIE0A = 0;
    stop_timer0();
    TIFR_TOV0 = 1;
    output_completed = 1;
}

__interrupt void pcint(void) {
//    u8_t tof = TIFR_TOV1;
//    u8_t timer = TCNT1;
//    if (tof) {
//        input_timer += 256;
//        TIFR_TOV1 = 1;
//    }
//    input_timer += timer;
//    u8_t new_pinb = PINB;
//    u8_t old_pinb = pinb;
//    pinb = new_pinb;
//    if (old_pinb & (1<<CH0BIT)) {
//        /* was high */
//        if (!(new_pinb & (1<<CH0BIT))) {
//            /* falling edge */
//            ++channels[0].mailbox.flag;
//            channels[0].mailbox.value = input_timer;
//            channels[0].mailbox.not_empty = 1;
//            ++channels[0].mailbox.flag;
//        }
//    } else {
//        if (new_pinb & (1<<CH0BIT)) {
//            /* rising edge */
//            TCNT1 = 0;
//            input_timer = 0;
//            input_in_sync = 1;
//        }
//    }
//
//    if (old_pinb & (1<<CH1BIT)) {
//        /* was high */
//        if (!(new_pinb & (1<<CH1BIT))) {
//            /* falling edge */
//            if (!input_in_sync)
//                return;
//            u16_t width = input_timer - channels[1].input.timer;
//            ++channels[1].mailbox.flag;
//            channels[1].mailbox.value = width;
//            channels[1].mailbox.not_empty = 1;
//            ++channels[1].mailbox.flag;
//        }
//    } else {
//        if (new_pinb & (1<<CH1BIT)) {
//            /* rising edge */
//            channels[1].input.timer = input_timer;
//        }
//    }

}
