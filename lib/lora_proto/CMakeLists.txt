# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LORA_PROTO)

  zephyr_library()
  zephyr_library_sources(src/lora_packet.c)

  # Public headers: <lora_proto/lora_frame.h>, <lora_proto/lora_packet.h>
  zephyr_include_directories(include)

endif()
