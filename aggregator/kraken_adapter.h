#pragma once
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "../common/order_book.h"

class KrakenAdapter {
public:
    using Callback = std::function<void(const OrderBook& book, const char* venue)>;

    explicit KrakenAdapter(std::string symbol = "BTCUSDT");
    ~KrakenAdapter();

    void start(Callback cb);
    void stop();

private:
    void run(Callback cb);
    bool fetch_snapshot(OrderBook& out_book, std::string& err);
    void apply_ws_message(const std::string& payload, OrderBook& book);

    // helpers
    std::string normalize_symbol_for_ws(const std::string& in) const;
    std::string symbol_for_rest_try1(const std::string& in) const;
    std::string symbol_for_rest_try2(const std::string& in) const; // e.g. BTC -> XBT

    std::string symbol_;
    std::thread th_;
    std::atomic<bool> running_{false};
    std::mutex book_mu_;
};

