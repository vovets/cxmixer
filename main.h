#ifndef main_h
#define main_h

#include "types.h"

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

enum timer0_clock_t {
    timer0_off = 0,
    timer0_1_8 = (1 << CS01)
};

enum timer1_clock_t {
    timer1_off = 0,
    timer1_1_8 = (1 << CS12),
    timer1_1_4096 = (1 << CS13) | (1 << CS12) | (1 << CS10)
};

enum {
    default_channel_value = 1500,
    calibration_periods = 300,
    checksum_seed = 1
};

struct calibration_data_t {
    struct channel_t {
        u16_t min;
        u16_t max;
        u16_t mid;
    } channels[2];
    u16_t throttle_low;
    u16_t throttle_high;
};

#endif
