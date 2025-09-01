#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "../common/order_book.h"

class AdapterBase {
public:
  using Callback = std::function<void(const OrderBook&, const char* venue)>;

  explicit AdapterBase(std::string symbol) : symbol_(std::move(symbol)) {}
  virtual ~AdapterBase() { stop(); }

  void start(Callback cb) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    th_ = std::thread([this, cb]{ this->run(cb); });
  }

  void stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;
    if (th_.joinable()) th_.join();
  }

protected:
  virtual void run(Callback cb) = 0;
  bool running() const { return running_.load(std::memory_order_relaxed); }
  std::string symbol_;
  std::mutex book_mu_;

private:
  std::thread th_;
  std::atomic<bool> running_{false};
};
