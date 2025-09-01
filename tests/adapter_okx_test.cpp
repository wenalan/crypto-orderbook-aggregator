#include <gtest/gtest.h>
#define private public
#include "../aggregator/okx_adapter.h"
#undef private

TEST(AdapterOKXTest, SnapshotDiffZeroDeleteAndDuplicateOverwrite) {
  OKXAdapter adp("BTC-USDT");
  OrderBook book;

  const std::string snap = R"({
    "arg":  { "channel":"books", "instId":"BTC-USDT" },
    "data": [{
      "action": "snapshot",
      "prevSeqId": -1,
      "seqId": 100,
      "bids": [["100","1.0"], ["99.5","2.0"]],
      "asks": [["100.5","3.0"]]
    }]
  })";
  adp.apply_update_json(snap, book);
  ASSERT_EQ(book.bids.size(), 2u);

  const std::string diff1 = R"({
    "data": [{
      "prevSeqId": 100,
      "seqId": 101,
      "b": [["100","0"]],
      "a": [["100.5","1.0"]]
    }]
  })";
  adp.apply_update_json(diff1, book);
  ASSERT_EQ(book.bids.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->first, 99.5);
  EXPECT_DOUBLE_EQ(book.asks.begin()->second, 1.0);

  const std::string diff2 = R"({
    "data": [{
      "prevSeqId": 101,
      "seqId": 102,
      "b": [["99.5","2.5"], ["99.5","1.0"]]
    }]
  })";
  adp.apply_update_json(diff2, book);
  ASSERT_EQ(book.bids.size(), 1u);
  EXPECT_DOUBLE_EQ(book.bids.begin()->first, 99.5);
  EXPECT_DOUBLE_EQ(book.bids.begin()->second, 1.0);
}
