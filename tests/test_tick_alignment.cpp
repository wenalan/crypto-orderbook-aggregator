#include <gtest/gtest.h>
#include "../common/consolidator.h"

static OrderBook ob_from_pairs(const std::vector<std::pair<double,double>>& bids,
                               const std::vector<std::pair<double,double>>& asks) {
  OrderBook ob;
  for (auto [p,s] : bids) ob.bids[p] = s;
  for (auto [p,s] : asks) ob.asks[p] = s;
  return ob;
}

TEST(TickAlignTest, FloorBidsAndCeilAsksWithEpsilons) {
  ConsolidationCfg cfg; cfg.tick = 0.5; cfg.topN = 50;

  auto a = ob_from_pairs({{99.999999999,1.0},{100.0,1.0}}, {{101.0000000001,2.0},{101.5,1.0}});
  auto b = ob_from_pairs({{100.25,3.0}}, {{101.249999999,4.0}});
  auto merged = consolidate({a, b}, cfg);
  ASSERT_GE(merged.bids.size(), 2u);
  
  auto itb = merged.bids.begin();
  EXPECT_DOUBLE_EQ(itb->first, 100.0); EXPECT_DOUBLE_EQ(itb->second, 4.0); ++itb;
  EXPECT_DOUBLE_EQ(itb->first, 99.5);  EXPECT_DOUBLE_EQ(itb->second, 1.0);
  ASSERT_FALSE(merged.asks.empty());

  auto ita = merged.asks.begin();
  EXPECT_DOUBLE_EQ(ita->first, 101.5);
  EXPECT_DOUBLE_EQ(ita->second, 7.0);
}

TEST(TickAlignTest, ExactBoundaryStability) {
  ConsolidationCfg cfg; cfg.tick = 0.5; cfg.topN = 50;

  auto a = ob_from_pairs({{101.0,1.0},{100.5,2.0}}, {{102.0,3.0},{102.5,4.0}});
  auto merged = consolidate({a}, cfg);
  ASSERT_GE(merged.bids.size(), 2u);

  auto itb = merged.bids.begin();
  EXPECT_DOUBLE_EQ(itb->first, 101.0); EXPECT_DOUBLE_EQ(itb->second, 1.0); ++itb;
  EXPECT_DOUBLE_EQ(itb->first, 100.5); EXPECT_DOUBLE_EQ(itb->second, 2.0);
  ASSERT_GE(merged.asks.size(), 2u);

  auto ita = merged.asks.begin();
  EXPECT_DOUBLE_EQ(ita->first, 102.0); EXPECT_DOUBLE_EQ(ita->second, 3.0); ++ita;
  EXPECT_DOUBLE_EQ(ita->first, 102.5); EXPECT_DOUBLE_EQ(ita->second, 4.0);
}

TEST(TickAlignTest, TopNAppliesAfterAlignment) {
  ConsolidationCfg cfg; cfg.tick = 0.5; cfg.topN = 1;

  auto a = ob_from_pairs({{100.49,1.0},{99.99,2.0}}, {{101.01,1.0},{101.51,2.0}});
  auto merged = consolidate({a}, cfg);
  ASSERT_EQ(merged.bids.size(), 1u);
  ASSERT_EQ(merged.asks.size(), 1u);
  EXPECT_DOUBLE_EQ(merged.bids.begin()->first, 100.0);
  EXPECT_DOUBLE_EQ(merged.asks.begin()->first, 101.5);
}
