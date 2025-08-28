#include <gtest/gtest.h>
#include "../common/bands.h"

TEST(VolumeBandsTest, PartialFillVWAP) {
  bookfeed::ConsolidatedBook book;
  { auto* a = book.add_asks(); a->set_price(100.0); a->set_size(1.0); }
  { auto* a = book.add_asks(); a->set_price(100.5); a->set_size(2.0); }
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(3.0); }

  double qty = 0.0;
  const double notional = 100.0 * 2.5;
  double v = vwap_for_notional_asks(book.asks(), notional, &qty);

  EXPECT_GT(v, 0.0);
  EXPECT_GT(qty, 0.0);
}
