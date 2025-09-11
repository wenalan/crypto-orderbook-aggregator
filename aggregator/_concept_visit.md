// common/adapter_concept.h
#pragma once
#include <string_view>
#include <functional>

struct OrderBook; 
using AdapterCallback = std::function<void(const OrderBook&, std::string_view venue)>;

template<class T>
concept AdapterLike = requires(T a, AdapterCallback cb) {
  { a.start(cb) } -> std::same_as<void>;
  { a.stop()  } -> std::same_as<void>;
  { a.name()  } -> std::convertible_to<std::string_view>;
};

//==========================

// common/adapters.h
#pragma once
#include <variant>
#include "adapter_concept.h"
#include "aggregator/binance_adapter.h"
#include "aggregator/okx_adapter.h"
#include "aggregator/kraken_adapter.h"

using AdapterVar = std::variant<BinanceAdapter, OkxAdapter, KrakenAdapter>;

template<class F>
inline decltype(auto) visit_adapter(AdapterVar& a, F&& f) {
  return std::visit([&](auto& impl) -> decltype(auto) {
    static_assert(AdapterLike<std::decay_t<decltype(impl)>>);
    return f(impl);
  }, a);
}

//==========================

// aggregator/main.cpp
#include "common/adapters.h"

std::vector<AdapterVar> adapters;

adapters.emplace_back(BinanceAdapter{"BTCUSDT"});
adapters.emplace_back(OkxAdapter{"BTC-USDT"});
adapters.emplace_back(KrakenAdapter{"XBT/USDT"});

for (auto& adp : adapters) {
  visit_adapter(adp, [&](auto& a){
    a.start([&](const OrderBook& ob, std::string_view venue){ on_update(ob, venue); });
  });
}

for (auto& adp : adapters) {
  visit_adapter(adp, [&](auto& a){ a.stop(); });
}
