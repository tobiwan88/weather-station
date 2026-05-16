*** Settings ***
Resource            common.robot

*** Test Cases ***
Outdoor Sensor Node Boots And Reaches Shell Prompt
    [Documentation]    Load the outdoor_sensor_node firmware and verify it boots to the Zephyr shell.
    [Tags]    smoke    boot    outdoor
    Prepare Machine         ${OUTDOOR_ELF}
    Wait For Boot Banner
    Wait For Shell Prompt

Shell Fake Sensors List Works On Outdoor Node
    [Documentation]    Verify 'fake_sensors list' executes on the outdoor sensor node.
    [Tags]    shell    sensors    outdoor
    Prepare Machine         ${OUTDOOR_ELF}
    Wait For Shell Prompt
    Write Line To Uart      fake_sensors list
    Wait For Prompt On Uart    uart:~$    timeout=15

Outdoor Node Does Not Crash After Boot
    [Documentation]    Let the outdoor sensor node run for 5 seconds after boot.
    [Tags]    stability    outdoor
    Prepare Machine         ${OUTDOOR_ELF}
    Wait For Shell Prompt
    Execute Command         sleep 5
    Write Line To Uart      kernel uptime
    Wait For Prompt On Uart    uart:~$    timeout=10
