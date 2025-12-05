add_executable(aggregator
  main.cpp
  binance_adapter.cpp
  binance_adapter.h
  okx_adapter.cpp
  okx_adapter.h
  kraken_adapter.cpp
  kraken_adapter.h
)

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

target_link_libraries(aggregator
  PRIVATE
    common proto_objs
    PkgConfig::GRPC protobuf::libprotobuf
    Boost::system
    OpenSSL::SSL OpenSSL::Crypto
)

add_executable(binance_book_printer
  binance_book_printer.cpp
  binance_adapter.cpp
  binance_adapter.h
)

target_link_libraries(binance_book_printer
  PRIVATE
    common
    Boost::system
    OpenSSL::SSL OpenSSL::Crypto
)
