#include <gtest/gtest.h>
#include "../common/bands.h"

TEST(PriceBandsTest, UpAndDownBpsSelectionAndVWAP) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.9); b->set_size(1.0); }
  { auto* b = book.add_bids(); b->set_price(100.5); b->set_size(2.0); }
  { auto* b = book.add_bids(); b->set_price(100.0); b->set_size(3.0); }
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(3.0); }
  { auto* a = book.add_asks(); a->set_price(101.5); a->set_size(4.0); }
  { auto* a = book.add_asks(); a->set_price(102.0); a->set_size(5.0); }

  const double best_bid = 100.9, best_ask = 101.0;
  const double mid = 0.5 * (best_bid + best_ask);  // 100.95
  const double up_target = mid * 1.005;            // 101.45475
  const double dn_target = mid * 0.995;            // 100.44525

  double qty = 0.0;

  // with a limit of <= 101.45475, only the 101.0 level is taken
  const double up_vwap = vwap_asks_to_price(book.asks(), up_target, &qty);
  EXPECT_NEAR(up_vwap, 101.0, 1e-9);
  EXPECT_NEAR(qty, 3.0, 1e-9);

  qty = 0.0;
  // with a limit of >= 100.44525, it will take 1 @ 100.9 and 2 @ 100.5
  const double dn_vwap = vwap_bids_to_price(book.bids(), dn_target, &qty);
  EXPECT_NEAR(dn_vwap, 100.63333333333333, 1e-9);
  EXPECT_NEAR(qty, 3.0, 1e-9);
}

// Boundary test (asks): confirm the limit is inclusive (<= limit).
TEST(PriceBandsTest, LimitInclusiveSemantics_Asks) {
  bookfeed::ConsolidatedBook book;
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(3.0); }
  { auto* a = book.add_asks(); a->set_price(101.5); a->set_size(4.0); }

  double qty = 0.0;
  const double vwap = vwap_asks_to_price(book.asks(), 101.5, &qty);

  const double exp_qty  = 3.0 + 4.0;
  const double exp_vwap = (101.0 * 3.0 + 101.5 * 4.0) / exp_qty;
  EXPECT_NEAR(vwap, exp_vwap, 1e-9);
  EXPECT_NEAR(qty, exp_qty, 1e-9);
}

// Boundary test (bids): confirm the limit is inclusive (>= limit).
TEST(PriceBandsTest, LimitInclusiveSemantics_Bids) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.9); b->set_size(1.0); }
  { auto* b = book.add_bids(); b->set_price(100.5); b->set_size(2.0); }

  double qty = 0.0;
  const double vwap = vwap_bids_to_price(book.bids(), 100.5, &qty);

  const double exp_qty  = 1.0 + 2.0;
  const double exp_vwap = (100.9 * 1.0 + 100.5 * 2.0) / exp_qty;
  EXPECT_NEAR(vwap, exp_vwap, 1e-9);
  EXPECT_NEAR(qty, exp_qty, 1e-9);
}
