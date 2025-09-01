#include "kraken_adapter.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ssl/context.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using json = nlohmann::json;


KrakenAdapter::KrakenAdapter(std::string symbol) : AdapterBase(std::move(symbol)){}
KrakenAdapter::~KrakenAdapter(){ stop(); }


std::string KrakenAdapter::normalize_symbol_for_ws(const std::string& in) const {
  std::string s = in;
  for (auto& c : s) if (c == '-') c = '/';
  return s;
}

bool KrakenAdapter::fetch_snapshot(OrderBook& out_book, std::string& err) {
  try {
    boost::asio::io_context ioc;
    ssl::context ctx{ssl::context::tls_client};
    ctx.set_default_verify_paths();

    tcp::resolver resolver{ioc};
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream{ioc, ctx};

    const std::string host = "api.kraken.com";
    const std::string port = "443";

    const std::string target = "/0/public/Depth?pair=" + symbol_ + "&count=100";

    auto const results = resolver.resolve(host, port);

    bool connected = false;
    for (auto const& entry : results) {
      try {
        boost::beast::get_lowest_layer(stream).connect(entry.endpoint());
        connected = true;
        break;
      } catch (const std::exception& e) {
        std::cerr << "[KRAKEN][REST] tcp connect attempt failed: "
          << e.what() << std::endl;
      }
    }
    if (!connected) { 
      err = "tcp connect failed";
      return false;
    }

    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
      boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                   boost::asio::error::get_ssl_category()};
      throw boost::system::system_error{ec};
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
      err = "HTTP " + std::to_string(static_cast<int>(res.result()));
      return false;
    }

    auto j = json::parse(res.body());
    if (j.contains("error") && !j["error"].empty()) {
      err = "kraken rest error";
      return false;
    }

    if (!j.contains("result") || j["result"].empty()) {
      err = "no result";
      return false;
    }

    auto it = j["result"].begin();
    if (it == j["result"].end()) { 
      err = "empty result";
      return false;
    }
    auto& payload = it.value();

    out_book.bids.clear();
    out_book.asks.clear();

    if (payload.contains("bids")) {
      for (auto& lvl : payload["bids"]) {
        double p = lvl[0].is_string() ?
          std::stod(lvl[0].get<std::string>()) : lvl[0].get<double>();
        double s = lvl[1].is_string() ?
          std::stod(lvl[1].get<std::string>()) : lvl[1].get<double>();
        if (s>0) out_book.bids[p] = s;
      }
    }

    if (payload.contains("asks")) {
      for (auto& lvl : payload["asks"]) {
        double p = lvl[0].is_string() ?
          std::stod(lvl[0].get<std::string>()) : lvl[0].get<double>();
        double s = lvl[1].is_string() ?
          std::stod(lvl[1].get<std::string>()) : lvl[1].get<double>();
        if (s>0) out_book.asks[p] = s;
      }
    }
    return true;
  } catch (const std::exception& e) {
    err = e.what();
    return false;
  }
}

void KrakenAdapter::apply_ws_message(
  const std::string& payload, OrderBook& book) {
  auto j = json::parse(payload);
  if (!j.is_object()) return;

  if (!j.contains("channel")) return;
  if (j["channel"].get<std::string>() != "book") return;
  if (!j.contains("type") || !j.contains("data")) return;

  std::string type = j["type"].get<std::string>();
  auto data = j["data"];
  if (!data.is_array() || data.empty()) return;

  auto book_obj = data[0];

  if (type == "snapshot") {
    book.bids.clear();
    book.asks.clear();
    if (book_obj.contains("bids")) {
      for (auto &lvl: book_obj["bids"]) {
        double p = lvl["price"].is_string() ?
          std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
        double q = lvl["qty"].is_string() ?
          std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
        if (q>0) book.bids[p] = q;
      }
    }
    
    if (book_obj.contains("asks")) {
      for (auto &lvl: book_obj["asks"]) {
        double p = lvl["price"].is_string() ?
          std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
        double q = lvl["qty"].is_string() ?
          std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
        if (q>0) book.asks[p] = q;
      }
    }
    got_ws_snapshot = true;
  } else if (type == "update") {
    if (!got_ws_snapshot) return;

    if (book_obj.contains("bids")) {
      for (auto &lvl: book_obj["bids"]) {
        double p = lvl["price"].is_string() ?
          std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
        double q = lvl["qty"].is_string() ?
          std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
        if (q == 0) book.bids.erase(p);
        else book.bids[p] = q;
      }
    }

    if (book_obj.contains("asks")) {
      for (auto &lvl: book_obj["asks"]) {
        double p = lvl["price"].is_string() ?
          std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
        double q = lvl["qty"].is_string() ?
          std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
        if (q == 0) book.asks.erase(p);
        else book.asks[p] = q;
      }
    }
  }
}

void KrakenAdapter::run(Callback cb) {
  using namespace std::chrono_literals;

  while (running()) {
    try {
      boost::asio::io_context ioc;
      ssl::context ctx{ssl::context::tls_client};
      ctx.set_default_verify_paths();

      tcp::resolver resolver{ioc};
      boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>> ws{ioc, ctx};

      const std::string host = "ws.kraken.com";
      const std::string port = "443";
      const std::string path = "/v2";

      auto const results = resolver.resolve(host, port);

      bool connected = false;
      for (auto const& entry : results) {
        try {
          boost::beast::get_lowest_layer(ws).connect(entry.endpoint());
          connected = true;
          break;
        } catch (const std::exception& e) {
          std::cerr << "[KRAKEN][WS] tcp connect attempt failed: " 
            << e.what() << std::endl;
        }
      }

      if (!connected) {
        std::cerr << "[KRAKEN][WS] tcp connect failed for all endpoints" << std::endl;
        std::this_thread::sleep_for(1s);
        continue;
      }

      if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
        boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                     boost::asio::error::get_ssl_category()};
        throw boost::system::system_error{ec};
      }
      ws.next_layer().handshake(ssl::stream_base::client);

      ws.handshake(host, path);

      json sub;
      sub["method"] = "subscribe";
      json params;
      params["channel"] = "book";

      std::string ws_sym = normalize_symbol_for_ws(symbol_);
      params["symbol"] = json::array({ ws_sym });
      params["depth"] = 100;
      params["snapshot"] = true;
      sub["params"] = params;

      std::string sub_msg = sub.dump();
      ws.write(boost::asio::buffer(sub_msg));

      OrderBook book;
      got_ws_snapshot = false;
      boost::beast::flat_buffer buffer;
      while (running()) {
        buffer.clear();
        ws.read(buffer);
        std::string payload = boost::beast::buffers_to_string(buffer.data());

        try {
          apply_ws_message(payload, book);
          cb(book, "KRAKEN");
        } catch (const std::exception& e) {
          std::cerr << "[KRAKEN][WS] apply error: " 
            << e.what() << " — resyncing" << std::endl;
          break;
        }
      }

      boost::system::error_code ec;
      ws.close(boost::beast::websocket::close_code::normal, ec);
    } catch (const std::exception& e) {
      std::cerr << "[KRAKEN] exception: " << e.what() << std::endl;
    }

    std::this_thread::sleep_for(1s);
  }
}

