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
    input_queue_size = 16,
    channel_queue_size = 8,
    filtered_queue_size = 4
};

typedef u16_t channel_queue_value_t;
typedef u16_t filtered_queue_value_t;

QUEUE_TYPE(channel);
QUEUE_TYPE(filtered);

enum input_event_type_t {
    ET_PC = 0,
    ET_TOF = 1
};

struct input_event_t {
    enum input_event_type_t type;
    u8_t input[2];
    u8_t timer;
    u8_t tof;
};

union pinb_t {
    u8_t n;
    struct {
        u8_t pad01:2;
        u8_t ch0:1;
        u8_t ch1:1;
        u8_t pad47:4;
    } b;
};

struct channel_state_t {
    u8_t input;
    u8_t timer;
    i8_t timer_cycles;
    u16_t width_sum;
    u16_t min;
    u16_t max;
};

struct state_t {
    struct channel_state_t channels[2];
    u8_t calibration;
};

typedef void (*input_event_handler_t)(const struct input_event_t*);

#endif
