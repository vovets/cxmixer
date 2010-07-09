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
    PCMSK |= (1<<CH0BIT);
    /* PCMSK |= (1<<CH0BIT)+(1<<CH1BIT); */
    GIMSK_PCIE = 1;
    TIMSK_TOIE1 = 1;
}

static void state_init(void);
static void process_input_event(const struct input_event_t* e);
static void process_input_event_pc(const struct input_event_t* e);
static void process_input_event_tof(const struct input_event_t* e);
static void on_channel_value(u8_t channel, u16_t width);
static void on_channel_value_calibration(u8_t channel, u16_t width);
static void on_channel_value_normal(u8_t channel, u16_t width);

//global variables
QUEUE_DEF(input, 16, INPUT_QUEUE_SIZE)
QUEUE_DEF(channel, 16, 32)
static struct state_t state;
static input_event_handler_t input_event_handlers[] = {
    &process_input_event_pc,
    &process_input_event_tof
};

void main(void) {
    setup_io();
    setup_timer1();
    setup_interrupts();
    input_queue_init();
    channel_queue_init();
    state_init();
    __enable_interrupt();
    while (1) {
        while (input_queue_not_empty_monitor()) {
            struct input_event_t e;
            input_event_unpack(&e, input_queue_get_monitor());
            process_input_event(&e);
        }
    }
}

#pragma vector=TIM1_OVF_vect
__interrupt void timer1_ovf(void);

#pragma vector=PCINT0_vect
__interrupt void pcint(void);

__interrupt void timer1_ovf(void) {
    u16_t e = 1;
    input_queue_put(e);
}

__interrupt void pcint(void) {
    u16_t e = TCNT1;
    u8_t input = PINB;
    e <<= 8;
    input <<= 1;
    e |= input;
    input_queue_put(e);
}

static void process_input_event(const struct input_event_t* e) {
    input_event_handlers[e->type](e);
}

static void on_rising_edge(u8_t channel, u8_t timer_value) {
    state.channels[channel].timer = timer_value;
    state.channels[channel].timer_cycles = 0;
    state.channels[channel].input = 1;
}

static void on_fallig_edge(u8_t channel, u8_t timer_value) {
    u16_t width = state.channels[channel].timer_cycles;
    if (!timer_value)
	++width;
    if (!state.channels[channel].timer)
	--width;
    width <<= 8;
    width += timer_value;
    width -= state.channels[channel].timer;
    state.channels[channel].input = 0;
    on_channel_value(channel, width);
}

static void process_input_event_pc_channel(u8_t channel, const struct input_event_t* e) {
    if (state.channels[channel].input) {
        if (!e->input[channel])
            on_fallig_edge(channel, e->timer);
    } else {
        if (e->input[channel])
            on_rising_edge(channel, e->timer);
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
    if (state.calibration)
	on_channel_value_calibration(channel, width);
    else
	on_channel_value_normal(channel, width);
}

static void on_channel_value_calibration(u8_t channel, u16_t width) {
    if (channel != state.calibration_channel)
	return;
    if (width == state.calibration_width) {
	++state.calibration_count;
	if (state.calibration_count > 50)
	    LEDPORT = 1;
    } else {
	state.calibration_width = width;
	state.calibration_count = 0;
    }
}

static void on_channel_value_normal(u8_t channel, u16_t width) {
    if (!channel) {
	if (!channel_queue_not_full())
	    channel_queue_get();
	channel_queue_put(width);
    }
}
