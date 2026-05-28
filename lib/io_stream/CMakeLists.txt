# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IO_STREAM)

  zephyr_library()
  zephyr_library_sources(src/io_stream.c)
  zephyr_library_sources_ifdef(CONFIG_IO_STREAM_BUFFER src/io_stream_buffer.c)
  zephyr_library_sources_ifdef(CONFIG_IO_STREAM_FLASH src/io_stream_flash.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
