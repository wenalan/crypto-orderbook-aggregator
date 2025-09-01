
#pragma once
#include "bookfeed.pb.h"
#include <cmath>

/*
  - The ConsolidatedBook passed to compute_bbo() is assumed to be produced by
    the OrderBook->ConsolidatedBook serialization pipeline, which guarantees:
      (1) bids sorted high->low, asks sorted low->high
      (2) same-price levels are aggregated (no duplicates)
      (3) size > 0, price/size are finite
      (4) TopN applied upstream if needed
  - Under this contract, compute_bbo() may simply read the first level
    on each side as the best price/size.
*/
// Compute BBO from a ConsolidatedBook. Returns false if any side missing.
inline bool compute_bbo(const bookfeed::ConsolidatedBook& book,
                        double* best_bid, double* bid_sz,
                        double* best_ask, double* ask_sz) {
  if (book.bids_size() == 0 || book.asks_size() == 0) return false;
  const auto& bb = book.bids(0);
  const auto& ba = book.asks(0);

  if (best_bid) *best_bid = bb.price();
  if (bid_sz) *bid_sz = bb.size();
  if (best_ask) *best_ask = ba.price();
  if (ask_sz) *ask_sz = ba.size();
  return true;
}



// Price band
// Consume asks up to price ceiling (inclusive). Returns VWAP; out_qty set to total qty used.
inline double vwap_asks_to_price(const google::protobuf::RepeatedPtrField<bookfeed::Level>& asks,
                                 double up_to_price_inclusive,
                                 double* out_qty) {
  double qty = 0.0, notional = 0.0;
  for (const auto& lv : asks) {
    if (lv.price() > up_to_price_inclusive) break;
    qty += lv.size();
    notional += lv.size() * lv.price();
  }
  if (out_qty) *out_qty = qty;
  return qty > 0 ? notional / qty : 0.0;
}

// Consume bids down to price floor (inclusive). Returns VWAP; out_qty set to total qty used.
inline double vwap_bids_to_price(const google::protobuf::RepeatedPtrField<bookfeed::Level>& bids,
                                 double down_to_price_inclusive,
                                 double* out_qty) {
  double qty = 0.0, notional = 0.0;
  for (const auto& lv : bids) {
    if (lv.price() < down_to_price_inclusive) break;
    qty += lv.size();
    notional += lv.size() * lv.price();
  }
  if (out_qty) *out_qty = qty;
  return qty > 0 ? notional / qty : 0.0;
}



// Volume band (buying from asks) for a target notional in quote currency.
// Fully consume lower price levels first, last level is proportionally filled.
inline double vwap_for_notional_asks(const google::protobuf::RepeatedPtrField<bookfeed::Level>& asks,
                                     double target_notional, double* out_qty) {
  if (out_qty) *out_qty = 0.0;
  if (target_notional <= 0.0) return 0.0;

  double qty = 0.0, notional = 0.0;
  for (const auto& lv : asks) {
    double level_notional = lv.price() * lv.size();
    if (notional + level_notional >= target_notional) {
      double remain = target_notional - notional;
      double add_qty = remain / lv.price();
      qty += add_qty;
      notional = target_notional;
      if (out_qty) *out_qty = qty;
      return qty > 0 ? (notional / qty) : 0.0;
    } else {
      qty += lv.size();
      notional += level_notional;
    }
  }

  if (out_qty) *out_qty = qty;
  return qty > 0 ? notional / qty : 0.0;
}

inline double vwap_for_notional_bids(const google::protobuf::RepeatedPtrField<bookfeed::Level>& bids,
                                     double target_notional, double* out_qty) {
  if (out_qty) *out_qty = 0.0;
  if (target_notional <= 0.0) return 0.0;

  double qty = 0.0, notional = 0.0;
  for (const auto& lv : bids) {
    double level_notional = lv.price() * lv.size();
    if (notional + level_notional >= target_notional) {
      double remain = target_notional - notional;
      double add_qty = remain / lv.price();
      qty += add_qty;
      notional = target_notional;
      if (out_qty) *out_qty = qty;
      return qty > 0 ? (notional / qty) : 0.0;
    } else {
      qty += lv.size();
      notional += level_notional;
    }
  }

  if (out_qty) *out_qty = qty;
  return qty > 0 ? notional / qty : 0.0;
}

