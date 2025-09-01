#include <gtest/gtest.h>
#include "../common/consolidator.h"

TEST(ConsolidatorTest, MergeAcrossSourcesWithTickAndTopN) {
  ConsolidationCfg cfg;
  cfg.tick = 0.5;
  cfg.topN = 2;

  OrderBook a, b;
  a.bids[100.0] = 1.0;
  a.bids[99.9]  = 2.0;      // -> 99.5 after floor_to_tick for bids
  a.asks[100.5] = 3.0;
  a.asks[100.6] = 1.0;      // -> 101.0 after ceil_to_tick for asks

  b.bids[100.0] = 2.0;
  b.bids[99.4]  = 1.0;      // -> 99.0 after floor_to_tick
  b.asks[100.5] = 1.0;
  b.asks[101.1] = 1.0;      // -> 101.5 after ceil_to_tick

  auto merged = consolidate({a, b}, cfg);

  ASSERT_GE(merged.bids.size(), 2u);
  auto itb = merged.bids.begin();
  EXPECT_DOUBLE_EQ(itb->first, 100.0);
  EXPECT_DOUBLE_EQ(itb->second, 3.0);
  
  ++itb;
  EXPECT_DOUBLE_EQ(itb->first, 99.5);
  EXPECT_DOUBLE_EQ(itb->second, 2.0);

  ++itb; 
  EXPECT_TRUE(itb == merged.bids.end());

  ASSERT_GE(merged.asks.size(), 2u);
  auto ita = merged.asks.begin();
  EXPECT_DOUBLE_EQ(ita->first, 100.5);
  EXPECT_DOUBLE_EQ(ita->second, 4.0);
  
  ++ita;
  EXPECT_DOUBLE_EQ(ita->first, 101.0);
  EXPECT_DOUBLE_EQ(ita->second, 1.0);

  ++ita;
  EXPECT_TRUE(ita == merged.asks.end());
}
