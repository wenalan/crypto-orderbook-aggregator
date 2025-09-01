#include <gtest/gtest.h>
#include "../common/bands.h"

TEST(BBOTest, EmptyBook_ReturnsFalse) {
  bookfeed::ConsolidatedBook book;
  double bb=0, bs=0, ba=0, as=0;
  EXPECT_FALSE(compute_bbo(book, &bb, &bs, &ba, &as));
}

TEST(BBOTest, OneSideOnly_ReturnsFalse) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.0); b->set_size(1.0); }
  double bb=0, bs=0, ba=0, as=0;
  EXPECT_FALSE(compute_bbo(book, &bb, &bs, &ba, &as));

  book.clear_bids();
  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(2.0); }
  EXPECT_FALSE(compute_bbo(book, &bb, &bs, &ba, &as));
}

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
TEST(BBOTest, BasicTrustedInput) {
  bookfeed::ConsolidatedBook book;
  { auto* b = book.add_bids(); b->set_price(100.9); b->set_size(2.0); }
  { auto* b = book.add_bids(); b->set_price(100.5); b->set_size(1.0); }
  { auto* b = book.add_bids(); b->set_price(100.7); b->set_size(3.0); }

  { auto* a = book.add_asks(); a->set_price(101.0); a->set_size(2.0); }
  { auto* a = book.add_asks(); a->set_price(101.2); a->set_size(3.0); }
  { auto* a = book.add_asks(); a->set_price(101.3); a->set_size(1.0); }

  double bb=0, bs=0, ba=0, as=0;
  ASSERT_TRUE(compute_bbo(book, &bb, &bs, &ba, &as));
  EXPECT_DOUBLE_EQ(bb, 100.9);
  EXPECT_DOUBLE_EQ(ba, 101.0);
}

