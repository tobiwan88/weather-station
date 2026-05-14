*** Settings ***
Suite Setup         Setup
Suite Teardown      Teardown
Test Teardown       Test Teardown
Resource            ${RENODEKEYWORDS}
Library             Process

*** Variables ***
${RESC}             ${CURDIR}/../frdm_mcxn947.resc
${UART}             sysbus.flexcomm4lpuart4
${UART_TIMEOUT}     30

*** Keywords ***
Setup
    [Documentation]    Per-suite setup: load platform and prepare terminal tester.
    Execute Command         include @${RESC}
    Create Terminal Tester  ${UART}    defaultPauseEmulation=True
    Write Char Delay        0.01

Prepare Machine
    [Documentation]    Load ELF, run reset macro, and start emulation.
    [Arguments]    ${elf_path}
    Execute Command         \$bin=@${elf_path}
    Execute Command         runMacro \$reset
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
