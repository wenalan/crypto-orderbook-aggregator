#pragma once
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "../common/order_book.h"

class OKXAdapter {
public:
    using Callback = std::function<void(const OrderBook& book, const char* venue)>;

    explicit OKXAdapter(std::string symbol = "BTC-USDT");
    ~OKXAdapter();

    void start(Callback cb);
    void stop();

private:
    void run(Callback cb);
    bool fetch_snapshot(OrderBook& out_book, std::string& err);
    void apply_update_json(const std::string& payload, OrderBook& book);

    std::string symbol_;
    std::thread th_;
    std::atomic<bool> running_{false};
    std::mutex book_mu_;
};

