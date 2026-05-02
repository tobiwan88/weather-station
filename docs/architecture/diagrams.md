# Architecture Diagrams

All diagrams are authored as Mermaid source files in this page. They render
client-side via the MkDocs Mermaid plugin.

## System Overview

Layered component view of the weather-station firmware.

```mermaid
graph TB
    subgraph APP["APPLICATION LAYER"]
        GW["apps/gateway\nmain.c <50 lines\nprj.conf (features)"]
        SN["apps/sensor-node\npipe_publisher egress\nprj.conf (lighter stack)"]
    end

    subgraph LIBS["LIBRARY LAYER (lib/)"]
        direction TB
        subgraph CHANNELS["zbus Channels"]
            SEC["sensor_event\n(env_sensor_data)"]
            STC["sensor_trigger\n(trigger event)"]
        end
        subgraph SENSORS["Sensor Layer"]
            FS["fake_sensors\nDT-driven drivers\n+ auto-publish timer"]
            SR["sensor_registry\nuid → metadata"]
        end
        subgraph SERVICES["Services"]
            SNTP["sntp_sync\nwall-clock SNTP"]
            HTTP["http_dashboard\nChart.js + config API\nport 8080"]
            CLK["clock_display\nHH:MM UTC ticker"]
            LVGL["lvgl_display\nSDL window (native_sim)"]
            CC["config_cmd\ncmd channel"]
            LOC["location_registry\nnamed locations"]
            SEL["sensor_event_log\nconsole logger"]
            RS["remote_sensor\ntransport abstraction + manager"]
            FRS["fake_remote_sensor\ntesting stub"]
            MQTT["mqtt_publisher\nMQTT egress"]
            PIPE_PUB["pipe_publisher\nFIFO protobuf writer\n(sensor-node)"]
            PIPE_TX["pipe_transport\nFIFO protobuf reader\n(gateway)"]
        end
    end

    subgraph RTOS["ZEPHYR RTOS"]
        ZBUS["zbus pub/sub"]
        KTIMER["k_timer"]
        KWORK["k_work_delayable"]
        KSPIN["k_spinlock"]
        KMUX["k_mutex"]
        HTTPD["HTTP_SERVER"]
        NET["NET_SOCKETS / SNTP"]
        LVGLLIB["LVGL / SDL2"]
    end

    GW -->|"Kconfig selects"| LIBS
    SN -->|"Kconfig selects"| LIBS

    FS -->|publishes| SEC
    FS -->|subscribes| STC
    FS -->|subscribes| CC
    HTTP -->|subscribes| SEC
    HTTP -->|reads| SR
    HTTP -->|reads| LOC
    HTTP -->|publishes| CC
    FS -->|registers| SR
    SNTP -->|subscribes| CC
    SEL -->|subscribes| SEC
    RS -->|publishes| SEC
    RS -->|subscribes| STC
    FRS -->|implements vtable| RS
    MQTT -->|subscribes| SEC
    PIPE_PUB -->|publishes| SEC
    PIPE_PUB -->|writes FIFO| SEC
    PIPE_TX -->|reads FIFO| SEC
    PIPE_TX -->|publishes| SEC

    SEC --> ZBUS
    STC --> ZBUS
    SNTP --> NET
    HTTP --> HTTPD
    CLK --> KWORK
    SNTP --> KWORK
    FS --> KTIMER
    HTTP --> KSPIN
    SR --> KMUX
    LVGL --> LVGLLIB

    style APP fill:#dbeafe,stroke:#3b82f6
    style LIBS fill:#dcfce7,stroke:#16a34a
    style CHANNELS fill:#fef9c3,stroke:#ca8a04
    style SENSORS fill:#f0fdf4,stroke:#16a34a
    style SERVICES fill:#f0fdf4,stroke:#16a34a
    style RTOS fill:#fce7f3,stroke:#db2777
```

## zbus Channel Map

Publishers, channels, and subscribers across the zbus event bus.

```mermaid
graph LR
    subgraph TRIGGERS["Trigger Sources"]
        TMR["fake_sensors_timer\nk_timer periodic"]
        BOOT["SYS_INIT startup\none-shot at boot"]
        BTN["button ISR\n(future)"]
        MQTT_CMD["MQTT command\n(future)"]
    end

    subgraph CHAN_T["sensor_trigger_chan (zbus)"]
        ST["struct sensor_trigger_event\n{ source, target_uid }"]
    end

    subgraph SENSORS["Sensor Listeners"]
        FT["fake_temperature\nlistener"]
        FH["fake_humidity\nlistener"]
        RSM["remote_sensor_manager\n(pull transports)"]
        RD["real drivers\n(future)"]
    end

    subgraph CHAN_E["sensor_event_chan (zbus)"]
        SE["struct env_sensor_data\n{ uid, type, q31_value, timestamp_ms }"]
    end

    subgraph CONSUMERS["Event Consumers"]
        SEL["sensor_event_log\nLOG_INF per event"]
        HTTPL["http_dashboard\nring buffer append"]
        MQTTP["mqtt_publisher\nbroker egress"]
        FLASH["flash storage\n(future)"]
    end

    subgraph CHAN_CFG["config_cmd_chan (zbus)"]
        CC["struct config_cmd_event\n{ cmd, arg }"]
    end

    subgraph CFG_CONSUMERS["Config Consumers"]
        FS_CFG["fake_sensors\ninterval update"]
        SNTP_CFG["sntp_sync\nresync trigger"]
    end

    subgraph CHAN_DISC["remote discovery (k_msgq, not zbus)"]
        RD2["remote_sensor_announce_disc()\n→ manager k_msgq"]
    end

    subgraph CHAN_SCAN["remote_scan_ctrl_chan (zbus)"]
        SC["struct remote_scan_ctrl_event\n{ action, proto }"]
    end

    subgraph TRANSPORTS["Transport Adapters"]
        FAKE_T["fake_remote_sensor\n(testing stub)"]
        BLE_T["ble_sensor\n(future)"]
        LORA_T["lora_sensor\n(future)"]
    end

    TMR -->|pub K_NO_WAIT| ST
    BOOT -->|pub K_NO_WAIT| ST
    BTN -->|pub K_NO_WAIT| ST
    MQTT_CMD -->|pub K_NO_WAIT| ST

    ST -->|sub| FT
    ST -->|sub| FH
    ST -->|sub| RSM
    ST -->|sub| RD

    FT -->|pub K_NO_WAIT| SE
    FH -->|pub K_NO_WAIT| SE
    RSM -->|remote_sensor_publish_data| SE
    RD -->|pub K_NO_WAIT| SE

    SE -->|sub| SEL
    SE -->|sub| HTTPL
    SE -->|sub| MQTTP
    SE -->|sub| FLASH

    HTTPL -->|pub config_cmd_event| CC
    CC -->|sub| FS_CFG
    CC -->|sub| SNTP_CFG
    CC -->|sub| MQTT_CFG

    FAKE_T -->|remote_sensor_announce_disc| RD2
    BLE_T -->|remote_sensor_announce_disc| RD2
    LORA_T -->|remote_sensor_announce_disc| RD2
    RD2 -->|k_msgq| RSM

    RSM -->|pub K_NO_WAIT| SC
    SC -->|vtable scan_start/stop| FAKE_T
    SC -->|vtable scan_start/stop| BLE_T
    SC -->|vtable scan_start/stop| LORA_T

    style CHAN_T fill:#fef9c3,stroke:#ca8a04
    style CHAN_E fill:#fef9c3,stroke:#ca8a04
    style CHAN_CFG fill:#fef9c3,stroke:#ca8a04
    style CHAN_DISC fill:#fef9c3,stroke:#ca8a04
    style CHAN_SCAN fill:#fef9c3,stroke:#ca8a04
    style TRIGGERS fill:#dbeafe,stroke:#3b82f6
    style SENSORS fill:#dcfce7,stroke:#16a34a
    style CONSUMERS fill:#fce7f3,stroke:#db2777
    style CFG_CONSUMERS fill:#fce7f3,stroke:#db2777
    style TRANSPORTS fill:#e0e7ff,stroke:#6366f1
```

## Sensor Data Flow

Sensor trigger → event → consumers sequence.

```mermaid
sequenceDiagram
    participant TMR as fake_sensors_timer<br/>(k_timer ISR)
    participant TRIG as sensor_trigger_chan<br/>(zbus)
    participant FTEMP as fake_temperature<br/>(zbus listener)
    participant FHUM as fake_humidity<br/>(zbus listener)
    participant EVCHAN as sensor_event_chan<br/>(zbus)
    participant GW as gateway listener<br/>(LOG_INF)
    participant DASH as http_dashboard<br/>(ring buffer)
    participant BROWSER as Browser<br/>GET /api/data

    Note over TMR: Every AUTO_PUBLISH_MS
    TMR->>TRIG: pub sensor_trigger_event<br/>{ source=TIMER, target_uid=0 }

    Note over TRIG,FHUM: zbus dispatches to all listeners
    TRIG->>FTEMP: listener callback
    FTEMP->>FTEMP: read milli-°C → Q31 encode
    FTEMP->>EVCHAN: pub env_sensor_data<br/>{ uid, TEMPERATURE, q31, ts }

    TRIG->>FHUM: listener callback
    FHUM->>FHUM: read milli-%RH → Q31 encode
    FHUM->>EVCHAN: pub env_sensor_data<br/>{ uid, HUMIDITY, q31, ts }

    Note over EVCHAN,DASH: zbus dispatches to all subscribers
    EVCHAN->>GW: listener callback → LOG_INF
    EVCHAN->>DASH: listener callback

    Note over DASH: acquire k_spinlock
    DASH->>DASH: append (ts, q31) to ring buffer
    Note over DASH: release k_spinlock

    Note over BROWSER,DASH: Later — HTTP GET /api/data
    BROWSER->>DASH: HTTP GET /api/data
    Note over DASH: acquire k_spinlock
    DASH->>DASH: memcpy snapshot
    Note over DASH: release k_spinlock
    DASH->>DASH: serialize snapshot → JSON<br/>(no lock held)
    DASH-->>BROWSER: 200 OK { sensors: [...] }
```

## Library Dependencies

Inter-library dependency graph.

```mermaid
graph TD
    APP["apps/gateway\n(application)"]

    SE["sensor_event\nenv_sensor_data + chan"]
    ST["sensor_trigger\ntrigger event + chan"]
    SR["sensor_registry\nuid → metadata"]
    FS["fake_sensors\nDT-driven drivers"]
    SNTP["sntp_sync\nwall-clock SNTP"]
    HTTP["http_dashboard\nweb dashboard + API"]
    CLK["clock_display\nHH:MM ticker"]
    LVGL["lvgl_display\nSDL window"]
    CC["config_cmd\ncmd channel"]
    LOC["location_registry\nnamed locations"]
    SEL["sensor_event_log\nconsole logger"]
    RS["remote_sensor\ntransport abstraction"]
    FRS["fake_remote_sensor\ntesting stub"]
    MQTT["mqtt_publisher\nMQTT egress"]

    ZBUS(["ZBUS\n(Zephyr)"])
    NET(["NETWORKING\n(Zephyr)"])
    HTTPD(["HTTP_SERVER\n(Zephyr)"])
    LVGLLIB(["LVGL + DISPLAY\n(Zephyr)"])
    SNTPLIB(["SNTP\n(Zephyr)"])
    SETTINGS(["SETTINGS\n(Zephyr)"])

    APP -->|Kconfig| SE
    APP -->|Kconfig| ST
    APP -->|Kconfig| SR
    APP -->|Kconfig| FS
    APP -->|Kconfig| SNTP
    APP -->|Kconfig| HTTP
    APP -->|Kconfig| CLK
    APP -->|Kconfig| LVGL
    APP -->|Kconfig| CC
    APP -->|Kconfig| LOC
    APP -->|Kconfig| SEL
    APP -->|Kconfig| RS
    APP -->|Kconfig| FRS
    APP -->|Kconfig| MQTT

    FS --> SE
    FS --> ST
    FS --> SR
    FS --> CC

    HTTP --> SE
    HTTP --> SR
    HTTP --> LOC
    HTTP --> CC
    HTTP --> HTTPD

    SNTP --> CC
    SNTP --> NET
    SNTP --> SNTPLIB

    SEL --> SE

    RS --> SE
    RS --> SR
    RS --> SETTINGS
    FRS --> RS

    MQTT --> SE
    MQTT --> NET

    LOC --> SETTINGS

    SE --> ZBUS
    ST --> ZBUS
    SR --> ZBUS
    CC --> ZBUS

    HTTP --> NET
    LVGL --> LVGLLIB

    style APP fill:#dbeafe,stroke:#3b82f6
    style ZBUS fill:#fce7f3,stroke:#db2777
    style NET fill:#fce7f3,stroke:#db2777
    style HTTPD fill:#fce7f3,stroke:#db2777
    style LVGLLIB fill:#fce7f3,stroke:#db2777
    style SNTPLIB fill:#fce7f3,stroke:#db2777
    style SETTINGS fill:#fce7f3,stroke:#db2777
```

## Boot Sequence

SYS_INIT boot ordering by priority.

```mermaid
sequenceDiagram
    participant KERNEL as Zephyr Kernel
    participant SNTP as sntp_sync<br/>priority 80
    participant FT as fake_temperature<br/>priority 90
    participant FH as fake_humidity<br/>priority 91
    participant LVGL as lvgl_display<br/>priority 91
    participant CO2 as fake_co2<br/>priority 92
    participant RSM as remote_sensor_mgr<br/>priority 92
    participant VOC as fake_voc<br/>priority 93
    participant FRS as fake_remote_sensor<br/>priority 93
    participant RSET as remote_sensor_settings<br/>priority 94
    participant SEL as sensor_event_log<br/>priority 95
    participant LOC as location_registry<br/>priority 96
    participant DASH as http_dashboard<br/>priority 97
    participant MQTTP as mqtt_publisher<br/>priority 98
    participant TMR as fake_sensors_timer<br/>priority 99
    participant CLK as clock_display<br/>priority 99
    participant MAIN as main()

    KERNEL->>SNTP: sntp_sync_init()
    SNTP->>SNTP: initial SNTP query (blocking)
    SNTP->>SNTP: schedule periodic_work (K_SECONDS(3600))
    SNTP-->>KERNEL: return 0

    KERNEL->>FT: fake_temperature_init()
    FT->>FT: sensor_registry_register(uid, ...)
    FT->>FT: zbus_chan_add_obs(sensor_trigger_chan)
    FT->>FT: pub sensor_trigger_event(STARTUP)
    FT-->>KERNEL: return 0

    KERNEL->>FH: fake_humidity_init()
    FH->>FH: sensor_registry_register(uid, ...)
    FH->>FH: zbus_chan_add_obs(sensor_trigger_chan)
    FH->>FH: pub sensor_trigger_event(STARTUP)
    FH-->>KERNEL: return 0

    KERNEL->>LVGL: lvgl_display_init()
    LVGL->>LVGL: zbus_chan_add_obs(sensor_event_chan)
    LVGL->>LVGL: start LVGL workqueue thread
    LVGL-->>KERNEL: return 0

    KERNEL->>CO2: fake_co2_init()
    CO2->>CO2: sensor_registry_register(uid, ...)
    CO2->>CO2: zbus_chan_add_obs(sensor_trigger_chan)
    CO2->>CO2: pub sensor_trigger_event(STARTUP)
    CO2-->>KERNEL: return 0

    KERNEL->>RSM: remote_sensor_manager_init()
    RSM->>RSM: zbus_chan_add_obs(remote_discovery_chan)
    RSM->>RSM: pub remote_scan_ctrl_event(START_ALL)
    RSM-->>KERNEL: return 0

    KERNEL->>VOC: fake_voc_init()
    VOC->>VOC: sensor_registry_register(uid, ...)
    VOC->>VOC: zbus_chan_add_obs(sensor_trigger_chan)
    VOC->>VOC: pub sensor_trigger_event(STARTUP)
    VOC-->>KERNEL: return 0

    KERNEL->>FRS: fake_remote_sensor_init()
    FRS->>FRS: init node state + start simulation timers
    FRS-->>KERNEL: return 0

    KERNEL->>RSET: remote_sensor_settings_init()
    RSET->>RSET: load persisted peer list → re-register known remote sensors
    RSET-->>KERNEL: return 0

    KERNEL->>SEL: sensor_event_log_init()
    SEL->>SEL: zbus_chan_add_obs(sensor_event_chan)
    Note over SEL: also at priority 95: sensor_registry settings load,<br/>sntp_sync config_cmd init, fake_sensors config_cmd init
    SEL-->>KERNEL: return 0

    KERNEL->>LOC: location_registry_settings_load()
    LOC->>LOC: load persisted location names from settings
    LOC-->>KERNEL: return 0

    KERNEL->>DASH: http_dashboard_init()
    DASH->>DASH: zbus_chan_add_obs(sensor_event_chan)
    DASH->>DASH: HTTP_SERVICE start on port 8080
    DASH-->>KERNEL: return 0

    KERNEL->>MQTTP: mqtt_publisher_init()
    MQTTP->>MQTTP: zbus_chan_add_obs(sensor_event_chan)
    MQTTP->>MQTTP: start publisher thread (connect to broker)
    MQTTP-->>KERNEL: return 0

    KERNEL->>TMR: fake_sensors_timer_init()
    TMR->>TMR: k_timer_start(AUTO_PUBLISH_MS)
    TMR-->>KERNEL: return 0

    KERNEL->>CLK: clock_display_init()
    CLK->>CLK: k_work_reschedule(K_NO_WAIT)
    CLK-->>KERNEL: return 0

    KERNEL->>MAIN: main()
    Note over MAIN: if CONFIG_LVGL_DISPLAY:<br/>  lvgl_display_run() — never returns<br/>else:<br/>  k_sleep(K_FOREVER)
```

## HTTP Config Flow

POST /api/config side-effects.

```mermaid
sequenceDiagram
    participant BR as Browser
    participant HTTP as HTTP Server Thread
    participant CC as config_cmd_chan<br/>(zbus)
    participant FS as fake_sensors_timer
    participant SNTP as sntp_sync
    participant SR as sensor_registry
    participant LOC as location_registry
    participant MQTTP as mqtt_publisher

    BR->>HTTP: POST /api/config<br/>trigger_interval_ms=2000<br/>sntp_server=ntp.ubuntu.com<br/>action=sntp_resync<br/>sensor_1_name=Kitchen<br/>mqtt_enabled=true

    Note over HTTP: Parse URL-encoded body<br/>form_extract() each field

    alt trigger_interval_ms present
        HTTP->>CC: publish CONFIG_CMD_SET_TRIGGER_INTERVAL
        CC-->>FS: zbus listener callback
        FS->>FS: k_timer_start(&g_auto_timer,<br/>K_MSEC(2000), K_MSEC(2000))
    end

    alt sntp_server present
        HTTP->>HTTP: update sntp_server_buf<br/>(spinlock-protected snapshot)
    end

    alt action=sntp_resync
        HTTP->>CC: publish CONFIG_CMD_SNTP_RESYNC
        CC-->>SNTP: zbus listener callback
        SNTP->>SNTP: k_work_reschedule(&immediate_work, K_NO_WAIT)
        Note over SNTP: Runs async in work queue<br/>performs SNTP query
    end

    alt sensor_<uid>_name present and USER_META enabled
        HTTP->>SR: sensor_registry_set_meta(uid, .display_name="Kitchen")
        alt SETTINGS enabled
            SR->>SR: settings_save_subtree(...)
        end
    end

    alt action=add_location / action=remove_location
        HTTP->>LOC: location_registry_add/remove(name)
        alt SETTINGS enabled
            LOC->>LOC: settings_save_subtree(...)
        end
    end

    alt mqtt_* fields present
        HTTP->>CC: publish CONFIG_CMD_MQTT_SET_*
        CC-->>MQTTP: zbus listener callback
        MQTTP->>MQTTP: update config, reconnect if needed
    end

    HTTP-->>BR: 303 See Other → /config
```
