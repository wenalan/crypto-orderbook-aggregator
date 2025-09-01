#include <gtest/gtest.h>
#include "../common/order_book.h"

TEST(OrderBookTest, ApplyDeltasBidsAddOverwriteDelete) {
  std::map<double,double,std::greater<double>> bids;

  apply_deltas(bids, {{100.0, 1.0}, {99.5, 2.0}});
  ASSERT_EQ(bids.size(), 2u);
  EXPECT_DOUBLE_EQ(bids.begin()->first, 100.0);
  EXPECT_DOUBLE_EQ(bids.begin()->second, 1.0);

  apply_deltas(bids, {{100.0, 3.0}});
  EXPECT_DOUBLE_EQ(bids.begin()->second, 3.0);

  apply_deltas(bids, {{99.5, 0.0}});
  ASSERT_EQ(bids.size(), 1u);
  EXPECT_DOUBLE_EQ(bids.begin()->first, 100.0);
}

TEST(OrderBookTest, ApplyDeltasAsksAddAndDelete) {
  std::map<double,double,std::less<double>> asks;

  apply_deltas(asks, {{100.5, 1.0}, {101.0, 2.0}});
  ASSERT_EQ(asks.size(), 2u);
  EXPECT_DOUBLE_EQ(asks.begin()->first, 100.5);
  EXPECT_DOUBLE_EQ(asks.begin()->second, 1.0);
  
  apply_deltas(asks, {{100.5, 3.0}});
  EXPECT_DOUBLE_EQ(asks.begin()->second, 3.0);

  apply_deltas(asks, {{100.5, 0.0}});
  ASSERT_EQ(asks.size(), 1u);
  EXPECT_DOUBLE_EQ(asks.begin()->first, 101.0);
}
