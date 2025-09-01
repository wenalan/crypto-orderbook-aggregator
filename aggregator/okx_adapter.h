#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "../common/order_book.h"
#include "adapter_base.h"

class OKXAdapter : public AdapterBase {
public:
  using Callback = AdapterBase::Callback;

  explicit OKXAdapter(std::string symbol = "BTC-USDT");
  ~OKXAdapter();

  using AdapterBase::start;
  using AdapterBase::stop;

private:
  void run(Callback cb) override;
  bool fetch_snapshot(OrderBook& out_book, std::string& err);
  void apply_update_json(const std::string& payload, OrderBook& book);
  bool got_ws_snapshot{false};
  long long last_seq_id{-1};
};

