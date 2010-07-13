#include <ioavr.h>
#include <intrinsics.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "queue.h"

void input_event_unpack(struct input_event_t* e, u16_t n) {
    union input_eventu_t u;
    u.n = n;
    e->type = (enum input_event_type_t)u.e.type;
    e->input[0] = u.e.ch0;
    e->input[1] = u.e.ch1;
    e->timer = u.e.timer;
    e->tof = u.e.tof;
}

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
    TCCR1_CS12 = 1;
    /* TCCR1_CS11 = 1; */
    /* TCCR1_CS10 = 1; */
}

static void setup_interrupts(void) {
    /* PCMSK |= (1<<CH0BIT); */
    PCMSK |= (1<<CH0BIT)+(1<<CH1BIT);
    GIMSK_PCIE = 1;
    TIMSK_TOIE1 = 1;
}

static void state_init(void);
static void process_input_event(const struct input_event_t* e);
static void process_input_event_pc(const struct input_event_t* e);
static void process_input_event_tof(const struct input_event_t* e);
static void on_channel_value(u8_t channel, u16_t width);
static void on_filtered_value(u8_t channel, u16_t width);

//global variables
static struct input_queue_t input_queue;
static struct channel_queue_t channel_queue[2];
static struct filtered_queue_t filtered_queue[2];
static struct state_t state;
static input_event_handler_t input_event_handlers[] = {
    &process_input_event_pc,
    &process_input_event_tof
};

void main(void) {
    setup_io();
    setup_timer1();
    setup_interrupts();
    state_init();
    __enable_interrupt();
    while (1) {
        u8_t value_read = 0;
        u16_t value;
        QUEUE_GET(input, input_queue, value, value_read);
        if (value_read) {
            struct input_event_t e;
            input_event_unpack(&e, value);
            process_input_event(&e);
        }
    }
}

#pragma vector=TIM1_OVF_vect
__interrupt void timer1_ovf(void);

#pragma vector=PCINT0_vect
__interrupt void pcint(void);

__interrupt void timer1_ovf(void) {
    QUEUE_PUT(input, input_queue, 1);
}

__interrupt void pcint(void) {
    u8_t tof = TIFR_TOV1;
    u16_t e = TCNT1;
    union {
        u8_t input;
        struct {
            u8_t bit0:1;
            u8_t bit1:1;
        } bits;
    };
    input = PINB;
    bits.bit0 = 0;
    bits.bit1 = tof;
    e <<= 8;
    e |= input;
    QUEUE_PUT(input, input_queue, e);
}

static void process_input_event(const struct input_event_t* e) {
    input_event_handlers[e->type](e);
}

static void on_rising_edge(u8_t channel, const struct input_event_t* e) {
    state.channels[channel].timer = e->timer;
    state.channels[channel].timer_cycles = (e->tof || !e->timer) ? -1 : 0;
    state.channels[channel].input = 1;
}

static void on_fallig_edge(u8_t channel, const struct input_event_t* e) {
    i8_t tc = state.channels[channel].timer_cycles;
    if (e->tof || !e->timer)
        ++tc;
    u16_t width = tc;
    width <<= 8;
    width += e->timer;
    width -= state.channels[channel].timer;
    state.channels[channel].input = 0;
    on_channel_value(channel, width);
}

static void process_input_event_pc_channel(u8_t channel, const struct input_event_t* e) {
    if (state.channels[channel].input) {
        if (!e->input[channel])
            on_fallig_edge(channel, e);
    } else {
        if (e->input[channel])
            on_rising_edge(channel, e);
    }
}

static void process_input_event_pc(const struct input_event_t* e) {
    process_input_event_pc_channel(0, e);
    process_input_event_pc_channel(1, e);
}

static void process_input_event_tof_channel(u8_t channel) {
    if (state.channels[channel].input)
        ++state.channels[channel].timer_cycles;
}

static void process_input_event_tof(const struct input_event_t* e) {
    process_input_event_tof_channel(0);
    process_input_event_tof_channel(1);
}

static void state_init() {
    ZERO(state);
    state.calibration = is_jumper_installed();
}

static void on_channel_value(u8_t channel, u16_t width) {
    if (!QUEUE_FULL(channel, channel_queue[channel])) {
        state.channels[channel].width_sum += width;
        QUEUE_PUT(channel, channel_queue[channel], width);
        return;
    }
    u16_t filtered = state.channels[channel].width_sum >> 4;
    u8_t value_read;
    u16_t value;
    QUEUE_GET(channel, channel_queue[channel], value, value_read);
    state.channels[channel].width_sum -= value;
    state.channels[channel].width_sum += width;
    QUEUE_PUT(channel, channel_queue[channel], width);
    on_filtered_value(channel, filtered);
}

static void on_filtered_value(u8_t channel, u16_t width) {
    if (QUEUE_FULL(filtered, filtered_queue[channel])) {
        u8_t value_read;
        u16_t value;
        QUEUE_GET(filtered, filtered_queue[channel], value, value_read);
    }
    QUEUE_PUT(filtered, filtered_queue[channel], width);
}
