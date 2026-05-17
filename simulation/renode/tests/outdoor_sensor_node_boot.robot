*** Settings ***
Library             Process
Resource            ${RENODEKEYWORDS}

*** Variables ***
${RESC}             ${CURDIR}/../wio_e5_mini/wio_e5_mini.resc
${UART}             sysbus.usart1
${UART_TIMEOUT}     60
${ELF}              ${CURDIR}/../../../build/renode/outdoor_sensor_node/zephyr/zephyr.elf

*** Test Cases ***
Outdoor Sensor Node Boots And Reaches Shell Prompt
    [Documentation]    Load the outdoor_sensor_node firmware and verify it boots to the Zephyr shell.
    [Tags]    smoke    boot    outdoor
    Execute Command         include @${RESC}
    Create Terminal Tester  ${UART}    defaultPauseEmulation=True
    Write Char Delay        0.01
    Execute Command         sysbus LoadELF @${ELF}
    Execute Command         cpu0 VectorTableOffset `sysbus GetSymbolAddress "_vector_table"`
    Execute Command         cpu0 EnableZephyrMode
    Start Emulation
    Wait For Line On Uart   *** Booting Zephyr OS    timeout=${UART_TIMEOUT}
    Wait For Prompt On Uart uart:~$    timeout=${UART_TIMEOUT}

Shell Responds After Boot
    [Documentation]    Verify the shell responds on the outdoor sensor node.
    [Tags]    smoke    shell    outdoor
    Execute Command         include @${RESC}
    Create Terminal Tester  ${UART}    defaultPauseEmulation=True
    Write Char Delay        0.01
    Execute Command         sysbus LoadELF @${ELF}
    Execute Command         cpu0 VectorTableOffset `sysbus GetSymbolAddress "_vector_table"`
    Execute Command         cpu0 EnableZephyrMode
    Start Emulation
    Wait For Prompt On Uart uart:~$    timeout=${UART_TIMEOUT}
    Write Line To Uart      help
    Wait For Line On Uart   kernel    timeout=10

Outdoor Node Does Not Crash After Boot
    [Documentation]    Let the outdoor sensor node run for 5 seconds after boot.
    [Tags]    stability    outdoor
    Execute Command         include @${RESC}
    Create Terminal Tester  ${UART}    defaultPauseEmulation=True
    Write Char Delay        0.01
    Execute Command         sysbus LoadELF @${ELF}
    Execute Command         cpu0 VectorTableOffset `sysbus GetSymbolAddress "_vector_table"`
    Execute Command         cpu0 EnableZephyrMode
    Start Emulation
    Wait For Prompt On Uart uart:~$    timeout=${UART_TIMEOUT}
    Execute Command         sleep 5
    Write Line To Uart      kernel uptime
    Wait For Prompt On Uart uart:~$    timeout=10
