#ifndef main_h
#define main_h

#include "types.h"
#include "queue.h"

#define CH0PIN PINB_PINB2
#define CH0BIT PINB2
#define CH1PIN PINB_PINB3
#define CH1BIT PINB3
#define LEDPORT PORTB_PORTB4

#define ZERO(A) memset(&A, 0, sizeof(A))

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

enum {
    channel_queue_size = 4,
    calibration_cycles = 100
};

typedef u16_t channel_queue_value_t;

QUEUE_TYPE(channel);

struct input_t {
    u8_t timer;
    i8_t timer_cycles;
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
    struct mailbox_t mailbox;
    struct stats_t stats;
};

typedef void (*on_value_t)(u8_t channel, u16_t value);

#endif
