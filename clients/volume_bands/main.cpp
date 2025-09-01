#include <grpcpp/grpcpp.h>
#include "bookfeed.grpc.pb.h"
#include <array>
#include <iostream>
#include <algorithm>
#include "../../common/bands.h"

int main() {
  auto channel = grpc::CreateChannel("aggregator:50051", grpc::InsecureChannelCredentials());
  auto stub = bookfeed::BookFeed::NewStub(channel);

  bookfeed::SubscribeRequest req; req.set_symbol("BTCUSDT");
  grpc::ClientContext ctx;
  auto reader = stub->StreamBook(&ctx, req);

  std::array<double,5> bands{1e6, 5e6, 1e7, 2.5e7, 5e7};
  bookfeed::ConsolidatedBook book;
  while (reader->Read(&book)) {
    for (double N : bands) {
      double qty = 0.0;
      double vwap = vwap_for_notional_asks(book.asks(), N, &qty);
      const double filled = qty * vwap;
      std::cout << "ts=" << book.ts_ms()
                << " notional=" << N
                << " qty=" << qty
                << " vwap=" << vwap
                << " filled_notional=" << filled 
                << std::endl;
    }
  }
  auto status = reader->Finish();
  if (!status.ok()) {
    std::cerr << "Stream ended: " << status.error_message() << std::endl;
  }
  return 0;
}
