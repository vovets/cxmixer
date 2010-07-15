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
    DDRB_DDB2 = 0;
    PORTB_PORTB2 = 1;
    DDRB_DDB3 = 0;
    PORTB_PORTB3 = 1;
    DDRB_DDB4 = 1;
}

static void setup_timer1(void) {
    /* TCCR1_CS13 = 1; */
    /* TCCR1_CS12 = 1; */
    TCCR1_CS11 = 1;
    TCCR1_CS10 = 1;
}

static void setup_interrupts(void) {
    PCMSK |= (1<<CH0BIT)+(1<<CH1BIT);
    GIMSK_PCIE = 1;
    TIMSK_TOIE1 = 1;
}

static void state_init(void);
static void on_channel_value(u8_t channel, u16_t width);
static void on_value_normal(u8_t channel, u16_t width);
static void on_value_calibration_wait(u8_t channel, u16_t width);
static void on_value_calibration_ch0_high(u8_t channel, u16_t width);
static void on_value_calibration_ch0_low(u8_t channel, u16_t width);
static void on_value_calibration_ch1_high(u8_t channel, u16_t width);
static void on_value_calibration_ch1_low(u8_t channel, u16_t width);
static void calibration_store(void);

//global variables
static struct channel_queue_t channel_queue[2];
static struct channel_state_t channels[2];
static on_value_t on_value;
static on_value_t on_value_after_wait;
static u8_t pinb;
static u8_t counter;

void main(void) {
    setup_io();
    setup_timer1();
    setup_interrupts();
    state_init();
    __enable_interrupt();
    while (1) {
        if (channels[0].mailbox.not_empty) {
            u8_t flag = channels[0].mailbox.flag;
            if (!(flag & 0x01)) {
                u16_t width = channels[0].mailbox.value;
                if (channels[0].mailbox.flag == flag) {
                    channels[0].mailbox.not_empty = 0;
                    on_channel_value(0, width);
                }
            }
        }
        if (channels[1].mailbox.not_empty) {
            u8_t flag = channels[1].mailbox.flag;
            if (!(flag & 0x01)) {
                u16_t width = channels[1].mailbox.value;
                u8_t not_empty = channels[1].mailbox.not_empty;
                if (not_empty && channels[1].mailbox.flag == flag) {
                    channels[1].mailbox.not_empty = 0;
                    on_channel_value(1, width);
                }
            }
        }
    }
}

#pragma vector=TIM1_OVF_vect
__interrupt void timer1_ovf(void);

#pragma vector=PCINT0_vect
__interrupt void pcint(void);

__interrupt void timer1_ovf(void) {
    if (pinb & (1<<CH0BIT))
        ++channels[0].input.timer_cycles;
    if (pinb & (1<<CH1BIT))
        ++channels[1].input.timer_cycles;
}

#pragma inline=forced
static void pcint_rising_edge(u8_t channel, u8_t timer, u8_t tof) {
    if (!timer || tof)
        channels[channel].input.timer_cycles -= 1;
    channels[channel].input.timer = timer;
}

#pragma inline=forced
static void pcint_falling_edge(u8_t channel, u8_t timer, u8_t tof) {
    u8_t tc = channels[channel].input.timer_cycles;
    channels[channel].input.timer_cycles = 0;
    if (!timer || tof)
        ++tc;
    u16_t width = tc;
    width <<= 8;
    width += timer;
    width -= channels[channel].input.timer;
    ++channels[channel].mailbox.flag;
    channels[channel].mailbox.value = width;
    channels[channel].mailbox.not_empty = 1;
    ++channels[channel].mailbox.flag;
}

#pragma inline=forced
static void pcint_e(u8_t timer, u8_t old_pinb, u8_t new_pinb, u8_t tof) {
    u8_t old_ch0 = old_pinb & (1<<CH0BIT);
    u8_t old_ch1 = old_pinb & (1<<CH1BIT);
    u8_t new_ch0 = new_pinb & (1<<CH0BIT);
    u8_t new_ch1 = new_pinb & (1<<CH1BIT);
    if (old_ch0) {
        if (!new_ch0)
            pcint_falling_edge(0, timer, tof);
    } else {
        if (new_ch0)
            pcint_rising_edge(0, timer, tof);
    }
    if (old_ch1) {
        if (!new_ch1)
            pcint_falling_edge(1, timer, tof);
    } else {
        if (new_ch1)
            pcint_rising_edge(1, timer, tof);
    }
}

__interrupt void pcint(void) {
    u8_t tof = TIFR_TOV1;
    u8_t timer = TCNT1;
    u8_t new_pinb = PINB;
    u8_t old_pinb = pinb;
    pinb = new_pinb;
    __enable_interrupt();
    pcint_e(timer, old_pinb, new_pinb, tof);
}

static void state_init() {
    on_value = is_jumper_installed() ? &on_value_calibration_ch0_high : &on_value_normal;
    channels[0].stats.min = 0xffff;
    channels[1].stats.min = 0xffff;
}

static void on_channel_value(u8_t channel, u16_t width) {
    if (!QUEUE_FULL(channel, channel_queue[channel])) {
        channels[channel].stats.width_sum += width;
        QUEUE_PUT(channel, channel_queue[channel], width);
        return;
    }
    u16_t filtered = (channels[channel].stats.width_sum + width) / channel_queue_size;
    u8_t value_read;
    u16_t value;
    QUEUE_GET(channel, channel_queue[channel], value, value_read);
    channels[channel].stats.width_sum -= value;
    channels[channel].stats.width_sum += width;
    QUEUE_PUT(channel, channel_queue[channel], width);
    on_value(channel, filtered);
}

static void on_value_normal(u8_t channel, u16_t width) {
    channels[channel].stats.min = MIN(channels[channel].stats.min, width);
    channels[channel].stats.max = MAX(channels[channel].stats.max, width);
    if (channels[channel].stats.max - channels[channel].stats.min > 14) {
        LEDPORT = 1;
    }
}

static void on_value_calibration_wait(u8_t channel, u16_t width) {
    --counter;
    if (!counter) {
        on_value = on_value_after_wait;
    }
}

static u8_t on_value_calibration_high(u8_t channel, u16_t width) {
    if (channels[channel].stats.max < width) {
        channels[channel].stats.max = width;
        counter = 0;
    } else {
        ++counter;
    }
    return counter > calibration_cycles;
}

static u8_t on_value_calibration_low(u8_t channel, u16_t width) {
    if (channels[channel].stats.min > width) {
        channels[channel].stats.min = width;
        counter = 0;
    } else {
        ++counter;
    }
    return counter > calibration_cycles;
}

static void on_value_calibration_ch0_high(u8_t channel, u16_t width) {
    if (channel)
        return;
    if (on_value_calibration_high(0, width)) {
        LEDPORT = 1;
        counter = calibration_cycles;
        on_value = &on_value_calibration_wait;
        on_value_after_wait = &on_value_calibration_ch0_low;
    }
}

static void on_value_calibration_ch0_low(u8_t channel, u16_t width) {
    LEDPORT = 0;
    if (channel)
        return;
    if (on_value_calibration_low(0, width)) {
        LEDPORT = 1;
        counter = calibration_cycles;
        on_value = &on_value_calibration_wait;
        on_value_after_wait = &on_value_calibration_ch1_high;
    }
}

static void on_value_calibration_ch1_high(u8_t channel, u16_t width) {
    LEDPORT = 0;
    if (!channel)
        return;
    if (on_value_calibration_high(1, width)) {
        LEDPORT = 1;
        counter = calibration_cycles;
        on_value = &on_value_calibration_wait;
        on_value_after_wait = &on_value_calibration_ch1_low;
    }
}

static void on_value_calibration_ch1_low(u8_t channel, u16_t width) {
    LEDPORT = 0;
    if (!channel)
        return;
    if (on_value_calibration_low(1, width)) {
        LEDPORT = 1;
        calibration_store();
    }
}

static void calibration_store() {
    calibration_store();
}
