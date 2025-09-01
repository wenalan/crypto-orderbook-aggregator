add_executable(client_bbo
  main.cpp
)
target_link_libraries(client_bbo PRIVATE proto_objs PkgConfig::GRPC protobuf::libprotobuf)
