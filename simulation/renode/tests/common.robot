*** Settings ***
Suite Setup         Setup
Suite Teardown      Teardown
Test Teardown       Test Teardown
Resource            ${RENODEKEYWORDS}
Library             Process

*** Variables ***
${RESC}             ${CURDIR}/../frdm_mcxn947.resc
${UART}             sysbus.flexcomm4lpuart4
${UART_TIMEOUT}     60
${ELF}              ${CURDIR}/../../../build/renode/gateway/zephyr/zephyr.elf

*** Keywords ***
Prepare Machine
    [Documentation]    Load platform, ELF, set vector table, and start emulation.
    [Arguments]    ${elf_path}
    Execute Command         include @${RESC}
    Create Terminal Tester  ${UART}    defaultPauseEmulation=True
    Write Char Delay        0.01
    Execute Command         sysbus LoadELF @${elf_path}
    Execute Command         cpu0 VectorTableOffset `sysbus GetSymbolAddress "_vector_table"`
    Execute Command         cpu0 EnableZephyrMode
    Start Emulation

Wait For Shell Prompt
    [Documentation]    Wait for the Zephyr shell prompt.
    Wait For Prompt On Uart    uart:~$    timeout=${UART_TIMEOUT}

Wait For Boot Banner
    [Documentation]    Wait for the Zephyr boot banner.
    Wait For Line On Uart    *** Booting Zephyr OS    timeout=${UART_TIMEOUT}

Send Shell Command
    [Documentation]    Send a shell command and wait for echo.
    [Arguments]    ${cmd}
    Write Line To Uart       ${cmd}
    Wait For Line On Uart    ${cmd}    timeout=5
