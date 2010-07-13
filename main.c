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
static struct channel_queue_t channel_queue[2];
static struct filtered_queue_t filtered_queue[2];
static struct state_t state;
static input_event_handler_t input_event_handlers[] = {
    &process_input_event_pc,
    &process_input_event_tof
};

static u8_t input_queue[input_queue_size];
static u8_t input_queue_read_index;
static u8_t input_queue_write_index;

static void input_queue_put(u8_t v) {
    input_queue[input_queue_write_index] = v;
    ++input_queue_write_index;
    input_queue_write_index &= input_queue_size - 1;
}

struct input_queue_result_t {
    u8_t is_empty;
    u8_t value;
};

static struct input_queue_result_t input_queue_get(void) {
    struct input_queue_result_t r;
    r.is_empty = input_queue_write_index == input_queue_read_index;
    if (!r.is_empty) {
        r.value = input_queue[input_queue_read_index];
        ++input_queue_read_index;
        input_queue_read_index &= input_queue_size - 1;
    }
    return r;
}

static u8_t input_queue_get_event(struct input_event_t* e) {
    struct input_queue_result_t r = input_queue_get();
    if (r.is_empty)
        return 0;
    union pinb_t u;
    switch (r.value) {
    case ET_PC:
        e->type = ET_PC;
        r = input_queue_get();
        e->tof = r.value;
        r = input_queue_get();
        e->timer = r.value;
        r = input_queue_get();
        u.n = r.value;
        e->input[0] = u.b.ch0;
        e->input[1] = u.b.ch1;
        break;
    case ET_TOF:
        e->type = ET_TOF;
        break;
    }
    return 1;
}

void main(void) {
    setup_io();
    setup_timer1();
    setup_interrupts();
    state_init();
    __enable_interrupt();
    while (1) {
        struct input_event_t e;
        if (input_queue_get_event(&e)) {
            process_input_event(&e);
        }
    }
}

#pragma vector=TIM1_OVF_vect
__interrupt void timer1_ovf(void);

#pragma vector=PCINT0_vect
__interrupt void pcint(void);

__interrupt void timer1_ovf(void) {
    input_queue_put(ET_TOF);
}

__interrupt void pcint(void) {
    u8_t tof = TIFR_TOV1;
    u8_t tcnt = TCNT1;
    u8_t input = PINB;
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
    state.channels[0].min = 0xffffu;
    state.channels[1].min = 0xffffu;
}

static void on_channel_value(u8_t channel, u16_t width) {
    if (!QUEUE_FULL(channel, channel_queue[channel])) {
        state.channels[channel].width_sum += width;
        QUEUE_PUT(channel, channel_queue[channel], width);
        return;
    }
    u16_t filtered = (state.channels[channel].width_sum + width) / channel_queue_size;
    u8_t value_read;
    u16_t value;
    QUEUE_GET(channel, channel_queue[channel], value, value_read);
    state.channels[channel].min = MIN(state.channels[channel].min, value);
    state.channels[channel].max = MAX(state.channels[channel].max, value);
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
