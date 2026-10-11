*** Comments ***
Renode tests of the real firmware (build/Termostat.elf) on an emulated board:
STM32F030 + HD44780 8x2 LCD + DS18B20 on 1-Wire + relay/buzzer outputs + buttons.
Run from the repository root after `make`:  make renode-test

*** Settings ***
Suite Setup         Setup
Suite Teardown      Teardown
Test Setup          Create Thermostat
Test Teardown       Test Teardown
Resource            ${RENODEKEYWORDS}

*** Variables ***
${RESC}             ${CURDIR}/termostat.resc
${ELF}              ${CURDIR}/../build/Termostat.elf
${STEP}             0.1
${BTN_UP}           2
${BTN_DOWN}         3
${BTN_ENTER}        9
${SECOND_SLOT}      0x08003C50

*** Keywords ***
Create Thermostat
    Reset Emulation
    Execute Command             $elf=@${ELF}
    Execute Command             include @${RESC}

Run For
    [Arguments]                 ${seconds}
    Execute Command             emulation RunFor "${seconds}"

LCD Line
    [Arguments]                 ${row}
    ${line}=                    Execute Command    sysbus.gpioPortA.lcd Line ${row}
    ${line}=                    Evaluate           $line.rstrip('\\r\\n')
    RETURN                      ${line}

LCD Screen
    ${screen}=                  Execute Command    sysbus.gpioPortA.lcd Screen
    ${screen}=                  Evaluate           $screen.rstrip('\\r\\n')
    RETURN                      ${screen}

Relay State
    ${state}=                   Execute Command    sysbus.gpioPortF.relay State
    ${state}=                   Evaluate           $state.strip() == 'True'
    RETURN                      ${state}

Wait Until LCD Line Starts With
    [Arguments]                 ${row}    ${text}    ${timeout}=5
    ${steps}=                   Evaluate           int(float(${timeout}) / ${STEP})
    FOR    ${i}    IN RANGE    ${steps}
        ${line}=                LCD Line           ${row}
        IF    $line.startswith($text)    RETURN
        Run For                 ${STEP}
    END
    ${screen}=                  LCD Screen
    Fail                        LCD line ${row} does not start with '${text}' after ${timeout} s, screen: '${screen}'

Wait Until LCD Shows
    [Arguments]                 ${text}    ${timeout}=5
    ${steps}=                   Evaluate           int(float(${timeout}) / ${STEP})
    FOR    ${i}    IN RANGE    ${steps}
        ${screen}=              LCD Screen
        IF    $text in $screen    RETURN
        Run For                 ${STEP}
    END
    Fail                        LCD does not show '${text}' after ${timeout} s, screen: '${screen}'

Wait Until Relay Is
    [Arguments]                 ${expected}    ${timeout}=5
    ${steps}=                   Evaluate           int(float(${timeout}) / ${STEP})
    FOR    ${i}    IN RANGE    ${steps}
        ${state}=               Relay State
        IF    ${state} == ${expected}    RETURN
        Run For                 ${STEP}
    END
    ${screen}=                  LCD Screen
    Fail                        Relay is not ${expected} after ${timeout} s, screen: '${screen}'

Relay Should Stay
    [Arguments]                 ${expected}    ${duration}
    ${steps}=                   Evaluate           int(float(${duration}) / ${STEP})
    FOR    ${i}    IN RANGE    ${steps}
        ${state}=               Relay State
        Should Be Equal         ${state}    ${expected}    Relay changed at step ${i}
        Run For                 ${STEP}
    END

Press Button
    [Arguments]                 ${pin}
    Execute Command             sysbus.gpioPortA OnGPIO ${pin} false
    Run For                     0.15
    Execute Command             sysbus.gpioPortA OnGPIO ${pin} true
    Run For                     0.15

Set Sensor Temperature
    [Arguments]                 ${celsius}
    Execute Command             sysbus.gpioPortB.ds18b20 SetTemperature 0 ${celsius}

*** Test Cases ***
Should Show Measured Temperature And Setpoint
    Run For                     0.3
    ${state}=                   Relay State
    Should Not Be True          ${state}    Relay must be off right after reset
    Wait Until LCD Line Starts With    0    ${SPACE}22.5 C    timeout=3
    Wait Until LCD Line Starts With    1    25.0              timeout=1

Should Switch Heating On Below Setpoint
    Wait Until Relay Is         ${True}    timeout=5
    Wait Until LCD Line Starts With    0    ${SPACE}22.5 C*    timeout=1
    Wait Until LCD Line Starts With    1    25.0 ON            timeout=1

Should Switch Heating Off At Setpoint And Keep Compressor Protection
    Wait Until Relay Is         ${True}    timeout=5
    Set Sensor Temperature      35.0
    Wait Until Relay Is         ${False}    timeout=5
    Wait Until LCD Line Starts With    1    25.0 OFF    timeout=1
    # Cold again, but the relay must stay off for the 3 min protection time
    Set Sensor Temperature      15.0
    Wait Until LCD Line Starts With    0    ${SPACE}1    timeout=8
    Relay Should Stay           ${False}    3

Should Report Sensor Error And Recover
    Wait Until Relay Is         ${True}    timeout=5
    Execute Command             sysbus.gpioPortB.ds18b20 Disconnect 0
    Wait Until LCD Line Starts With    0    Sens.Err    timeout=10
    ${state}=                   Relay State
    Should Not Be True          ${state}    Relay must be off on a sensor error
    Execute Command             sysbus.gpioPortB.ds18b20 Connect 0
    Wait Until LCD Line Starts With    0    ${SPACE}22.5    timeout=8

Should Change Setpoint With Buttons
    Wait Until LCD Line Starts With    0    ${SPACE}22.5    timeout=3
    Press Button                ${BTN_UP}
    Wait Until LCD Line Starts With    1    25.5    timeout=1
    Press Button                ${BTN_DOWN}
    Press Button                ${BTN_DOWN}
    Wait Until LCD Line Starts With    1    24.5    timeout=1

Should Open Menu With Enter
    Wait Until LCD Line Starts With    0    ${SPACE}22.5    timeout=3
    Press Button                ${BTN_ENTER}
    Wait Until LCD Shows        Basic    timeout=1

Should Save Setpoint To Flash
    Wait Until LCD Line Starts With    0    ${SPACE}22.5    timeout=3
    # The defaults are written to the first 80-byte slot at the first start, the next slot is still erased
    ${slot}=                    Execute Command    sysbus ReadDoubleWord ${SECOND_SLOT}
    Should Contain              ${slot}    0xFFFFFFFF
    Press Button                ${BTN_UP}
    Press Button                ${BTN_UP}
    Wait Until LCD Line Starts With    1    26.0    timeout=1
    # Auto-save runs 10 s after the last key press
    Run For                     11
    ${slot}=                    Execute Command    sysbus ReadDoubleWord ${SECOND_SLOT}
    Should Not Contain          ${slot}    0xFFFFFFFF
    # Reboot the MCU: the setpoint is loaded from Flash
    Execute Command             sysbus.cpu Reset
    Wait Until LCD Line Starts With    1    26.0    timeout=3
