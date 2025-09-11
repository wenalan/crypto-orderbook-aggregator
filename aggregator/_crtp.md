// common/adapter_crtp.h
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include "order_book.h"

template <class Derived>
class AdapterCRTP {
public:
  using Callback = std::function<void(const OrderBook&, const char* /*venue*/)>;

  explicit AdapterCRTP(std::string symbol)
      : symbol_(std::move(symbol)) {}

  void start(Callback cb) {
    if (running_.exchange(true)) return;
    th_ = std::thread([this, cb] {
      self().run_impl(cb);
      running_ = false;
    });
  }

  void stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
  }

  bool running() const noexcept { return running_.load(); }
  const std::string& symbol() const noexcept { return symbol_; }

protected:
  Derived& self()             { return static_cast<Derived&>(*this); }
  const Derived& self() const { return static_cast<const Derived&>(*this); }

  std::atomic<bool> running_{false};
  std::thread       th_;
  std::string       symbol_;
};

//==========================

// aggregator/binance_adapter.h
#pragma once
#include <string>
#include "../common/adapter_crtp.h"
#include "../common/order_book.h"

class BinanceAdapter : public AdapterCRTP<BinanceAdapter> {
public:
  using Base = AdapterCRTP<BinanceAdapter>;
  explicit BinanceAdapter(std::string symbol) : Base(std::move(symbol)) {}
  ~BinanceAdapter() { stop(); }

  bool fetch_snapshot(OrderBook& out_book, long long& last_update_id, std::string& err);
  void apply_update_json(const std::string& payload, long long& last_update_id, OrderBook& book);

  void run_impl(Callback cb);
};


//=====================
#include "binance_adapter.h"
#include <boost/asio.hpp>

void BinanceAdapter::run_impl(Callback cb) {
  using namespace std::chrono_literals;
  while (running_) {
    try {
        //...
    } catch (const std::exception& e) {
      std::cerr << "[BINANCE] exception: " << e.what() << std::endl;
    }
    std::this_thread::sleep_for(1s);
  }
}

//===============
BinanceAdapter binance{ "BTCUSDT" };
OkxAdapter     okx{ "BTC-USDT" };
KrakenAdapter  kraken{ "BTC/USDT" };

binance.start([this](const OrderBook& ob, const char* src){ onVenueUpdate(ob, src); });
okx.start(...);
kraken.start(...);

