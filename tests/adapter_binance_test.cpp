#include <gtest/gtest.h>
#define private public
#include "../aggregator/binance_adapter.h"
#undef private

TEST(AdapterBinanceTest, SequenceChainAndDeletion) {
  BinanceAdapter adp("BTCUSDT");
  OrderBook book;
  long long last = 100;

  const std::string upd1 = R"({
    "U":101,"u":102,
    "b":[["100.0","1.5"],["99.5","2.0"]],
    "a":[["100.5","3.0"]]
  })";
  adp.apply_update_json(upd1, last, book);
  EXPECT_EQ(last, 102);
  ASSERT_EQ(book.bids.size(), 2u);
  ASSERT_EQ(book.asks.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->first, 100.0);
  EXPECT_DOUBLE_EQ(book.bids.begin()->second, 1.5);
  EXPECT_DOUBLE_EQ(book.asks.begin()->first, 100.5);
  EXPECT_DOUBLE_EQ(book.asks.begin()->second, 3.0);

  const std::string upd2 = R"({
    "U":103,"u":104,
    "b":[["100.0","0"]],
    "a":[["100.5","1.0"]]
  })";
  adp.apply_update_json(upd2, last, book);
  EXPECT_EQ(last, 104);
  ASSERT_EQ(book.bids.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->first, 99.5);
  EXPECT_DOUBLE_EQ(book.bids.begin()->second, 2.0);
  ASSERT_EQ(book.asks.size(), 1u);
  EXPECT_DOUBLE_EQ(book.asks.begin()->second, 1.0);

  const std::string gap = R"({"U":200,"u":201,"b":[],"a":[]})";
  EXPECT_THROW(adp.apply_update_json(gap, last, book), std::runtime_error);
}
