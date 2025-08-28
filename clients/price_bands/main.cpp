#include <grpcpp/grpcpp.h>
#include "bookfeed.grpc.pb.h"
#include <array>
#include <iostream>
#include <cmath>
#include "../../common/bands.h"

int main() {
  auto channel = grpc::CreateChannel("aggregator:50051", grpc::InsecureChannelCredentials());
  auto stub = bookfeed::BookFeed::NewStub(channel);

  bookfeed::SubscribeRequest req; req.set_symbol("BTCUSDT");
  grpc::ClientContext ctx;
  auto reader = stub->StreamBook(&ctx, req);

  std::array<int,5> bps{50,100,200,500,1000};
  bookfeed::ConsolidatedBook book;
  while (reader->Read(&book)) {
    double bb=0, ba=0, bs=0, asz=0;
    if (!compute_bbo(book, &bb, &bs, &ba, &asz)) {
      std::cout << "ts="<< book.ts_ms() <<" BBO=NA"<< std::endl;
      continue;
    }
    double mid = 0.5*(bb + ba);
    for (int bp : bps) {
      double up_px = mid * (1.0 + bp * 1e-4);
      double dn_px = mid * (1.0 - bp * 1e-4);
      double qty_up = 0.0, qty_dn = 0.0;
      double vwap_up = vwap_asks_to_price(book.asks(), up_px, &qty_up);
      double vwap_dn = vwap_bids_to_price(book.bids(), dn_px, &qty_dn);
      std::cout << "ts="<< book.ts_ms()
                << " +"<< bp <<"bps qty="<< qty_up <<" vwap="<< vwap_up
                << " | -"<< bp <<"bps qty="<< qty_dn <<" vwap="<< vwap_dn
                << std::endl;
    }
  }
  auto status = reader->Finish();
  if (!status.ok()) {
    std::cerr << "Stream ended: " << status.error_message() << std::endl;
  }
  return 0;
}
