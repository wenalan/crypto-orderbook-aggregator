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

// helper to lowercase, if needed
static std::string to_lower(std::string s){ for (auto& c: s) c = std::tolower(c); return s; }

OKXAdapter::OKXAdapter(std::string symbol) : symbol_(std::move(symbol)) {}
OKXAdapter::~OKXAdapter(){ stop(); }

void OKXAdapter::start(Callback cb){
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    th_ = std::thread([this, cb]{ run(cb); });
}

void OKXAdapter::stop(){
    running_ = false;
    if (th_.joinable()) th_.join();
}

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

        // resolve endpoints
        auto const results = resolver.resolve(host, port);

        // robust: try each resolved endpoint
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

        // SNI + TLS handshake
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                         boost::asio::error::get_ssl_category()};
            throw boost::system::system_error{ec};
        }
        stream.handshake(ssl::stream_base::client);

        // HTTP GET
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "beast");
        http::write(stream, req);

        // Read response
        boost::beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        // Shutdown TLS
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

        // OKX REST book uses arrays of [price, size, ...]
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
    // OKX websocket messages can be arrays or objects; be tolerant.
    auto j = json::parse(payload);

    // If top-level is object and has "event" like "subscribe" or "error", ignore
    // If it's an array of messages, iterate
    std::vector<json> messages;
    if (j.is_array()) {
        for (auto& el : j) messages.push_back(el);
    } else if (j.is_object()) {
        messages.push_back(j);
    } else {
        return;
    }

    for (auto& msg : messages) {
        // OKX messages of interest have "arg" and "data", or "data" directly
        if (!msg.contains("data")) continue;
        auto data_node = msg["data"];
        if (!data_node.is_array()) continue;

        for (auto& entry : data_node) {
            // entry might contain "b" and "a" (book delta) or "bids"/"asks"
            std::vector<std::pair<double,double>> bid_d, ask_d;

            // case: compact fields "b" and "a" (some OKX messages)
            if (entry.contains("b") || entry.contains("a")) {
                if (entry.contains("b")) {
                    for (auto& lvl : entry["b"]) {
                        // each lvl might be [price, size] or object — handle common case
                        if (!lvl.is_array() || lvl.size() < 2) continue;
                        double p = std::stod(lvl[0].get<std::string>());
                        double s = std::stod(lvl[1].get<std::string>());
                        bid_d.emplace_back(p, s);
                    }
                }
                if (entry.contains("a")) {
                    for (auto& lvl : entry["a"]) {
                        if (!lvl.is_array() || lvl.size() < 2) continue;
                        double p = std::stod(lvl[0].get<std::string>());
                        double s = std::stod(lvl[1].get<std::string>());
                        ask_d.emplace_back(p, s);
                    }
                }
            } else {
                // case: REST-like "bids" / "asks"
                if (entry.contains("bids")) {
                    for (auto& lvl : entry["bids"]) {
                        if (!lvl.is_array() || lvl.size() < 2) continue;
                        double p = std::stod(lvl[0].get<std::string>());
                        double s = std::stod(lvl[1].get<std::string>());
                        bid_d.emplace_back(p, s);
                    }
                }
                if (entry.contains("asks")) {
                    for (auto& lvl : entry["asks"]) {
                        if (!lvl.is_array() || lvl.size() < 2) continue;
                        double p = std::stod(lvl[0].get<std::string>());
                        double s = std::stod(lvl[1].get<std::string>());
                        ask_d.emplace_back(p, s);
                    }
                }
            }

            // apply deltas if any (apply_deltas is expected to be available in your common code)
            if (!bid_d.empty()) apply_deltas(book.bids, bid_d);
            if (!ask_d.empty()) apply_deltas(book.asks, ask_d);
        }
    }
}

void OKXAdapter::run(Callback cb) {
    using namespace std::chrono_literals;
    while (running_) {
        try {
            // 1) snapshot
            OrderBook book;
            std::string err;
            if (!fetch_snapshot(book, err)) {
                std::cerr << "[OKX] snapshot error: " << err << std::endl;
                std::this_thread::sleep_for(1s);
                continue;
            }
            cb(book, "OKX");

            // 2) websocket connect & subscribe
            boost::asio::io_context ioc;
            ssl::context ctx{ssl::context::tls_client};
            ctx.set_default_verify_paths();

            tcp::resolver resolver{ioc};
            boost::beast::websocket::stream<
                boost::beast::ssl_stream<boost::beast::tcp_stream>> ws{ioc, ctx};

            const std::string host = "ws.okx.com";
            const std::string port = "8443";
            const std::string path = "/ws/v5/public"; // OKX public v5

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

            // SNI + TLS handshake
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
                boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                             boost::asio::error::get_ssl_category()};
                throw boost::system::system_error{ec};
            }
            ws.next_layer().handshake(ssl::stream_base::client);

            // WebSocket handshake
            ws.handshake(host, path);

            // Subscribe to books channel
            json sub;
            sub["op"] = "subscribe";
            sub["args"] = json::array({ { {"channel", "books"}, {"instId", symbol_} } });
            std::string sub_msg = sub.dump();
            ws.write(boost::asio::buffer(sub_msg));

            // Read loop: on each payload, apply updates and callback
            boost::beast::flat_buffer buffer;
            while (running_) {
                buffer.clear();
                ws.read(buffer);
                std::string payload = boost::beast::buffers_to_string(buffer.data());

                try {
                    apply_update_json(payload, book);
                    cb(book, "OKX");
                } catch (const std::exception& e) {
                    std::cerr << "[OKX][WS] update apply error: " << e.what() << " — resyncing..." << std::endl;
                    break; // break loop -> resnapshot
                }
            }

            // graceful close
            boost::system::error_code ec;
            ws.close(boost::beast::websocket::close_code::normal, ec);

        } catch (const std::exception& e) {
            std::cerr << "[OKX] exception: " << e.what() << std::endl;
        }

        // backoff before resnapshot / reconnect
        std::this_thread::sleep_for(1s);
    }
}

