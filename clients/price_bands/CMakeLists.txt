add_executable(client_pricebands
  main.cpp
)
target_link_libraries(client_pricebands PRIVATE proto_objs PkgConfig::GRPC protobuf::libprotobuf)
