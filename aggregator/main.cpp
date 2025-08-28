#include <grpcpp/grpcpp.h>
#include "bookfeed.grpc.pb.h"

#include "../common/order_book.h"
#include "../common/consolidator.h"
#include "binance_adapter.h"
#include "okx_adapter.h"
#include "kraken_adapter.h"

#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>

class BookFeedService final : public bookfeed::BookFeed::Service {
public:
  BookFeedService() {
    binance_ = std::make_unique<BinanceAdapter>("BTCUSDT");
    binance_->start([this](const OrderBook& b, const char* venue){ onVenueUpdate(b, venue); });
    
    okx_ = std::make_unique<OKXAdapter>("BTC-USDT");
    okx_->start([this](const OrderBook& b, const char* venue) { onVenueUpdate(b, venue); });

    kraken_ = std::make_unique<KrakenAdapter>("BTC-USDT");
    kraken_->start([this](const OrderBook& b, const char* venue){ onVenueUpdate(b, venue); });
  }

  ~BookFeedService() override {
    if (binance_) binance_->stop();
    if (okx_) okx_->stop();
    if (kraken_) kraken_->stop();
  }

  grpc::Status StreamBook(grpc::ServerContext* ctx,
                          const bookfeed::SubscribeRequest*,
                          grpc::ServerWriter<bookfeed::ConsolidatedBook>* writer) override {
    while (!ctx->IsCancelled()) {
      bookfeed::ConsolidatedBook msg;
      {
        std::lock_guard<std::mutex> lk(mu_);
        auto merged = consolidate({book_binance_, book_okx_, book_kraken_}, cfg_);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count();

        msg.set_ts_ms(static_cast<int64_t>(now_ms));
        for (auto& [p,s] : merged.bids) { auto* lv = msg.add_bids(); lv->set_price(p); lv->set_size(s); }
        for (auto& [p,s] : merged.asks) { auto* lv = msg.add_asks(); lv->set_price(p); lv->set_size(s); }
      }
      if (!writer->Write(msg)) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return grpc::Status::OK;
  }

private:
  std::unique_ptr<BinanceAdapter> binance_;
  std::unique_ptr<OKXAdapter> okx_;
  std::unique_ptr<KrakenAdapter> kraken_;

  std::mutex   mu_;
  OrderBook    book_binance_, book_okx_, book_kraken_;
  ConsolidationCfg cfg_{0.1, 200};

  void onVenueUpdate(const OrderBook& b, const char* venue){
    std::lock_guard<std::mutex> lk(mu_);
    if      (std::string(venue)=="BINANCE") book_binance_ = b;
    else if (std::string(venue)=="OKX")     book_okx_     = b;
    else if (std::string(venue)=="KRAKEN")  book_kraken_  = b;
  }
};


int main() {
  const std::string addr = "0.0.0.0:50051";
  BookFeedService svc;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&svc);
  auto server = builder.BuildAndStart();
  std::cout << "Aggregator listening on " << addr << std::endl;
  server->Wait();
  return 0;
}
