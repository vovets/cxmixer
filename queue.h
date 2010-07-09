#ifndef queue_h
#define queue_h

#include "types.h"

#define CONCAT1(x,y) x ## y
#define CONCAT(x,y)  CONCAT1(x,y)

#define QUEUE_BNAME(N) CONCAT(N, _queue)
#define QUEUE_TNAME(N) struct CONCAT(QUEUE_BNAME(N), _t)
#define QUEUE_FNAME(N, F) CONCAT(QUEUE_BNAME(N), _##F)
#define QUEUE_DNAME(T) u##T##_t

#define QUEUE_DEF(N, T, S)                                              \
    QUEUE_TNAME(N) {                                                    \
        QUEUE_DNAME(T) buffer[S];                                       \
        u8_t read_index;                                                \
        u8_t write_index;                                               \
    };                                                                  \
                                                                        \
    static QUEUE_TNAME(N) QUEUE_BNAME(N);                               \
                                                                        \
    static u8_t QUEUE_FNAME(N, next_index)(u8_t index);                 \
    static u8_t QUEUE_FNAME(N, not_empty)(void);                        \
    static u8_t QUEUE_FNAME(N, not_full)(void);                         \
    static void QUEUE_FNAME(N, put)(QUEUE_DNAME(T) e);                  \
    static QUEUE_DNAME(T) QUEUE_FNAME(N, get)(void);                    \
    __monitor static u8_t QUEUE_FNAME(N, not_empty_monitor)(void);      \
    __monitor static QUEUE_DNAME(T) QUEUE_FNAME(N, get_monitor)(void);	\
    static void QUEUE_FNAME(N, init)(void);                             \
                                                                        \
    static u8_t QUEUE_FNAME(N, next_index)(u8_t index) {                \
        return (index + 1) & (S - 1);                                   \
    }                                                                   \
                                                                        \
    static u8_t QUEUE_FNAME(N, not_empty)(void) {                       \
        return QUEUE_BNAME(N).read_index != QUEUE_BNAME(N).write_index;	\
    }                                                                   \
                                                                        \
    __monitor static u8_t QUEUE_FNAME(N, not_empty_monitor)(void) {     \
        return QUEUE_FNAME(N, not_empty)();                             \
    }                                                                   \
                                                                        \
    static u8_t QUEUE_FNAME(N, not_full)(void) {                        \
        return QUEUE_FNAME(N, next_index)(QUEUE_BNAME(N).write_index) != QUEUE_BNAME(N).read_index; \
    }                                                                   \
                                                                        \
    static void QUEUE_FNAME(N, put)(QUEUE_DNAME(T) e) {                 \
        if (QUEUE_FNAME(N, not_full)()) {                               \
            QUEUE_BNAME(N).buffer[QUEUE_BNAME(N).write_index] = e;		\
            QUEUE_BNAME(N).write_index = QUEUE_FNAME(N, next_index)(QUEUE_BNAME(N).write_index); \
        }                                                               \
    }                                                                   \
                                                                        \
    static QUEUE_DNAME(T) QUEUE_FNAME(N, get)() {                       \
        u8_t read_index = QUEUE_BNAME(N).read_index;                    \
        if (QUEUE_FNAME(N, not_empty)())                                \
            QUEUE_BNAME(N).read_index = QUEUE_FNAME(N, next_index)(read_index); \
        return QUEUE_BNAME(N).buffer[read_index];                       \
    }                                                                   \
                                                                        \
    __monitor static QUEUE_DNAME(T) QUEUE_FNAME(N, get_monitor)() {     \
        return QUEUE_FNAME(N, get)();                                   \
    }                                                                   \
                                                                        \
    static void QUEUE_FNAME(N, init)(void) {                            \
        QUEUE_BNAME(N).read_index = 0;                                  \
        QUEUE_BNAME(N).write_index = 0;                                 \
    }


#endif
