# SPDX-License-Identifier: Apache-2.0
if(CONFIG_PIPE_TRANSPORT)
  list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
  include(nanopb)

  zephyr_library()

  # Generate sensor_message.pb.c/.pb.h into CMAKE_CURRENT_BINARY_DIR so that
  # #include "sensor_message.pb.h" resolves without path gymnastics.
  set(NANOPB_GENERATE_CPP_STANDALONE OFF)
  set(_proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../proto)
  nanopb_generate_cpp(_pb_srcs _pb_hdrs RELPATH ${_proto_dir}
                      ${_proto_dir}/sensor_message.proto)
  target_include_directories(${ZEPHYR_CURRENT_LIBRARY}
                             PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${_pb_srcs} ${_pb_hdrs})
  string(MD5 _hash "${_pb_hdrs}")
  add_custom_target(_pipe_xport_proto_${_hash} DEPENDS ${_pb_hdrs})
  add_dependencies(nanopb_generated_headers _pipe_xport_proto_${_hash})
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} nanopb_generated_headers)

  zephyr_library_sources(src/pipe_transport.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)
endif()
