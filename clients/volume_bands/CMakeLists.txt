add_executable(client_volume
  main.cpp
)
target_link_libraries(client_volume PRIVATE proto_objs PkgConfig::GRPC protobuf::libprotobuf)
