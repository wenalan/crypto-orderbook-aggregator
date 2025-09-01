#include "okx_adapter.h"
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


OKXAdapter::OKXAdapter(std::string symbol) : AdapterBase(std::move(symbol)){}
OKXAdapter::~OKXAdapter(){ stop(); }


bool OKXAdapter::fetch_snapshot(OrderBook& out_book, std::string& err) {
  try {
    boost::asio::io_context ioc;
    ssl::context ctx{ssl::context::tls_client};
    ctx.set_default_verify_paths();

    tcp::resolver resolver{ioc};
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream{ioc, ctx};

    const std::string host = "www.okx.com";
    const std::string port = "443";
    const std::string target = "/api/v5/market/books?instId=" + symbol_ + "&sz=400";

    auto const results = resolver.resolve(host, port);

    bool connected = false;
    for (auto const& entry : results) {
      try {
        boost::beast::get_lowest_layer(stream).connect(entry.endpoint());
        connected = true;
        break;
      } catch (const std::exception& e) {
        std::cerr << "[OKX][REST] tcp connect attempt failed: " << e.what() << std::endl;
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
    if (!j.contains("data") || j["data"].empty()) {
      err = "no data";
      return false;
    }

    out_book.bids.clear();
    out_book.asks.clear();

    for (auto& lvl : j["data"][0]["bids"]) {
      double p = std::stod(lvl[0].get<std::string>());
      double s = std::stod(lvl[1].get<std::string>());
      if (s > 0) out_book.bids[p] = s;
    }

    for (auto& lvl : j["data"][0]["asks"]) {
      double p = std::stod(lvl[0].get<std::string>());
      double s = std::stod(lvl[1].get<std::string>());
      if (s > 0) out_book.asks[p] = s;
    }

    return true;
  } catch (const std::exception& e) {
    err = e.what();
    return false;
  }
}

void OKXAdapter::apply_update_json(const std::string& payload, OrderBook& book) {
  auto j = json::parse(payload);

  std::vector<json> messages;
  if (j.is_array()) {
    for (auto& el : j) messages.push_back(el);
  } else if (j.is_object()) {
    messages.push_back(j);
  } else {
    return;
  }

  for (auto& msg : messages) {
    if (msg.contains("event")) continue;

    if (!msg.contains("data") || !msg["data"].is_array() || msg["data"].empty()) continue;
    const auto& entry = msg["data"].front();

    if (msg.contains("arg") && msg["arg"].contains("channel")
        && msg["arg"]["channel"].is_string()
        && msg["arg"]["channel"] != "books") continue;

    long long seq = entry.contains("seqId") && entry["seqId"].is_number_integer()
                    ? entry["seqId"].get<long long>() : -1;
    long long prev = entry.contains("prevSeqId") && entry["prevSeqId"].is_number_integer()
                     ? entry["prevSeqId"].get<long long>() : -2;

    bool is_snapshot = (msg.contains("action")
                        && msg["action"].is_string()
                        && msg["action"] == "snapshot");
    if (!is_snapshot && prev == -1) is_snapshot = true;

    if (is_snapshot) {
      book.bids.clear();
      book.asks.clear();

      auto fill_side = [&](const json& arr, auto& side) {
        if (!arr.is_array()) return;
        for (const auto& lvl : arr) {
          if (!lvl.is_array() || lvl.size() < 2) continue;
          double p = lvl[0].is_string() ? std::stod(lvl[0].get<std::string>()) : lvl[0].get<double>();
          double s = lvl[1].is_string() ? std::stod(lvl[1].get<std::string>()) : lvl[1].get<double>();
          if (s > 0) side[p] = s;
        }
      };

      if (entry.contains("bids")) fill_side(entry["bids"], book.bids);
      if (entry.contains("asks")) fill_side(entry["asks"], book.asks);
      if (entry.contains("b"))    fill_side(entry["b"],    book.bids);
      if (entry.contains("a"))    fill_side(entry["a"],    book.asks);

      got_ws_snapshot = true;
      if (seq >= 0) last_seq_id = seq;
      continue;
    }

    if (!got_ws_snapshot) continue;

    if (prev != -2 && last_seq_id != -1 && prev != last_seq_id) {
      throw std::runtime_error("OKX seq mismatch: prevSeqId != last_seq_id");
    }

    std::vector<std::pair<double,double>> bid_d, ask_d;
    auto collect_side = [&](const json& arr, std::vector<std::pair<double,double>>& out) {
      if (!arr.is_array()) return;
      for (const auto& lvl : arr) {
        if (!lvl.is_array() || lvl.size() < 2) continue;
        double p = lvl[0].is_string() ? std::stod(lvl[0].get<std::string>()) : lvl[0].get<double>();
        double s = lvl[1].is_string() ? std::stod(lvl[1].get<std::string>()) : lvl[1].get<double>();
        out.emplace_back(p, s);
      }
    };

    if (entry.contains("bids")) collect_side(entry["bids"], bid_d);
    if (entry.contains("asks")) collect_side(entry["asks"], ask_d);
    if (entry.contains("b"))    collect_side(entry["b"],    bid_d);
    if (entry.contains("a"))    collect_side(entry["a"],    ask_d);

    if (!bid_d.empty()) apply_deltas(book.bids, bid_d);
    if (!ask_d.empty()) apply_deltas(book.asks, ask_d);

    if (seq >= 0) last_seq_id = seq;
  }
}

void OKXAdapter::run(Callback cb) {
  using namespace std::chrono_literals;

  while (running()) {
    try {
      got_ws_snapshot = false;
      last_seq_id = -1;

      boost::asio::io_context ioc;
      ssl::context ctx{ssl::context::tls_client};
      ctx.set_default_verify_paths();

      tcp::resolver resolver{ioc};
      boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>> ws{ioc, ctx};

      const std::string host = "ws.okx.com";
      const std::string port = "8443";
      const std::string path = "/ws/v5/public";

      auto const results = resolver.resolve(host, port);
      bool connected = false;
      for (auto const& entry : results) {
        try {
          boost::beast::get_lowest_layer(ws).connect(entry.endpoint());
          connected = true;
          break;
        } catch (const std::exception& e) {
          std::cerr << "[OKX][WS] tcp connect attempt failed: " << e.what() << std::endl;
        }
      }
      if (!connected) {
        std::cerr << "[OKX][WS] tcp connect failed for all endpoints" << std::endl;
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
      sub["op"] = "subscribe";
      sub["args"] = json::array({ { {"channel","books"}, {"instId", symbol_} } });
      ws.write(boost::asio::buffer(sub.dump()));

      OrderBook book;
      boost::beast::flat_buffer buffer;

      while (running()) {
        buffer.clear();
        ws.read(buffer);
        std::string payload = boost::beast::buffers_to_string(buffer.data());

        try {
          apply_update_json(payload, book);
          if (got_ws_snapshot) {
            cb(book, "OKX");
          }
        } catch (const std::exception& e) {
          std::cerr << "[OKX][WS] apply error: " << e.what() << " — resubscribing..." << std::endl;
          break;
        }
      }

      if (ws.is_open()) {
        boost::system::error_code ec;
        ws.close(boost::beast::websocket::close_code::normal, ec);
      }

  } catch (const std::exception& e) {
    std::cerr << "[OKX] exception: " << e.what() << std::endl;
  }

  std::this_thread::sleep_for(1s);
  }
}

