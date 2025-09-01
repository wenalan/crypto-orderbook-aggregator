#pragma once
#include "order_book.h"
#include <vector>
#include <cmath>

struct ConsolidationCfg {
  double tick{0.1};
  size_t topN{200};
};

inline double floor_to_tick(double p, double tick) {
  if (tick <= 0) return p;
  return std::floor(p / tick) * tick;
}
inline double ceil_to_tick(double p, double tick) {
  if (tick <= 0) return p;
  return std::ceil(p / tick) * tick;
}


inline OrderBook consolidate(const std::vector<OrderBook>& books, const ConsolidationCfg& cfg) {
  OrderBook merged;
  if (books.empty()) return merged;

  if (!std::isfinite(cfg.tick) || cfg.tick <= 0.0)
    throw std::invalid_argument("ConsolidationCfg.tick must be finite and > 0");

  if (cfg.topN <= 0)
    throw std::invalid_argument("ConsolidationCfg.topN must be >= 1");

  constexpr long double SCALE = 100000000.0L; // 1e8
  const long long tick_i = std::llround(static_cast<long double>(cfg.tick) * SCALE);

  std::map<long long,double,std::greater<long long>> bids_i;
  std::map<long long,double,std::less<long long>>    asks_i;

  for (const auto& ob : books) {
    for (const auto& kv : ob.bids) {
      const long double p = static_cast<long double>(kv.first);
      const double s = kv.second;
      if (!(s > 0.0)) continue;
      const long long p_i = static_cast<long long>(std::floor(p * SCALE));
      const long long id  = p_i / tick_i; // floor for bids
      bids_i[id] += s;
    }
    for (const auto& kv : ob.asks) {
      const long double p = static_cast<long double>(kv.first);
      const double s = kv.second;
      if (!(s > 0.0)) continue;
      const long long p_i = static_cast<long long>(std::ceil(p * SCALE));
      const long long id  = (p_i + tick_i - 1) / tick_i; // ceil for asks
      asks_i[id] += s;
    }
  }

  if (bids_i.size() > cfg.topN) { auto it=bids_i.begin(); std::advance(it, cfg.topN); bids_i.erase(it, bids_i.end()); }
  if (asks_i.size() > cfg.topN) { auto it=asks_i.begin(); std::advance(it, cfg.topN); asks_i.erase(it, asks_i.end()); }

  for (const auto& kv : bids_i) {
    const long double price = (static_cast<long double>(kv.first) * static_cast<long double>(tick_i)) / SCALE;
    merged.bids[static_cast<double>(price)] = kv.second;
  }
  for (const auto& kv : asks_i) {
    const long double price = (static_cast<long double>(kv.first) * static_cast<long double>(tick_i)) / SCALE;
    merged.asks[static_cast<double>(price)] = kv.second;
  }
  return merged;
}

