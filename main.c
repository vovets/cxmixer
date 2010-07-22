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

static void enable_timer0(void) {
    TCCR0B_CS01 = 1;
}

static void setup_interrupts(void) {
    PCMSK |= (1<<CH0BIT)+(1<<CH1BIT);
    /* GIMSK_PCIE = 1; */
    TIMSK_TOIE0 = 1;
    /* TIMSK_TOIE1 = 1; */
}

static void load_osccal(void) {
    OSCCAL = 0x6c;
}

static void state_init(void);
static void on_raw_value(u8_t channel, u16_t width);
static void on_period(u8_t channel, u16_t width);
static void on_value_normal(u8_t channel, u16_t width);
static void on_value_calibration_wait(u8_t channel, u16_t width);
static void on_value_calibration_ch0_high(u8_t channel, u16_t width);
static void on_value_calibration_ch0_low(u8_t channel, u16_t width);
static void on_value_calibration_ch1_high(u8_t channel, u16_t width);
static void on_value_calibration_ch1_low(u8_t channel, u16_t width);
static void calibration_store(void);
static void calibration_load(void);

//global variables
static struct channel_queue_t channel_queue[2];
static struct channel_state_t channels[4];
static on_value_t on_value;
static on_value_t on_value_after_wait;
static u8_t pinb;
static u16_t counter;
static u8_t calibration_mode;
static u16_t period;
static u16_t period_current;
static u16_t frq;
static u8_t output_stage;
static u8_t output_enabled = 1;
static union {
    u16_t v;
    struct {
        u8_t low;
        u8_t high;
    } b;
} output_width_u;
static u8_t input_in_sync;
static i8_t delta = 1;
static u16_t input_timer;

//eeprom
__eeprom struct eeprom_t calibration_data;

void main(void) {
    load_osccal();
    calibration_mode = is_jumper_installed();
    setup_io();
    enable_timer1();
    enable_timer0();
    setup_interrupts();
    state_init();
    __enable_interrupt();
    while (1) {
        u8_t channel;
        if (period) {
            for (channel = 0; channel < 2; ++channel) {
                if (channels[channel].mailbox.not_empty) {
                    u8_t flag = channels[channel].mailbox.flag;
                    if (!(flag & 0x01)) {
                        u16_t width = channels[channel].mailbox.value;
                        if (channels[channel].mailbox.flag == flag) {
                            channels[channel].mailbox.not_empty = 0;
                            on_raw_value(channel, width);
                        }
                    }
                }
            }
        } else {
            for (channel = 2; channel < 4; ++channel) {
                if (channels[channel].mailbox.not_empty) {
                    u8_t flag = channels[channel].mailbox.flag;
                    if (!(flag & 0x01)) {
                        u16_t period = channels[channel].mailbox.value;
                        u8_t not_empty = channels[channel].mailbox.not_empty;
                        if (not_empty && channels[channel].mailbox.flag == flag) {
                            channels[channel].mailbox.not_empty = 0;
                            on_period(channel, period);
                        }
                    }
                }
            }
        }
    }
}

static void state_init() {
    if (calibration_mode) {
        on_value = &on_value_calibration_ch0_high;
        channels[0].stats.min = 0xffff;
        channels[1].stats.min = 0xffff;
        channels[2].stats.min = 0xffff;
        channels[3].stats.min = 0xffff;
    } else {
        calibration_load();
        channels[0].output.width = channels[0].stats.min;
        channels[1].output.width = channels[1].stats.min;
        on_value = &on_value_normal;
    }
}

static void on_raw_value(u8_t channel, u16_t width) {
    /* if (!QUEUE_FULL(channel, channel_queue[channel])) { */
    /*     channels[channel].stats.width_sum += width; */
    /*     QUEUE_PUT(channel, channel_queue[channel], width); */
    /*     return; */
    /* } */
    /* u16_t filtered = (channels[channel].stats.width_sum + width) / channel_queue_size; */
    /* u8_t value_read; */
    /* u16_t value; */
    /* QUEUE_GET(channel, channel_queue[channel], value, value_read); */
    /* channels[channel].stats.width_sum -= value; */
    /* channels[channel].stats.width_sum += width; */
    /* QUEUE_PUT(channel, channel_queue[channel], width); */
    /* if ((channel == calibration_period_channel) && counter) */
    /*     --counter; */
    /* on_value(channel, filtered); */
    on_value(channel, width);
}

static void on_period(u8_t channel, u16_t width) {
    if (!channels[channel].stats.max) {
        channels[channel].stats.max = 1;
        return;
    }
    if (MIN(channels[2].stats.max, channels[3].stats.max) < calibration_periods_count) {
        channels[channel].stats.min = MIN(channels[channel].stats.min, width);
        ++channels[channel].stats.max;
    } else {
        period = MIN(channels[2].stats.min, channels[3].stats.min);
        frq = timer_clock / period;
    }
}

static void on_value_normal(u8_t channel, u16_t width) {
    /* u16_t ch0 = channels[0].output.width; */
    /* if (channel) { */
    /*     ch0 += delta; */
    /*     if((delta > 0 && ch0 >= channels[0].stats.max) */
    /*        || (delta < 0 && ch0 <= channels[0].stats.min)) { */
    /*         delta = -delta; */
    /*     } */
    /*     if (ch0 > channels[0].stats.max) */
    /*         ch0 = channels[0].stats.max; */
    /*     if (ch0 < channels[0].stats.min) */
    /*         ch0 = channels[0].stats.min; */
    /* } */
    u16_t ch0 = channels[0].output.width;
    u16_t ch1 = channels[1].output.width;
    __disable_interrupt();
    channels[0].output.width = ch0;
    channels[1].output.width = ch1;
    __enable_interrupt();
    output_enabled = 1;
}

static void on_value_calibration_wait(u8_t channel, u16_t width) {
    if (!counter) {
        on_value = on_value_after_wait;
    }
}

static u8_t on_value_calibration_high(u8_t channel, u16_t width) {
    if (channels[channel].stats.max < width) {
        channels[channel].stats.max = width;
        counter = frq * calibration_seconds;
    }
    return !counter;
}

static u8_t on_value_calibration_low(u8_t channel, u16_t width) {
    if (channels[channel].stats.min > width) {
        channels[channel].stats.min = width;
        counter = frq * calibration_seconds;
    }
    return !counter;
}

static void on_value_calibration_ch0_high(u8_t channel, u16_t width) {
    if (channel)
        return;
    if (on_value_calibration_high(0, width)) {
        LEDPORT = 1;
        counter = frq * calibration_seconds;
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
        counter = frq * calibration_seconds;
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
        counter = frq * calibration_seconds;
        on_value = &on_value_calibration_wait;
        on_value_after_wait = &on_value_calibration_ch1_low;
    }
}

static void on_value_calibration_ch1_low(u8_t channel, u16_t width) {
    LEDPORT = 0;
    if (!channel)
        return;
    if (on_value_calibration_low(1, width)) {
        calibration_store();
    }
}

#pragma optimize=s none
static void calibration_store() {
    calibration_data.period = period;
    calibration_data.channels[0].min = channels[0].stats.min;
    calibration_data.channels[0].max = channels[0].stats.max;
    calibration_data.channels[1].min = channels[1].stats.min;
    calibration_data.channels[1].max = channels[1].stats.max;
    LEDPORT = 1;
 stop:
    goto stop;
}

static void calibration_load() {
    /* period = calibration_data.period; */
    /* channels[0].stats.min = calibration_data.channels[0].min; */
    /* channels[0].stats.max = calibration_data.channels[0].max; */
    /* channels[1].stats.min = calibration_data.channels[1].min; */
    /* channels[1].stats.max = calibration_data.channels[1].max; */
    period = 15972;
    channels[0].stats.min = 1102;
    channels[0].stats.max = 1923;
    channels[1].stats.min = 1096;
    channels[1].stats.max = 1926;
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
    --output_width_u.b.high;
    if (!output_width_u.b.high) {
        switch (output_stage) {
        case 0:
            TCCR0A_COM0A1 = 1;
            TCCR0A_COM0A0 = 0;
            if (output_enabled) {
                TCCR0A_COM0B1 = 1;
                TCCR0A_COM0B0 = 1;
            }
            output_stage = 1;
            break;
        case 1:
            TCCR0A_COM0B1 = 1;
            TCCR0A_COM0B0 = 0;
            output_stage = 2;
            break;
        case 2:
            if (output_enabled) {
                TCCR0A_COM0A1 = 1;
                TCCR0A_COM0A0 = 1;
            }
            output_stage = 0;
            break;
        }
        OCR0A = OCR0B = output_width_u.b.low;
        TIMSK_OCIE0A = 1;
    }
}

#pragma inline=forced
u8_t adjust_output_width(void) {
    enum { tmin = 50 };
    u8_t tcnt = 0;
    if (output_width_u.b.low < tmin) {
        tcnt = tmin - output_width_u.b.low;
        output_width_u.b.low = tmin;
        /* --output_width_u.b.high; */
    }
    return tcnt;
}

__interrupt void timer0_compa(void) {
    TIMSK_OCIE0A = 0;
    switch (output_stage) {
    case 0:
        period_current = period - channels[0].output.width - channels[1].output.width;
        channels[0].output.width_current = channels[0].output.width;
        channels[1].output.width_current = channels[1].output.width;
        output_width_u.v = channels[0].output.width_current;
        break;
    case 1:
        output_width_u.v = channels[1].output.width_current;
        break;
    case 2:
        output_width_u.v = period_current;
        break;
    }
    TCNT0 = adjust_output_width();
}

__interrupt void pcint(void) {
    u8_t tof = TIFR_TOV1;
    u8_t timer = TCNT1;
    if (tof) {
        input_timer += 256;
        TIFR_TOV1 = 1;
    }
    input_timer += timer;
    u8_t new_pinb = PINB;
    u8_t old_pinb = pinb;
    pinb = new_pinb;
    if (old_pinb & (1<<CH0BIT)) {
        /* was high */
        if (!(new_pinb & (1<<CH0BIT))) {
            /* falling edge */
            ++channels[0].mailbox.flag;
            channels[0].mailbox.value = input_timer;
            channels[0].mailbox.not_empty = 1;
            ++channels[0].mailbox.flag;
        }
    } else {
        if (new_pinb & (1<<CH0BIT)) {
            /* rising edge */
            TCNT1 = 0;
            input_timer = 0;
            input_in_sync = 1;
        }
    }

    if (old_pinb & (1<<CH1BIT)) {
        /* was high */
        if (!(new_pinb & (1<<CH1BIT))) {
            /* falling edge */
            if (!input_in_sync)
                return;
            u16_t width = input_timer - channels[1].input.timer;
            ++channels[1].mailbox.flag;
            channels[1].mailbox.value = width;
            channels[1].mailbox.not_empty = 1;
            ++channels[1].mailbox.flag;
        }
    } else {
        if (new_pinb & (1<<CH1BIT)) {
            /* rising edge */
            channels[1].input.timer = input_timer;
        }
    }

}
