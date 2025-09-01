#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "../common/order_book.h"
#include "adapter_base.h"

class KrakenAdapter : public AdapterBase {
public:
  using Callback = AdapterBase::Callback;

  explicit KrakenAdapter(std::string symbol = "XBTUSDT");
  ~KrakenAdapter();

  using AdapterBase::start;
  using AdapterBase::stop;

private:
  void run(Callback cb) override;
  bool fetch_snapshot(OrderBook& out_book, std::string& err);
  void apply_ws_message(const std::string& payload, OrderBook& book);

  std::string normalize_symbol_for_ws(const std::string& in) const;
  bool got_ws_snapshot{false};
};

