#ifndef main_h
#define main_h

#include "types.h"
#include "queue.h"

#define CH0PIN PINB_PINB2
#define CH0PORT PORTB_PORTB0
#define CH0BIT PINB2
#define CH1PIN PINB_PINB3
#define CH1PORT PORTB_PORTB1
#define CH1BIT PINB3
#define LEDPORT PORTB_PORTB4

#define ZERO(A) memset(&A, 0, sizeof(A))

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

enum {
    channel_queue_size = 4,
    clock = 8000000,
    timer_clock = clock / 8,
    calibration_seconds = 1,
    calibration_periods_count = 10,
    calibration_period_channel = 1,
    default_channel_value = 1500,
    default_period_value = 20000
};

typedef u16_t channel_queue_value_t;

QUEUE_TYPE(channel);

struct input_t {
    u8_t timer;
    i8_t timer_cycles;
};

struct output_t {
    u16_t timer;
    u16_t timer_left;
};

struct mailbox_t {
    u8_t not_empty;
    u8_t flag;
    u16_t value;
};

struct stats_t {
    u16_t width_sum;
    u16_t min;
    u16_t max;
};

struct channel_state_t {
    struct input_t input;
    struct output_t output;
    struct mailbox_t mailbox;
    struct stats_t stats;
};

struct eeprom_t {
    u8_t osccal;
    u16_t period;
    struct channel_t {
        u16_t min;
        u16_t max;
    } channels[2];
};
    

typedef void (*on_value_t)(u8_t channel, u16_t value);

#endif
