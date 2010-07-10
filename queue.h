#ifndef queue_h
#define queue_h

#include "types.h"

#define CONCAT1(x,y) x ## y
#define CONCAT(x,y)  CONCAT1(x,y)

#define QUEUEB(N) CONCAT(N, _queue)
#define QUEUE_BUF(N) CONCAT(QUEUEB(N), _buffer)
#define QUEUE_VALUE_TYPE(N) CONCAT(QUEUEB(N), _value_t)
#define QUEUE_SIZE(N) CONCAT(QUEUEB(N), _size)
#define QUEUE_RI(N) CONCAT(QUEUEB(N), _read_index)
#define QUEUE_WI(N) CONCAT(QUEUEB(N), _write_index)

#define QUEUE_VARS(N)                                  \
    QUEUE_VALUE_TYPE(N) QUEUE_BUF(N)[QUEUE_SIZE(N)];   \
    u8_t QUEUE_RI(N);                                  \
    u8_t QUEUE_WI(N);

#define QUEUE_NEXT(N, V) ((V + 1) & (QUEUE_SIZE(N) - 1))

#define QUEUE_PUT(N, V)                                                 \
    do {                                                                \
        u8_t tmp = QUEUE_NEXT(N, QUEUE_WI(N));                          \
        if (tmp != QUEUE_RI(N)) {                                       \
            QUEUE_BUF(N)[tmp] = V;                                      \
            QUEUE_WI(N) = tmp;                                          \
        }                                                               \
    } while (0)

#define QUEUE_GET(N, V, B)                      \
    do {                                        \
        u8_t tmp = QUEUE_RI(N);                 \
        if (QUEUE_WI(N) != tmp) {               \
            V = QUEUE_BUF(N)[tmp];              \
            QUEUE_RI(N) = QUEUE_NEXT(N, tmp);   \
            B = 1;                              \
        }                                       \
    } while (0)

#define QUEUE_FULL(N) (QUEUE_NEXT(N, QUEUE_WI(N)) == QUEUE_RI(N))

#endif
