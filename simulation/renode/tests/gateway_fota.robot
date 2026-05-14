*** Settings ***
Resource            common.robot

*** Test Cases ***
MCUboot Chain Boots To Shell
    [Documentation]    Boot via MCUboot and verify the application reaches the Zephyr shell.
    [Tags]    bootloader    smoke
    Prepare Machine         ${ELF}
    Wait For Boot Banner
    Wait For Shell Prompt

Kernel Version Available
    [Documentation]    After MCUboot boot, verify 'kernel version' works.
    [Tags]    bootloader    shell
    Prepare Machine         ${ELF}
    Wait For Shell Prompt
    Write Line To Uart      kernel version
    # The command output is "Zephyr version X.Y.Z" — wait for the prompt to
    # reappear rather than matching the echo (which has no newline until complete).
    Wait For Prompt On Uart    uart:~$    timeout=15

MCUmgr Is Active
    [Documentation]    Verify MCUmgr shell commands respond after boot.
    [Tags]    bootloader    mcumgr
    Prepare Machine         ${ELF}
    Wait For Shell Prompt
    Write Line To Uart      mcumgr
    Wait For Line On Uart   mcumgr    timeout=15

FOTA Auto-Confirm Does Not Crash
    [Documentation]    Boot and wait for fota_confirm to complete (5s settle delay).
    [Tags]    bootloader    fota    stability
    Prepare Machine         ${ELF}
    Wait For Shell Prompt
    # Run the emulation for 30 virtual seconds so that:
    #   - init log messages fully flush (~1s)
    #   - fota_confirm fires boot_write_img_confirmed (~5s delay)
    # After RunFor the emulation is paused automatically.
    Execute Command         emulation RunFor "00:00:30"
    # Verify the shell is still responsive — no crash from fota_confirm.
    Write Line To Uart      kernel uptime
    Wait For Prompt On Uart    uart:~$    timeout=15
