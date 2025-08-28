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

static std::string to_upper(std::string s){ for (auto &c: s) c = std::toupper(c); return s; }

KrakenAdapter::KrakenAdapter(std::string symbol): symbol_(std::move(symbol)) {}
KrakenAdapter::~KrakenAdapter(){ stop(); }

void KrakenAdapter::start(Callback cb){
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    th_ = std::thread([this, cb]{ run(cb); });
}
void KrakenAdapter::stop(){
    running_ = false;
    if (th_.joinable()) th_.join();
}

// Normalize input like "BTCUSDT", "BTC-USDT", "BTC/USDT" => "BTC/USDT" (for WS)
std::string KrakenAdapter::normalize_symbol_for_ws(const std::string& in) const {
    std::string s = in;
    for (auto &c: s) if (c=='-'||c=='_') c='/';
    if (s.find('/')==std::string::npos) {
        // try to split base/quote heuristically: check common quotes
        const std::vector<std::string> quotes = {"USDT","USDC","USD","EUR","BTC","ETH"};
        std::string up = to_upper(s);
        for (auto &q : quotes) {
            if (up.size() > q.size() && up.substr(up.size()-q.size()) == q) {
                std::string base = up.substr(0, up.size()-q.size());
                return base + "/" + q;
            }
        }
        // fallback: split in middle (not ideal)
        size_t mid = s.size()/2;
        return s.substr(0, mid) + "/" + s.substr(mid);
    }
    return s;
}

// REST pair try 1: remove separators, uppercase, e.g. "BTC/USDT" -> "BTCUSDT"
std::string KrakenAdapter::symbol_for_rest_try1(const std::string& in) const {
    std::string s = in;
    std::string out;
    for (char c: s) if (c!='/' && c!='-' && c!='_') out.push_back(c);
    return to_upper(out);
}
// REST pair try 2: replace BTC with XBT (Kraken historically uses XBT in some APIs)
std::string KrakenAdapter::symbol_for_rest_try2(const std::string& in) const {
    std::string s = symbol_for_rest_try1(in);
    // naive replace BTC -> XBT at start
    if (s.rfind("BTC", 0) == 0) {
        return "XBT" + s.substr(3);
    }
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

        // Try rest pair variants
        std::string p1 = symbol_for_rest_try1(symbol_);
        std::string p2 = symbol_for_rest_try2(symbol_);

        // try up to 2 targets
        std::vector<std::string> targets;
        targets.push_back("/0/public/Depth?pair=" + p1 + "&count=100");
        if (p2 != p1) targets.push_back("/0/public/Depth?pair=" + p2 + "&count=100");

        bool ok = false;
        for (auto const& target : targets) {
            try {
                auto const results = resolver.resolve(host, port);

                // robust connect across endpoints
                bool connected = false;
                for (auto const& entry : results) {
                    try {
                        boost::beast::get_lowest_layer(stream).connect(entry.endpoint());
                        connected = true;
                        break;
                    } catch (const std::exception& e) {
                        std::cerr << "[KRAKEN][REST] tcp connect attempt failed: " << e.what() << std::endl;
                    }
                }
                if (!connected) { err = "tcp connect failed"; continue; }

                // SNI + TLS handshake
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
                    continue;
                }

                auto j = json::parse(res.body());
                if (j.contains("error") && !j["error"].empty()) {
                    // Kraken returns errors array
                    err = "kraken rest error";
                    continue;
                }
                if (!j.contains("result") || j["result"].empty()) {
                    err = "no result";
                    continue;
                }

                // result is an object where the first key is pair code
                auto it = j["result"].begin();
                if (it == j["result"].end()) { err = "empty result"; continue; }
                auto& payload = it.value();

                out_book.bids.clear();
                out_book.asks.clear();

                // payload has "bids" and "asks" arrays with [price, volume, ...]
                if (payload.contains("bids")) {
                    for (auto& lvl : payload["bids"]) {
                        // lvl[0]=price, lvl[1]=volume
                        double p = lvl[0].is_string() ? std::stod(lvl[0].get<std::string>()) : lvl[0].get<double>();
                        double s = lvl[1].is_string() ? std::stod(lvl[1].get<std::string>()) : lvl[1].get<double>();
                        if (s>0) out_book.bids[p] = s;
                    }
                }
                if (payload.contains("asks")) {
                    for (auto& lvl : payload["asks"]) {
                        double p = lvl[0].is_string() ? std::stod(lvl[0].get<std::string>()) : lvl[0].get<double>();
                        double s = lvl[1].is_string() ? std::stod(lvl[1].get<std::string>()) : lvl[1].get<double>();
                        if (s>0) out_book.asks[p] = s;
                    }
                }

                ok = true;
                break; // successful snapshot
            } catch (const std::exception& e) {
                err = e.what();
                // try next target (p2)
                std::cerr << "[KRAKEN][REST] attempt error: " << e.what() << std::endl;
            }
        }

        return ok;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Parse Kraken v2 book snapshot/update JSON and update book accordingly
void KrakenAdapter::apply_ws_message(const std::string& payload, OrderBook& book) {
    auto j = json::parse(payload);
    if (!j.is_object()) return;

    // We're interested in channel "book"
    // Message shapes: snapshot/update with "channel":"book", "type": "snapshot"/"update", "data":[{...}]
    if (!j.contains("channel")) return;
    if (j["channel"].get<std::string>() != "book") return;
    if (!j.contains("type") || !j.contains("data")) return;

    std::string type = j["type"].get<std::string>();
    auto data = j["data"];
    if (!data.is_array() || data.empty()) return;

    // data[0] has "bids" and "asks" arrays with objects { "price":..., "qty":... }
    auto book_obj = data[0];

    if (type == "snapshot") {
        // Replace book
        book.bids.clear();
        book.asks.clear();
        if (book_obj.contains("bids")) {
            for (auto &lvl: book_obj["bids"]) {
                double p = lvl["price"].is_string() ? std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
                double q = lvl["qty"].is_string() ? std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
                if (q>0) book.bids[p] = q;
            }
        }
        if (book_obj.contains("asks")) {
            for (auto &lvl: book_obj["asks"]) {
                double p = lvl["price"].is_string() ? std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
                double q = lvl["qty"].is_string() ? std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
                if (q>0) book.asks[p] = q;
            }
        }
    } else if (type == "update") {
        // Apply deltas: price levels with qty==0 mean remove; otherwise set
        if (book_obj.contains("bids")) {
            for (auto &lvl: book_obj["bids"]) {
                double p = lvl["price"].is_string() ? std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
                double q = lvl["qty"].is_string() ? std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
                if (q == 0) book.bids.erase(p);
                else book.bids[p] = q;
            }
        }
        if (book_obj.contains("asks")) {
            for (auto &lvl: book_obj["asks"]) {
                double p = lvl["price"].is_string() ? std::stod(lvl["price"].get<std::string>()) : lvl["price"].get<double>();
                double q = lvl["qty"].is_string() ? std::stod(lvl["qty"].get<std::string>()) : lvl["qty"].get<double>();
                if (q == 0) book.asks.erase(p);
                else book.asks[p] = q;
            }
        }
    }
}

void KrakenAdapter::run(Callback cb) {
    using namespace std::chrono_literals;
    while (running_) {
        try {
            // 1) Try snapshot via REST (best-effort)
            OrderBook book;
            std::string err;
            if (!fetch_snapshot(book, err)) {
                std::cerr << "[KRAKEN] REST snapshot error: " << err << " — will try WS snapshot" << std::endl;
                // continue; // do not continue: Kraken WS sends a snapshot on subscribe, so go to WS path
            } else {
                cb(book, "KRAKEN"); // publish initial REST snapshot if available
            }

            // 2) Connect to WS v2 and subscribe to book channel (we request snapshot=true)
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
                    std::cerr << "[KRAKEN][WS] tcp connect attempt failed: " << e.what() << std::endl;
                }
            }
            if (!connected) {
                std::cerr << "[KRAKEN][WS] tcp connect failed for all endpoints" << std::endl;
                std::this_thread::sleep_for(1s);
                continue;
            }

            // SNI + TLS handshake
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
                boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                             boost::asio::error::get_ssl_category()};
                throw boost::system::system_error{ec};
            }
            ws.next_layer().handshake(ssl::stream_base::client);

            // WebSocket handshake
            ws.handshake(host, path);

            // Subscribe message: method=subscribe, params: channel=book, symbol=[ ... ], depth, snapshot=true
            json sub;
            sub["method"] = "subscribe";
            json params;
            params["channel"] = "book";
            // normalize for WS (e.g. "BTC/USDT")
            std::string ws_sym = normalize_symbol_for_ws(symbol_);
            params["symbol"] = json::array({ ws_sym });
            params["depth"] = 100;     // choose depth you need (10/25/100/500/1000)
            params["snapshot"] = true;
            sub["params"] = params;

            std::string sub_msg = sub.dump();
            ws.write(boost::asio::buffer(sub_msg));

            // Read loop
            boost::beast::flat_buffer buffer;
            while (running_) {
                buffer.clear();
                ws.read(buffer);
                std::string payload = boost::beast::buffers_to_string(buffer.data());

                try {
                    // apply message updates to book (snapshot/update)
                    apply_ws_message(payload, book);
                    cb(book, "KRAKEN");
                } catch (const std::exception& e) {
                    std::cerr << "[KRAKEN][WS] apply error: " << e.what() << " — resyncing" << std::endl;
                    break;
                }
            }

            // close ws gracefully
            boost::system::error_code ec;
            ws.close(boost::beast::websocket::close_code::normal, ec);
        } catch (const std::exception& e) {
            std::cerr << "[KRAKEN] exception: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(1s);
    }
}

