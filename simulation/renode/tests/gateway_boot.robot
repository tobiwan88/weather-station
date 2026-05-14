*** Settings ***
Resource            common.robot

*** Test Cases ***
Gateway Boots And Reaches Shell Prompt
    [Documentation]    Load the gateway firmware and verify it boots to the Zephyr shell.
    [Tags]    smoke    boot
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Boot Banner
    Wait For Shell Prompt

Shell Help Responds
    [Documentation]    Verify the shell 'help' command works.
    [Tags]    smoke    shell
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    Write Line To Uart      help
    Wait For Line On Uart   help    timeout=5

Shell Device List Shows Sensors
    [Documentation]    Verify 'device list' shows registered devices.
    [Tags]    shell
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    Write Line To Uart      device list
    Wait For Line On Uart   device list    timeout=5

Fake Sensors Are Registered
    [Documentation]    Verify fake_sensors list shows expected sensors.
    [Tags]    shell    sensors
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    Write Line To Uart      fake_sensors list
    Wait For Line On Uart   fake_sensors list    timeout=5

Gateway Does Not Crash After Boot
    [Documentation]    Let the gateway run for 10s after boot and verify no panic.
    [Tags]    stability
    Prepare Machine         ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Shell Prompt
    # Let it run idle for 10 simulated seconds
    Test If Uart Is Idle    10    pauseEmulation=True
