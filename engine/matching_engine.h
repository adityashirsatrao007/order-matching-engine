#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "order.h"
#include "order_book.h"
#include "types.h"

namespace ome {

// Callbacks the host (REST/WebSocket bridge, replays, tests) can attach.
// These are what make the engine observable — trade tape + book updates.
struct EngineCallbacks {
    std::function<void(const Fill&)> on_fill;
    std::function<void(const Order&)> on_order_accepted;
    std::function<void(const Order&)> on_order_rejected;
    std::function<void(const Order&)> on_order_cancelled;
};

// Deterministic limit-order matching engine with price-time priority.
//
// Matching rules:
//   - A BUY order crosses when its limit >= best ask.
//   - A SELL order crosses when its limit <= best bid.
//   - Within a level, FIFO (price-time priority) determines precedence.
//   - MARKET orders cross at the best available price; remainder is
//     rejected for IOC/FOK or becomes a resting order otherwise.
//   - STOP orders trigger when the last trade price crosses the stop
//     price; a triggered stop becomes a marketable order.
class MatchingEngine {
public:
    MatchingEngine();

    // Submit an order; returns what happened to it.
    AcceptResult submit(const Order& o);

    // Cancel a resting order.
    bool cancel(uint64_t order_id);

    // Depth + best quotes.
    std::vector<Level> bid_depth(uint32_t n = 10) const { return book_.bid_depth(n); }
    std::vector<Level> ask_depth(uint32_t n = 10) const { return book_.ask_depth(n); }
    uint64_t best_bid() const { return book_.best_bid(); }
    uint64_t best_ask() const { return book_.best_ask(); }
    size_t resting_count() const { return book_.resting_count(); }

    // Tap into the trade tape.
    void set_callbacks(const EngineCallbacks& cb) { cb_ = cb; }

    // Statistics.
    uint64_t total_fills() const { return total_fills_; }
    uint64_t total_shares_traded() const { return total_shares_; }

private:
    void match(Order& o, AcceptResult& out);
    void match_against_book(Order& o, AcceptResult& out);
    void sweep_for_stops(const Order& last);
    void execute(Order& taker, LevelPtr maker, uint64_t price, uint64_t qty,
                 AcceptResult& out);

    OrderBook book_;
    EngineCallbacks cb_;
    uint64_t next_id_ = 0;
    uint64_t last_trade_price_ = 0;
    bool has_last_price_ = false;
    uint64_t total_fills_ = 0;
    uint64_t total_shares_ = 0;
    uint64_t clock_ns_ = 0;
};

}  // namespace ome
