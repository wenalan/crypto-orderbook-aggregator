#pragma once
#include "order_book.h"
#include <vector>
#include <cmath>

struct ConsolidationCfg {
  double tick{0.1};
  size_t topN{50};
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

  // Fallback: invalid tick -> raw merge then topN
  if (!(cfg.tick > 0.0)) {
    for (const auto& ob : books) {
      for (const auto& kv : ob.bids) if (kv.second > 0.0) merged.bids[kv.first] += kv.second;
      for (const auto& kv : ob.asks) if (kv.second > 0.0) merged.asks[kv.first] += kv.second;
    }
    if (merged.bids.size() > cfg.topN) { auto it=merged.bids.begin(); std::advance(it, cfg.topN); merged.bids.erase(it, merged.bids.end()); }
    if (merged.asks.size() > cfg.topN) { auto it=merged.asks.begin(); std::advance(it, cfg.topN); merged.asks.erase(it, merged.asks.end()); }
    return merged;
  }

  // Fixed-point with long double to stabilize boundaries
  constexpr long long SCALE_INT = 100000000LL; // 1e8
  constexpr long double SCALE = 100000000.0L;
  const long long tick_i = std::llround(static_cast<long double>(cfg.tick) * SCALE);

  std::map<long long,double,std::greater<long long>> bids_i;
  std::map<long long,double,std::less<long long>>    asks_i;

  for (const auto& ob : books) {
    for (const auto& kv : ob.bids) {
      const long double p = static_cast<long double>(kv.first);
      const double s = kv.second;
      if (!(s > 0.0)) continue;
      const long long p_i = static_cast<long long>(std::floor(p * SCALE));
      const long long id  = p_i / tick_i; // floor
      bids_i[id] += s;
    }
    for (const auto& kv : ob.asks) {
      const long double p = static_cast<long double>(kv.first);
      const double s = kv.second;
      if (!(s > 0.0)) continue;
      const long long p_i = static_cast<long long>(std::ceil(p * SCALE));
      const long long id  = (p_i + tick_i - 1) / tick_i; // ceil
      asks_i[id] += s;
    }
  }

  // TopN after merge
  if (bids_i.size() > cfg.topN) { auto it=bids_i.begin(); std::advance(it, cfg.topN); bids_i.erase(it, bids_i.end()); }
  if (asks_i.size() > cfg.topN) { auto it=asks_i.begin(); std::advance(it, cfg.topN); asks_i.erase(it, asks_i.end()); }

  // Convert back
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

