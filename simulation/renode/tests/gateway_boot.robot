*** Settings ***
Resource            common.robot

*** Test Cases ***
Gateway Boots And Reaches Shell Prompt
    [Documentation]    Load the gateway firmware and verify it boots to the Zephyr shell.
    [Tags]    smoke    boot
    Prepare Machine         ${ELF}
    Wait For Boot Banner
    Wait For Shell Prompt

Shell Help Responds After Boot
    [Documentation]    Verify the shell 'help' command works.
    [Tags]    smoke    shell
    Prepare Machine         ${ELF}
    Wait For Shell Prompt
    Write Line To Uart      help
    # 'help' command prints a list of available commands. Wait for 'fake_sensors'
    # which is guaranteed to be in the output since CONFIG_FAKE_SENSORS=y.
    Wait For Line On Uart   fake_sensors    timeout=10

Shell Fake Sensors List Works
    [Documentation]    Verify 'fake_sensors list' executes without crashing.
    [Tags]    shell    sensors
    Prepare Machine         ${ELF}
    Wait For Shell Prompt
    Write Line To Uart      fake_sensors list
    # After the command, a new prompt should appear (command completed)
    Wait For Prompt On Uart    uart:~$    timeout=15

Gateway Does Not Crash After Boot
    [Documentation]    Let the gateway run for 10 seconds after boot.
    [Tags]    stability
    Prepare Machine         ${ELF}
    Wait For Shell Prompt
    # After reaching shell, wait 10s and check prompt still works
    Execute Command         sleep 10
    Write Line To Uart      kernel uptime
    Wait For Prompt On Uart    uart:~$    timeout=10
