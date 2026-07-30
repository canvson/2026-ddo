#ifndef UART_RING_MATH_H
#define UART_RING_MATH_H

#include <stdint.h>

static uint32_t UartRing_RetainedConsumer(uint32_t producer,
                                         uint32_t consumer,
                                         uint32_t capacity,
                                         uint8_t *overflowed)
{
    if ((uint32_t)(producer - consumer) > capacity) {
        if (overflowed != 0) {
            *overflowed = 1u;
        }
        return producer - capacity;
    }
    if (overflowed != 0) {
        *overflowed = 0u;
    }
    return consumer;
}

#endif
