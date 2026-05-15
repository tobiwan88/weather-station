*** Settings ***
Resource            common.robot

*** Test Cases ***
Gateway Boots And Collects Trace
    [Documentation]    Boot the gateway, wait for shell, let it run briefly,
    ...                then collect a trace dump.
    [Tags]    trace    boot
    Prepare Machine         ${ELF}
    Wait For Boot Banner
    Wait For Shell Prompt
    # Let the system run for a few seconds to accumulate trace data
    Execute Command         sleep 5
    Dump Trace
    # Verify trace buffer address resolves (confirms ELF loaded correctly)
    ${trace_addr}=         Execute Command    sysbus GetSymbolAddress "trace_records"
    Should Not Be Equal    ${trace_addr.strip()}    0x0    trace_records symbol not found
