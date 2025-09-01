#include <grpcpp/grpcpp.h>
#include "bookfeed.grpc.pb.h"
#include <iostream>
#include "../../common/bands.h"

int main() {
  auto channel = grpc::CreateChannel("aggregator:50051", grpc::InsecureChannelCredentials());
  auto stub = bookfeed::BookFeed::NewStub(channel);

  bookfeed::SubscribeRequest req; 
  req.set_symbol("BTCUSDT");
  grpc::ClientContext ctx;

  std::unique_ptr<grpc::ClientReader<bookfeed::ConsolidatedBook>> reader(
      stub->StreamBook(&ctx, req));

  bookfeed::ConsolidatedBook book;
  while (reader->Read(&book)) {
    double bid = 0, ask = 0, bsz = 0, asz = 0;
    if (!compute_bbo(book, &bid, &bsz, &ask, &asz)) {
      std::cout << "ts=" << book.ts_ms() << " BBO=NA" << std::endl;
      continue;
    }
    std::cout << "ts=" << book.ts_ms()
              << " bid=" << bid << "@" << bsz
              << " ask=" << ask << "@" << asz
              << std::endl;
  }

  grpc::Status status = reader->Finish();
  if (!status.ok()) {
    std::cerr << "Stream ended: " << status.error_message() << std::endl;
    return 1;
  }
  return 0;
}

