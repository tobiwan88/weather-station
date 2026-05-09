# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LORA_RADIO)

  zephyr_library()
  zephyr_library_sources(
    src/lora_radio_main.c
    src/lora_packet.c
    src/lora_session.c
    src/lora_chan.c
    src/lora_handler_data.c
    src/lora_handler_rpc.c
    src/lora_handler_prov.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_DRV_SX1262
                               src/lora_drv_sx1262.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_FAKE src/lora_drv_fake.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_FOTA src/lora_handler_fota.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_SHELL src/lora_shell.c)
  zephyr_library_include_directories(include)

  zephyr_linker_sources(SECTIONS lora_radio_iterables.ld)

endif()
