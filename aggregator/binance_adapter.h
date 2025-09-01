#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "../common/order_book.h"
#include "adapter_base.h"

class BinanceAdapter : public AdapterBase {
public:
  using Callback = AdapterBase::Callback;
  
  explicit BinanceAdapter(std::string symbol = "BTCUSDT");
  ~BinanceAdapter();

  using AdapterBase::start;
  using AdapterBase::stop;

private:
  void run(Callback cb) override;
  bool fetch_snapshot(OrderBook& out_book, long long& last_update_id, std::string& err);
  void apply_update_json(const std::string& payload, long long& last_update_id, OrderBook& book);
};
