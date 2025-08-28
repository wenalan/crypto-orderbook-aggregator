#include <gtest/gtest.h>
#include "../common/bands.h"

TEST(PriceBandsTest, UpAndDownBpsSelectionAndVWAP) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(101.0); b->set_size(1.0); }
  { auto* b = book.add_bids(); b->set_price(100.5); b->set_size(2.0); }
  { auto* b = book.add_bids(); b->set_price(100.0); b->set_size(3.0); }
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(3.0); }
  { auto* a = book.add_asks(); a->set_price(101.5); a->set_size(4.0); }
  { auto* a = book.add_asks(); a->set_price(102.0); a->set_size(5.0); }

  double best_bid=0, bid_sz=0, best_ask=0, ask_sz=0;
  ASSERT_TRUE(compute_bbo(book, &best_bid, &bid_sz, &best_ask, &ask_sz));

  const double mid = 0.5 * (best_bid + best_ask);

  double qty = 0.0;
  double up = vwap_asks_to_price(book.asks(), mid * 1.005, &qty);
  EXPECT_GT(up, 0.0);

  double dn = vwap_bids_to_price(book.bids(), mid * 0.995, &qty);
  EXPECT_GT(dn, 0.0);
}
