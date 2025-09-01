cmake_minimum_required(VERSION 3.20)
project(crypto_agg CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)

if (TARGET protobuf::protoc)
  get_target_property(_PROTOC_EXECUTABLE protobuf::protoc LOCATION)
else()
  find_program(_PROTOC_EXECUTABLE protoc REQUIRED)
endif()

find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(GEN_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

set(PROTO_FILE ${PROTO_DIR}/bookfeed.proto)
set(GEN_SRCS
  ${GEN_DIR}/bookfeed.pb.cc
  ${GEN_DIR}/bookfeed.grpc.pb.cc
)
set(GEN_HDRS
  ${GEN_DIR}/bookfeed.pb.h
  ${GEN_DIR}/bookfeed.grpc.pb.h
)

add_custom_command(
  OUTPUT ${GEN_SRCS} ${GEN_HDRS}
  COMMAND ${_PROTOC_EXECUTABLE}
          --proto_path=${PROTO_DIR}
          --cpp_out=${GEN_DIR}
          --grpc_out=${GEN_DIR}
          --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
          ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  COMMENT "Generating C++ sources from ${PROTO_FILE}"
  VERBATIM
)

add_library(proto_objs ${GEN_SRCS})
target_include_directories(proto_objs PUBLIC ${GEN_DIR} ${PROTO_DIR})
target_link_libraries(proto_objs PUBLIC PkgConfig::GRPC protobuf::libprotobuf)

enable_testing()

add_subdirectory(common)
add_subdirectory(aggregator)
add_subdirectory(clients/bbo)
add_subdirectory(clients/volume_bands)
add_subdirectory(clients/price_bands)
add_subdirectory(tests)
