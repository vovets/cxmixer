#include "types.h"

#define CH0PIN PINB_PINB2
#define CH0BIT PINB2
#define CH1PIN PINB_PINB3
#define CH1BIT PINB3
#define LEDPORT PORTB_PORTB4

#define INPUT_QUEUE_SIZE 16
#define ZERO(A) memset(&A, 0, sizeof(A))

enum input_event_type_t {
    ET_PC = 0,
    ET_TOF = 1
};

struct input_event_t {
    enum input_event_type_t type;
    u8_t input[2];
    u8_t timer;
};

union input_eventu_t {
    u16_t n;
    struct {
        u8_t type:1;
        u8_t ch0:1;
        u8_t ch1:1;
        u8_t timer;
    } e;
};

u16_t input_event_pack(const struct input_event_t* e);
void input_event_unpack(struct input_event_t* e, u16_t n);

struct channel_state_t {
    u8_t input;
    u8_t timer;
    u8_t timer_cycles;
};

struct state_t {
    struct channel_state_t channels[2];
    u8_t calibration;
    u8_t calibration_channel;
    u16_t calibration_width;
    u8_t calibration_count;
};

typedef void (*input_event_handler_t)(const struct input_event_t*);
