#include "board.h"

volatile uint32_t tick_count = 0;
static uint32_t ticks_per_us = 0;              // SysTick ticks in 1 us (set in board_systick_init)

#ifdef DEBUG_BUTTONS
volatile uint8_t virtual_buttons = 0;
#endif

// Non-blocking beep on the passive buzzer
static volatile uint8_t beep_active = 0;
static volatile uint16_t beep_half_periods = 0; // remaining half-periods (4 per ms at 2 kHz)

// ==================== GPIO helpers ====================

// Push-pull output, high speed
static void setup_gpio_output(GPIO_TypeDef* port, uint16_t pin_mask) {
    for (int i = 0; i < 16; i++) {
        if (pin_mask & (1 << i)) {
            port->MODER &= ~(0x3 << (i * 2));
            port->MODER |= (0x1 << (i * 2));
            port->OTYPER &= ~(1 << i);
            port->OSPEEDR |= (0x3 << (i * 2));
        }
    }
}

// Input with pull-up
static void setup_gpio_input_pullup(GPIO_TypeDef* port, uint16_t pin_mask) {
    for (int i = 0; i < 16; i++) {
        if (pin_mask & (1 << i)) {
            port->MODER &= ~(0x3 << (i * 2));
            port->PUPDR &= ~(0x3 << (i * 2));
            port->PUPDR |= (0x1 << (i * 2));
        }
    }
}

// ==================== Clock and time ====================

void board_clock_init(void) {
    // 1. Make sure HSI is running
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));

    // 2. Flash latency MUST be set before switching to 48 MHz (1 wait state + prefetch)
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;

    // 3. If PLL is already used, switch to HSI and stop the PLL to reconfigure it
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
        RCC->CFGR &= ~RCC_CFGR_SW;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
    }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY);

    // 4. PLL: HSI/2 * 12 = 48 MHz
    RCC->CFGR2 &= ~RCC_CFGR2_PREDIV1;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLMULL | RCC_CFGR_PLLSRC)) | RCC_CFGR_PLLSRC_HSI_DIV2 | RCC_CFGR_PLLMULL12;

    // 5. Start the PLL
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // 6. Use PLL as the system clock
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    SystemCoreClockUpdate();
}

void board_systick_init(void) {
    ticks_per_us = SystemCoreClock / 1000000;
    SysTick_Config(SystemCoreClock / 1000); // 1 ms interrupt
}

void SysTick_Handler(void) {
    tick_count++;
}

// Precise microsecond delay based on SysTick (required for 1-Wire on Cortex-M0).
// Works with interrupts disabled.
void delay_us(uint32_t us) {
    uint32_t ticks = us * ticks_per_us;
    uint32_t reload = SysTick->LOAD + 1;
    uint32_t start = SysTick->VAL;
    uint32_t elapsed = 0;

    while (elapsed < ticks) {
        uint32_t current = SysTick->VAL;
        // SysTick counts down and reloads from LOAD after reaching 0
        elapsed += (start >= current) ? (start - current) : (start + reload - current);
        start = current;
    }
}

void delay_ms(uint32_t ms) {
    while (ms--) delay_us(1000);
}

// ==================== GPIO ====================

void board_gpio_init(bool relay_idle_high) {
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOFEN;

    // Set the "relay off" level BEFORE the pin becomes an output:
    // with NC logic "off" is the high level, a low glitch would switch the load on
    board_relay_write(relay_idle_high);
    BEEPER_PORT->BRR = BEEPER_PIN;
    setup_gpio_output(RELAY_PORT, RELAY_PIN);
    setup_gpio_output(BEEPER_PORT, BEEPER_PIN);

    setup_gpio_input_pullup(BTN_PORT, BTN_UP_PIN | BTN_DOWN_PIN | BTN_ENTER_PIN);
}

void board_relay_write(bool high) {
    if (high) RELAY_PORT->BSRR = RELAY_PIN;
    else RELAY_PORT->BRR = RELAY_PIN;
}

uint8_t board_buttons_read(void) {
#ifdef DEBUG_BUTTONS
    return virtual_buttons;
#else
    uint32_t idr = BTN_PORT->IDR;   // buttons are active low
    uint8_t pressed = 0;
    if (!(idr & BTN_UP_PIN)) pressed |= BTN_UP;
    if (!(idr & BTN_DOWN_PIN)) pressed |= BTN_DOWN;
    if (!(idr & BTN_ENTER_PIN)) pressed |= BTN_ENTER;
    return pressed;
#endif
}

void board_sleep(void) {
    __WFI(); // SysTick wakes the core every 1 ms
}

// ==================== Watchdog ====================

void board_watchdog_init(void) {
#if USE_WATCHDOG
    // Freeze the watchdog while the core is halted by the debugger
    RCC->APB2ENR |= RCC_APB2ENR_DBGMCUEN;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    // LSI ~40 kHz / 32 = 1250 Hz, 2500 counts = ~2 s
    IWDG->KR = 0xCCCC;          // start
    IWDG->KR = 0x5555;          // unlock PR/RLR
    IWDG->PR = 3;               // prescaler /32
    IWDG->RLR = 2500;
    while (IWDG->SR);           // wait until the registers are updated
    IWDG->KR = 0xAAAA;          // reload
#endif
}

void board_watchdog_feed(void) {
#if USE_WATCHDOG
    IWDG->KR = 0xAAAA;
#endif
}

// ==================== Beeper ====================

/**
 * @brief TIM14 generates 4 kHz update interrupts, each toggles PF1 -> 2 kHz tone
 * @note  48 MHz / (PSC+1) / (ARR+1) = 48e6 / 48 / 250 = 4000 Hz
 */
void board_beeper_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;

    TIM14->PSC = 47;      // 1 MHz counter clock
    TIM14->ARR = 249;     // 250 us = one half-period of the 2 kHz tone
    TIM14->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM14_IRQn);
    // The timer is started only by board_beeper_start()
}

// Starts a non-blocking beep. A new beep is ignored while another one is playing.
void board_beeper_start(uint16_t duration_ms) {
    if (beep_active || duration_ms == 0) return;

    beep_half_periods = (duration_ms > 16000) ? 64000 : duration_ms * 4;
    beep_active = 1;

    TIM14->CNT = 0;
    TIM14->CR1 |= TIM_CR1_CEN;
}

void TIM14_IRQHandler(void) {
    if (TIM14->SR & TIM_SR_UIF) {
        TIM14->SR = (uint16_t)~TIM_SR_UIF;   // rc_w0: write 0 only to UIF

        if (beep_active && beep_half_periods) {
            BEEPER_PORT->ODR ^= BEEPER_PIN;
            if (--beep_half_periods == 0) {
                beep_active = 0;
                BEEPER_PORT->BRR = BEEPER_PIN;   // leave the buzzer pin low
                TIM14->CR1 &= ~TIM_CR1_CEN;      // stop the timer
            }
        } else {
            beep_active = 0;
            TIM14->CR1 &= ~TIM_CR1_CEN;
        }
    }
}
