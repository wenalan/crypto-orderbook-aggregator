add_library(common INTERFACE)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# CHANGE this line:
# target_link_libraries(common INTERFACE proto_objs PkgConfig::GRPC protobuf::libprotobuf)
target_link_libraries(common INTERFACE proto_objs PkgConfig::GRPC protobuf::libprotobuf)
