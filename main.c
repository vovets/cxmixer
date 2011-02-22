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

//global variables
static u8_t pinb;
static volatile u8_t output_completed;
static union {
    u16_t v;
    struct {
        u8_t l;
        u8_t h;
    } b;
} frame[4];
static volatile u8_t frame_bits;
u8_t timer1_hi;
static union {
    u16_t v;
    struct {
        u8_t l;
        u8_t h;
    } b;
} output_timer;
static u16_t channels[2] = { default_channel_value, default_channel_value };
struct calibration_data_t calibration_data;
static u16_t throttle_low_threshold;

__eeprom struct calibration_data_t calibration_data_eeprom;
__eeprom u8_t eeprom_checksum;

static void calibration_store(void);
static u8_t calibration_load(void);
static void calibrate(void);
static void capture_frame(void);
static void calculate_channels(void);
static void transform_channels(void);
static void produce_output(void);
static void setup_timer0(void);
static void wait_for_output_comletion(void);
static void produce_pulse(u8_t ch, u16_t width);
static void set_timer0_clock(enum timer0_clock_t c);
static void start_timer0(void);
static void stop_timer0(void);
static void set_timer1_clock(enum timer1_clock_t c);
static void start_timer1(void);
static void stop_timer1(void);
static void wait_on_timer1(void);

void main(void) {
    u8_t calibration = is_jumper_installed();
    setup_io();
    __enable_interrupt();
    if (calibration) {
        calibrate();
        calibration_store();
    } else {
        if (calibration_load()) {
            throttle_low_threshold = calibration_data.channels[0].min
                + (calibration_data.channels[0].max - calibration_data.channels[0].min) / 16;
            while(1) {
                capture_frame();
                calculate_channels();
                transform_channels();
                produce_output();
            }
        }
    }
    LEDPORT = 1;
}

static u8_t calculate_checksum(void) {
    u8_t checksum = checksum_seed;
    for (u8_t* byte = (u8_t*)&calibration_data;
         byte < (u8_t*)&calibration_data + sizeof(calibration_data);
         ++byte) {
        checksum += *byte;
    }
    return checksum;
}

static void calibration_store(void) {
    calibration_data_eeprom = calibration_data;
    eeprom_checksum = calculate_checksum();
}

static u8_t calibration_load(void) {
    calibration_data = calibration_data_eeprom;
    if (calculate_checksum() != eeprom_checksum)
        return 0;
    return 1;
}

#pragma optimize=s none
static void wait_on_timer1(void) {
    set_timer1_clock(timer1_1_4096);
    timer1_hi = 0;
    start_timer1();
    while (timer1_hi < 9)
        ;
    stop_timer1();
}

#pragma optimize=s none
void calibrate(void) {
    calibration_data.channels[0].min = 0xffff;
    calibration_data.channels[1].min = 0xffff;

    capture_frame();
    LEDPORT = 1;
    for (u16_t i = 0; i < calibration_periods; ++i) {
        capture_frame();
        calculate_channels();
        for (u8_t ch = 0; ch < 2; ++ch) {
            if (channels[ch] > calibration_data.channels[ch].max)
                calibration_data.channels[ch].max = channels[ch];
            if (channels[ch] < calibration_data.channels[ch].min)
                calibration_data.channels[ch].min = channels[ch];
        }
    }
    LEDPORT = 0;

    wait_on_timer1();

    LEDPORT = 1;
    for (u8_t i = 0; i < 255; ++i) {
        capture_frame();
        calculate_channels();
        for (u8_t ch = 0; ch < 2; ++ch) {
            calibration_data.channels[ch].mid = channels[ch];
        }
    }
    LEDPORT = 0;
 
    wait_on_timer1();
}

#pragma optimize=s none
void capture_frame(void) {
    timer1_hi = 0;
    pinb = 0;
    TIFR_TOV1 = 1;
    frame_bits = 0;
    PCMSK = (1<<CH0BIT) | (1<<CH1BIT);
    GIFR_PCIF = 1;
    set_timer1_clock(timer1_1_8);
    start_timer1();
    GIMSK_PCIE = 1;
    while(!(frame_bits == 0x0F))
        ;
    stop_timer1();
}

void calculate_channels(void) {
    channels[0] = frame[1].v - frame[0].v;
    channels[1] = frame[3].v - frame[2].v;
}

void transform_channels(void) {
    u16_t ch0 = channels[0];
    i16_t ch1 = channels[1];

    u16_t ch0_;
    u16_t ch1_;

    if (ch0 < throttle_low_threshold) {
        ch0_ = ch0;
        ch1_ = ch0;
    } else {
        ch1 -= calibration_data.channels[1].mid;
        ch1 /= 2;
        ch0_ = ch0 + ch1;
        ch1_ = ch0 - ch1;
    }

    if (ch0_ > calibration_data.channels[0].max)
        ch0_ = calibration_data.channels[0].max;
    if (ch0_ < calibration_data.channels[0].min)
        ch0_ = calibration_data.channels[0].min;
    
    if (ch1_ > calibration_data.channels[0].max)
        ch1_ = calibration_data.channels[0].max;
    if (ch1_ < calibration_data.channels[0].min)
        ch1_ = calibration_data.channels[0].min;

    channels[0] = ch0_;
    channels[1] = ch1_;
}

static void produce_output() {
    produce_pulse(0, channels[0]);
    produce_pulse(1, channels[1]);
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
    set_timer0_clock(timer0_1_8);
    start_timer0();
    wait_for_output_comletion();
}

static void set_timer0_clock(enum timer0_clock_t c) {
    u8_t tmp = TCCR0B & 0xf8;
    tmp |= c;
    TCCR0B = tmp;
}

static void start_timer0() {
    GTCCR_TSM = 1;
    GTCCR_PSR0 = 1;
    TIFR_TOV0 = 1;
    TIMSK_TOIE0 = 1;
    GTCCR_TSM = 0;
}

static void stop_timer0() {
    set_timer0_clock(timer0_off);
}

static void set_timer1_clock(enum timer1_clock_t c) {
    u8_t tmp = TCCR1 & 0xf0;
    tmp |= c;
    TCCR1 = tmp;
}

static void start_timer1() {
    GTCCR_TSM = 1;
    GTCCR_PSR1 = 1;
    TIFR_TOV1 = 1;
    TIMSK_TOIE1 = 1;
    GTCCR_TSM = 0;
}

static void stop_timer1() {
    set_timer1_clock(timer1_off);
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
    ++timer1_hi;
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
        ++timer1_hi;
        TIFR_TOV1 = 1;
    }
    u8_t new_pinb = PINB;
    u8_t old_pinb = pinb;
    pinb = new_pinb;

    if (old_pinb & (1<<CH0BIT)) {
        /* was high */
        if (!(new_pinb & (1<<CH0BIT))) {
            /* falling edge */
            /* bit0 set */
            if (frame_bits & 0x01) {
                frame[1].b.h = timer1_hi;
                frame[1].b.l = timer;
                frame_bits |= 0x02;
            }
        }
    } else {
        if (new_pinb & (1<<CH0BIT)) {
            /* rising edge */
            /* bit0 reset */
            if ((frame_bits & 0x01) == 0x00) {
                frame[0].b.h = timer1_hi;
                frame[0].b.l = timer;
                frame_bits |= 0x01;
            }
        }
    }

    if (old_pinb & (1<<CH1BIT)) {
        /* was high */
        if (!(new_pinb & (1<<CH1BIT))) {
            /* falling edge */
            /* bit2 set */
            if (frame_bits & 0x04) {
                frame[3].b.h = timer1_hi;
                frame[3].b.l = timer;
                frame_bits |= 0x08;
            }
        }
    } else {
        if (new_pinb & (1<<CH1BIT)) {
            /* rising edge */
            /* bit0 set && bit2 reset */
            if ((frame_bits & 0x05) == 0x01) {
                frame[2].b.h = timer1_hi;
                frame[2].b.l = timer;
                frame_bits |= 0x04;
            }
        }
    }

    if (frame_bits == 0x0F) {
        PCMSK = 0;
        GIMSK_PCIE = 0;
        GIFR_PCIF = 0;
    }
}
