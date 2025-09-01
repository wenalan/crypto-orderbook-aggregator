#include <gtest/gtest.h>
#define private public
#include "../aggregator/kraken_adapter.h"
#undef private

TEST(AdapterKrakenTest, SnapshotUpdateAndZeroQuantityDelete) {
  KrakenAdapter adp("BTCUSDT");
  OrderBook book;

  const std::string snap = R"({
    "channel":"book",
    "type":"snapshot",
    "data":[{
      "bids":[{"price":"100.0","qty":"1.0"},{"price":"99.5","qty":"2.0"}],
      "asks":[{"price":"100.5","qty":"3.0"}]
    }]
  })";
  adp.apply_ws_message(snap, book);
  ASSERT_EQ(book.bids.size(), 2u);
  ASSERT_EQ(book.asks.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->first, 100.0);
  EXPECT_DOUBLE_EQ(book.asks.begin()->first, 100.5);

  const std::string upd1 = R"({
    "channel":"book","type":"update",
    "data":[{ "bids":[{"price":"100.0","qty":"0"}], "asks":[{"price":"100.5","qty":"1.0"}] }]
  })";
  adp.apply_ws_message(upd1, book);
  ASSERT_EQ(book.bids.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->first, 99.5);
  EXPECT_DOUBLE_EQ(book.asks.begin()->second, 1.0);

  const std::string upd2 = R"({
    "channel":"book","type":"update",
    "data":[{ "bids":[{"price":"99.5","qty":"2.5"},{"price":"99.5","qty":"1.1"}] }]
  })";
  adp.apply_ws_message(upd2, book);
  ASSERT_EQ(book.bids.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->second, 1.1);
}
