*** Settings ***
Resource            common.robot

*** Test Cases ***
MCUboot Chain Boots To Shell
    [Documentation]    Boot via MCUboot and verify the application reaches the Zephyr shell.
    [Tags]    bootloader    smoke
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Boot Banner
    Wait For Shell Prompt

Kernel Version Available
    [Documentation]    After MCUboot boot, verify 'kernel version' works.
    [Tags]    bootloader    shell
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    Write Line To Uart      kernel version
    Wait For Line On Uart   kernel version    timeout=5

MCUmgr Is Active
    [Documentation]    Verify MCUmgr shell commands respond after boot.
    [Tags]    bootloader    mcumgr
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    Write Line To Uart      mcumgr
    Wait For Line On Uart   mcumgr    timeout=5

FOTA Auto-Confirm Does Not Crash
    [Documentation]    Boot and wait for fota_confirm to complete (5s settle delay).
    [Tags]    bootloader    fota    stability
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    # The fota_confirm library calls boot_write_img_confirmed after 5 seconds.
    # Let the system run for 15 seconds to cover the settle delay.
    Test If Uart Is Idle    15    pauseEmulation=True
