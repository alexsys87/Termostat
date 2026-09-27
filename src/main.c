// Thermostat firmware for STM32F030F4P6: DS18B20 sensors, 8x2 HD44780 LCD, three buttons, relay output

#include "board.h"
#include "ds18b20.h"
#include "thermostat.h"
#include "ui.h"

int main(void) {
    board_clock_init();

    // SysTick MUST be initialized first: delay_us() used by the LCD and 1-Wire depends on it
    board_systick_init();
    board_watchdog_init();

    // Settings are loaded before GPIO: the relay logic (NO/NC) defines the safe pin level
    thermostat_init();
    ui_sanitize_settings();

    board_gpio_init(thermo.cfg.relay_logic != 0);
    board_beeper_init();
    ds18b20_init();
    ui_init();
    thermostat_start();

    beep(200);

    while (1) {
        board_watchdog_feed();
        thermostat_task();
        ui_task();

        if (thermo.cfg.power_save) board_sleep();
    }
}
