#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "order.h"
#include "types.h"

namespace ome {

// Forward declaration so the book can call back into the engine for fills.
class MatchingEngine;

// A single resting order in the book. Lives on the owning price level's
// FIFO deque. `std::shared_ptr` keeps references valid across fills so the
// engine can update `filled` without invalidating book iterators.
struct BookOrder {
    Order order;
    bool resting = true;
};

using LevelPtr = std::shared_ptr<BookOrder>;

// Price-time priority limit order book.
//
//   - bids:  descending price (best bid at rbegin, highest first)
//   - asks:  ascending price (best ask at begin, lowest first)
//   - FIFO within each price level (deque push_back, pop_front)
//
// All quantities are integer "lots" to avoid float error.
class OrderBook {
public:
    OrderBook() = default;
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Add a resting order. Caller is responsible for price validity.
    void add(const Order& o);

    // Cancel a resting order; returns true if found and removed.
    bool cancel(uint64_t order_id);

    // Read-only depth snapshots.
    std::vector<Level> bid_depth(uint32_t n = 10) const;
    std::vector<Level> ask_depth(uint32_t n = 10) const;
    std::vector<Level> bid_levels(uint32_t n = 10) const { return bid_depth(n); }
    std::vector<Level> ask_levels(uint32_t n = 10) const { return ask_depth(n); }

    uint64_t best_bid() const { return bids_.empty() ? 0 : bids_.rbegin()->first; }
    uint64_t best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }
    bool has_bid() const { return !bids_.empty(); }
    bool has_ask() const { return !asks_.empty(); }
    size_t resting_count() const { return by_id_.size(); }

    const std::map<int64_t, std::deque<LevelPtr>>& bids() const { return bids_; }
    const std::map<int64_t, std::deque<LevelPtr>>& asks() const { return asks_; }

    // Internal helpers (exposed for the engine).
    void set_engine(MatchingEngine* e) { engine_ = e; }
    bool erase_id(uint64_t order_id);

private:
    std::map<int64_t, std::deque<LevelPtr>> bids_;  // price -> FIFO queue
    std::map<int64_t, std::deque<LevelPtr>> asks_;
    std::unordered_map<uint64_t, LevelPtr> by_id_;
    MatchingEngine* engine_ = nullptr;
};

}  // namespace ome
