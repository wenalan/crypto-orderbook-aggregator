#include <grpcpp/grpcpp.h>
#include "bookfeed.grpc.pb.h"
#include <iostream>
#include <iomanip>

#include "../../common/bands.h"

int main() {
  auto channel = grpc::CreateChannel("aggregator:50051", grpc::InsecureChannelCredentials());
  auto stub = bookfeed::BookFeed::NewStub(channel);

  bookfeed::SubscribeRequest req; 
  req.set_symbol("BTCUSDT");
  grpc::ClientContext ctx;

  std::unique_ptr<grpc::ClientReader<bookfeed::ConsolidatedBook>> reader(
      stub->StreamBook(&ctx, req));

  auto _flags = std::cout.flags();
  auto _prec  = std::cout.precision();
  std::cout.setf(std::ios::fixed);
  std::cout << std::setprecision(6);

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

  std::cout.flags(_flags);
  std::cout.precision(_prec);

  grpc::Status status = reader->Finish();
  if (!status.ok()) {
    std::cerr << "Stream ended: " << status.error_message() << std::endl;
    return 1;
  }
  return 0;
}

