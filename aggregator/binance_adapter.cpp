#include "binance_adapter.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ssl/context.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using json = nlohmann::json;

static std::string to_lower(std::string s){ for (auto& c: s) c=std::tolower(c); return s; }

BinanceAdapter::BinanceAdapter(std::string symbol) : AdapterBase(std::move(symbol)){}
BinanceAdapter::~BinanceAdapter(){ stop(); }


bool BinanceAdapter::fetch_snapshot(
    OrderBook& out_book, long long& last_update_id, std::string& err) {
  try {
    boost::asio::io_context ioc;
    ssl::context ctx{ssl::context::tls_client};
    ctx.set_default_verify_paths();

    tcp::resolver resolver{ioc};
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream{ioc, ctx};

    auto const host = "api.binance.com";
    auto const port = "443";
    auto const target = std::string("/api/v3/depth?symbol=") + symbol_ + "&limit=1000";

    auto const results = resolver.resolve(host, port);
    boost::beast::get_lowest_layer(stream).connect(results);
    if (! SSL_set_tlsext_host_name(stream.native_handle(), host)) {
      throw std::runtime_error("SNI set failed");
    }
    stream.handshake(ssl::stream_base::client);

    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "beast");
    http::write(stream, req);

    boost::beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    boost::system::error_code ec;
    stream.shutdown(ec);

    if (res.result() != http::status::ok) {
      err = "HTTP " + std::to_string((int)res.result());
      return false;
    }

    auto j = json::parse(res.body());
    last_update_id = j.at("lastUpdateId").get<long long>();
    out_book.bids.clear(); out_book.asks.clear();

    for (auto& lvl : j["bids"]) {
      double p = std::stod(lvl[0].get<std::string>());
      double s = std::stod(lvl[1].get<std::string>());
      if (s>0) out_book.bids[p] = s;
    }
    
    for (auto& lvl : j["asks"]) {
      double p = std::stod(lvl[0].get<std::string>());
      double s = std::stod(lvl[1].get<std::string>());
      if (s>0) out_book.asks[p] = s;
    }
    return true;
  } catch (const std::exception& e) { 
      err = e.what();
      return false;
  }
}

void BinanceAdapter::apply_update_json(
    const std::string& payload, long long& last_update_id, OrderBook& book) {
  auto j = json::parse(payload);
  long long U = j.at("U").get<long long>();
  long long u = j.at("u").get<long long>();
  long long pu = j.contains("pu") ? j["pu"].get<long long>() : 0LL;

  if (u <= last_update_id) return;

  // Accept if bridged by U/u window or by pu linkage; otherwise real gap.
  bool bridged_window = (U <= last_update_id + 1 && last_update_id + 1 <= u);
  bool bridged_pu = (pu == last_update_id && pu != 0);
  if (!(bridged_window || bridged_pu)) {
    throw std::runtime_error("sequence gap; need resnapshot");
  }

  std::vector<std::pair<double,double>> bid_d, ask_d;

  for (auto& lvl: j["b"]) {
    double p = std::stod(lvl[0].get<std::string>());
    double s = std::stod(lvl[1].get<std::string>());
    bid_d.emplace_back(p, s);
  }

  for (auto& lvl: j["a"]) {
    double p = std::stod(lvl[0].get<std::string>());
    double s = std::stod(lvl[1].get<std::string>());
    ask_d.emplace_back(p, s);
  }

  apply_deltas(book.bids, bid_d);
  apply_deltas(book.asks, ask_d);
  last_update_id = u;
}

void BinanceAdapter::run(Callback cb) {
  using namespace std::chrono_literals;

  while (running()) {
    try {
      OrderBook book;
      long long last_id = 0;

      // 1) connect WS first
      boost::asio::io_context ioc;
      ssl::context ctx{ssl::context::tls_client};
      ctx.set_default_verify_paths();

      tcp::resolver resolver{ioc};
      boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>> ws{ioc, ctx};

      auto const host = "stream.binance.com";
      auto const port = "9443";
      auto const path = std::string("/ws/") + to_lower(symbol_) + "@depth@100ms";

      auto const results = resolver.resolve(host, port);
      boost::beast::get_lowest_layer(ws).connect(results);
      if (! SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host)) {
        throw std::runtime_error("SNI set failed");
      }
      ws.next_layer().handshake(ssl::stream_base::client);
      ws.handshake(host, path);

      // 2) HTTP snapshot
      std::string err;
      if (!fetch_snapshot(book, last_id, err)) {
        std::cerr << "[BINANCE] snapshot error: " << err << std::endl;
        boost::system::error_code ec;
        ws.close(boost::beast::websocket::close_code::normal, ec);
        std::this_thread::sleep_for(1s);
        continue;
      }

      // 3) Bridge buffered updates from WS using U/u or pu
      boost::beast::flat_buffer buffer;
      for (;;) {
        buffer.clear();
        ws.read(buffer);
        std::string payload = boost::beast::buffers_to_string(buffer.data());
        try {
          auto before = last_id;
          apply_update_json(payload, last_id, book);
          if (last_id != before) { 
            cb(book, "BINANCE");
            break;
          }
        } catch (const std::exception& e) {
          continue;
        }
      }

      // 4) Steady-state WS consumption
      for (;;) {
        buffer.clear();
        ws.read(buffer);
        std::string payload = boost::beast::buffers_to_string(buffer.data());

        try {
          apply_update_json(payload, last_id, book);
          cb(book, "BINANCE");
        } catch (const std::exception& e) {
          std::cerr << "[BINANCE] " << e.what() << " — resyncing..." << std::endl;
          break;
        }
      }

      // graceful close
      boost::system::error_code ec;
      ws.close(boost::beast::websocket::close_code::normal, ec);
    } catch (const std::exception& e) {
      std::cerr << "[BINANCE] exception: " << e.what() << std::endl;
    }

    std::this_thread::sleep_for(1s); 
  }
}

