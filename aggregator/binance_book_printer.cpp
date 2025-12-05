#include "binance_adapter.h"

#include <atomic>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>
#include <iomanip>

namespace {
std::atomic<bool> g_stop{false};

void handle_signal(int) { g_stop.store(true); }

void print_book(const OrderBook& book, std::size_t depth = 10) {
  auto _flags = std::cout.flags();
  auto _prec  = std::cout.precision();
  std::cout.setf(std::ios::fixed);
  std::cout << std::setprecision(6);

  std::cout << "[BINANCE] top " << depth << " levels\n";

  std::cout << "  Bids: ";
  std::size_t i = 0;
  for (const auto& [p, s] : book.bids) {
    if (i++ >= depth) break;
    std::cout << p << "@" << s << "  ";
  }
  if (i == 0) std::cout << "(empty)";
  std::cout << "\n";

  std::cout << "  Asks: ";
  i = 0;
  for (const auto& [p, s] : book.asks) {
    if (i++ >= depth) break;
    std::cout << p << "@" << s << "  ";
  }
  if (i == 0) std::cout << "(empty)";
  std::cout << "\n" << std::endl;

  std::cout.flags(_flags);
  std::cout.precision(_prec);
}
} // namespace

int main(int argc, char** argv) {
  std::string symbol = "BTCUSDT";
  if (argc > 1 && argv[1]) {
    symbol = argv[1];
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::cout << "Starting Binance order book stream for symbol " << symbol
            << " (Ctrl+C to quit)" << std::endl;

  BinanceAdapter adapter(symbol);
  adapter.start([](const OrderBook& book, const char*) {
    print_book(book);
  });

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "Stopping..." << std::endl;
  adapter.stop();
  return 0;
}
