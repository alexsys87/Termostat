// Host stub of the device header: only what hardware-independent modules need to compile
#ifndef STM32F0XX_H
#define STM32F0XX_H

#include <stdint.h>

typedef struct {
    volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2], BRR;
} GPIO_TypeDef;

#endif
