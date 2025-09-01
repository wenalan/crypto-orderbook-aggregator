include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(unit_tests
  test_order_book.cpp
  test_consolidator.cpp
  test_bbo.cpp
  test_price_bands.cpp
  test_volume_bands.cpp
  test_tick_alignment.cpp
  adapter_binance_test.cpp
  adapter_okx_test.cpp
  adapter_kraken_test.cpp
  ../aggregator/binance_adapter.cpp
  ../aggregator/okx_adapter.cpp
  ../aggregator/kraken_adapter.cpp
)
target_include_directories(unit_tests PRIVATE ../common ../aggregator)
target_link_libraries(unit_tests PRIVATE gtest_main common OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
add_test(NAME unit_tests COMMAND unit_tests)
