#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include "../common/bands.h"

TEST(VolumeBandsTest, PartialFillVWAP_Asks) {
  bookfeed::ConsolidatedBook book;
  { auto* a = book.add_asks(); a->set_price(100.0); a->set_size(1.0); }
  { auto* a = book.add_asks(); a->set_price(100.5); a->set_size(2.0); }
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(3.0); }

  double qty = 0.0;
  const double notional = 250.0;
  const double vwap = vwap_for_notional_asks(book.asks(), notional, &qty);

  const double exp_qty = 1.0 + (notional - 1.0 * 100.0) / 100.5;
  const double exp_vwap = notional / exp_qty;
  EXPECT_NEAR(vwap, exp_vwap, 1e-9);
  EXPECT_NEAR(qty, exp_qty, 1e-9);
}

TEST(VolumeBandsTest, FullFillVWAP_Asks) {
  bookfeed::ConsolidatedBook book;
  { auto* a = book.add_asks(); a->set_price(100.0); a->set_size(1.0); }
  { auto* a = book.add_asks(); a->set_price(100.5); a->set_size(2.0); }
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(3.0); }

  double qty = 0.0, notional = 0.0, total = 0.0;
  for (const auto& lv : book.asks()) { 
    notional += lv.price() * lv.size();
    total += lv.size();
  }

  const double vwap = vwap_for_notional_asks(book.asks(), notional, &qty);
  EXPECT_NEAR(vwap, notional/total, 1e-9);
  EXPECT_NEAR(qty, total, 1e-9);
}

TEST(VolumeBandsTest, PartialFillVWAP_Bids) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.9); b->set_size(0.5); }
  { auto* b = book.add_bids(); b->set_price(100.7); b->set_size(1.0); }
  { auto* b = book.add_bids(); b->set_price(100.4); b->set_size(2.0); }

  double qty = 0.0;
  const double notional = 110.7;
  const double vwap = vwap_for_notional_bids(book.bids(), notional, &qty);

  const double exp_qty  = 0.5 + (notional - 0.5 * 100.9) / 100.7;
  const double exp_vwap = notional / exp_qty;
  EXPECT_NEAR(vwap, exp_vwap, 1e-9);
  EXPECT_NEAR(qty,  exp_qty,  1e-9);
}

TEST(VolumeBandsTest, FullFillVWAP_Bids) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.9); b->set_size(0.5); }
  { auto* b = book.add_bids(); b->set_price(100.7); b->set_size(1.0); }
  { auto* b = book.add_bids(); b->set_price(100.4); b->set_size(2.0); }

  double qty = 0.0, notional = 0.0, total = 0.0;
  for (const auto& lv : book.bids()) {
    notional += lv.price() * lv.size();
    total += lv.size();
  }

  const double vwap = vwap_for_notional_bids(book.bids(), notional, &qty);
  EXPECT_NEAR(vwap, notional/total, 1e-9);
  EXPECT_NEAR(qty, total, 1e-9);
}

TEST(VolumeBandsTest, EdgeCases_Asks) {
  bookfeed::ConsolidatedBook book;
  { auto* a = book.add_asks(); a->set_price(100.0); a->set_size(1.0); }
  double qty=123.0;
  EXPECT_DOUBLE_EQ(vwap_for_notional_asks(book.asks(), 0.0, &qty), 0.0);
  EXPECT_DOUBLE_EQ(qty, 0.0);
}

TEST(VolumeBandsTest, EdgeCases_Bids) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.0); b->set_size(1.0); }
  double qty=123.0;
  EXPECT_DOUBLE_EQ(vwap_for_notional_bids(book.bids(), 0.0, &qty), 0.0);
  EXPECT_DOUBLE_EQ(qty, 0.0);
}
