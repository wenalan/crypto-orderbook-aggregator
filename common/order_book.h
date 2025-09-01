#pragma once
#include <map>
#include <vector>
#include <algorithm>

struct LevelPxSz { double price{}; double size{}; };

struct OrderBook {
  std::map<double,double,std::greater<double>> bids;
  std::map<double,double,std::less<double>> asks;
};

// Apply L2 deltas: price -> new size (0 means remove)
inline void apply_deltas(std::map<double,double,std::greater<double>>& side, const std::vector<std::pair<double,double>>& deltas) {
  for (auto& [p, sz] : deltas) {
    if (sz == 0.0) { auto it = side.find(p); if (it != side.end()) side.erase(it); }
    else side[p] = sz;
  }
}

inline void apply_deltas(std::map<double,double,std::less<double>>& side, const std::vector<std::pair<double,double>>& deltas) {
  for (auto& [p, sz] : deltas) {
    if (sz == 0.0) { auto it = side.find(p); if (it != side.end()) side.erase(it); }
    else side[p] = sz;
  }
}
