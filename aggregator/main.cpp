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
#include <array>
#include <iomanip>

static constexpr bool debug_mode = true;

class BookFeedService final : public bookfeed::BookFeed::Service {
public:
  BookFeedService() {
    binance_ = std::make_unique<BinanceAdapter>("BTCUSDT");
    binance_->start([this](const OrderBook& b, const char* venue){
        onVenueUpdate(b, venue); });
    
    okx_ = std::make_unique<OKXAdapter>("BTC-USDT");
    okx_->start([this](const OrderBook& b, const char* venue) { 
        onVenueUpdate(b, venue); });

    kraken_ = std::make_unique<KrakenAdapter>("BTC-USDT");
    kraken_->start([this](const OrderBook& b, const char* venue){ 
        onVenueUpdate(b, venue); });
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

        // --- Debug: print per-venue BBOs and source count ---
        if constexpr (debug_mode) {
          auto bb = [](const OrderBook& ob){
            double bid_p=0.0, bid_s=0.0, ask_p=0.0, ask_s=0.0;
            if (!ob.bids.empty()) { 
              bid_p = ob.bids.begin()->first;
              bid_s = ob.bids.begin()->second;
            }
            if (!ob.asks.empty()) { 
              ask_p = ob.asks.begin()->first;
              ask_s = ob.asks.begin()->second;
            }
            return std::array<double,4>{bid_p,bid_s,ask_p,ask_s};
          };
          
          auto b1 = bb(book_binance_);
          auto b2 = bb(book_okx_);
          auto b3 = bb(book_kraken_);

          int sources = 0;
          if (!book_binance_.bids.empty() || !book_binance_.asks.empty()) ++sources;
          if (!book_okx_.bids.empty()     || !book_okx_.asks.empty())     ++sources;
          if (!book_kraken_.bids.empty()  || !book_kraken_.asks.empty())  ++sources;

          auto _flags = std::cout.flags();
          auto _prec  = std::cout.precision();
          std::cout.setf(std::ios::fixed);
          std::cout << std::setprecision(6);
          std::cout << "[AGG] src BBOs [bid/ask]  BINANCE " << b1[0] << "@" << b1[1] << "/" << b1[2] << "@" << b1[3]
                    << "  OKX " << b2[0] << "@" << b2[1] << "/" << b2[2] << "@" << b2[3]
                    << "  KRAKEN " << b3[0] << "@" << b3[1] << "/" << b3[2] << "@" << b3[3]
                    << "  (sources=" << sources << "/3)" << std::endl;
          std::cout.flags(_flags);
          std::cout.precision(_prec);
        }
        // --- End debug ---

        auto merged = consolidate({book_binance_, book_okx_, book_kraken_}, cfg_);

        // --- Debug: print merged BBO ---
        if constexpr (debug_mode) {
          if (!merged.bids.empty() || !merged.asks.empty()) {
            double mbp=0.0, mbs=0.0, map=0.0, mas=0.0;
            if (!merged.bids.empty()) { 
              mbp = merged.bids.begin()->first;
              mbs = merged.bids.begin()->second;
            }
            if (!merged.asks.empty()) { 
              map = merged.asks.begin()->first;
              mas = merged.asks.begin()->second;
            }

            auto _flags = std::cout.flags();
            auto _prec  = std::cout.precision();
            std::cout.setf(std::ios::fixed);
            std::cout << std::setprecision(6);
            std::cout << "[AGG] merged BBO [bid/ask] " << mbp << "@" 
              << mbs << " / " << map << "@" << mas << std::endl;
            std::cout.flags(_flags);
            std::cout.precision(_prec);
          }
        }
        // --- End debug ---

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();

        msg.set_ts_ms(static_cast<int64_t>(now_ms));
        for (auto& [p,s] : merged.bids) { 
          auto* lv = msg.add_bids(); lv->set_price(p); lv->set_size(s);
        }
        for (auto& [p,s] : merged.asks) { 
          auto* lv = msg.add_asks(); lv->set_price(p); lv->set_size(s);
        }
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

  std::mutex mu_;
  OrderBook book_binance_, book_okx_, book_kraken_;
  ConsolidationCfg cfg_{0.1, 200};

  void onVenueUpdate(const OrderBook& b, const char* venue) {
    std::lock_guard<std::mutex> lk(mu_);
    if (std::string(venue)=="BINANCE") book_binance_ = b;
    else if (std::string(venue)=="OKX") book_okx_ = b;
    else if (std::string(venue)=="KRAKEN") book_kraken_ = b;
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
