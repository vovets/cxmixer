#ifndef queue_h
#define queue_h

#include "types.h"

#define CONCAT1(x,y) x ## y
#define CONCAT(x,y)  CONCAT1(x,y)

#define QUEUEB(A) CONCAT(A, _queue)
#define QUEUE_VALUE_TYPE(A) CONCAT(QUEUEB(A), _value_t)
#define QUEUE_SIZE(A) CONCAT(QUEUEB(A), _size)

#define QUEUE_TYPE(T)                                  \
    struct CONCAT(QUEUEB(T), _t) {                     \
        QUEUE_VALUE_TYPE(T) buf[QUEUE_SIZE(T)];        \
        u8_t read_index;                               \
        u8_t write_index;                              \
    }

#define QUEUE_NEXT(T, V) ((V + 1) & (QUEUE_SIZE(T) - 1))

#define QUEUE_PUT(T, N, V)                                              \
    do {                                                                \
        u8_t tmp = QUEUE_NEXT(T, N.write_index);                        \
        if (tmp != N.read_index) {                                      \
            N.buf[N.write_index] = V;                                   \
            N.write_index = tmp;                                        \
        }                                                               \
    } while (0)

#define QUEUE_PUT_UNSAFE(T, N, V)                                       \
    do {                                                                \
        u8_t tmp = QUEUE_NEXT(T, N.write_index);                        \
        N.buf[N.write_index] = V;                                       \
        N.write_index = tmp;                                            \
    } while (0)

#define QUEUE_GET(T, N, V, B)                    \
    do {                                         \
        u8_t tmp = N.read_index;                 \
        if (N.write_index != tmp) {              \
            V = N.buf[tmp];                      \
            N.read_index = QUEUE_NEXT(T, tmp);   \
            B = 1;                               \
        }                                        \
    } while (0)

#define QUEUE_FULL(T, N) (QUEUE_NEXT(T, N.write_index) == N.read_index)

#endif
