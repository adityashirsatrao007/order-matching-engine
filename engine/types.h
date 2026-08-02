#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "order.h"

namespace ome {

// Human-readable strings for status codes / order types (useful for the
// JSON bridge and debugging).
inline const char* side_str(Side s) {
    return s == Side::BUY ? "BUY" : "SELL";
}

inline const char* status_str(OrderStatus s) {
    switch (s) {
        case OrderStatus::NEW: return "NEW";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::CANCELLED: return "CANCELLED";
        case OrderStatus::REJECTED: return "REJECTED";
    }
    return "UNKNOWN";
}

// A single execution (trade) resulting from matching two orders.
struct Fill {
    uint64_t taker_id = 0;
    uint64_t maker_id = 0;
    uint64_t price = 0;
    uint64_t quantity = 0;
    uint64_t ts_ns = 0;
};

// Aggregate order-book depth at one price level.
struct Level {
    int64_t price = 0;
    uint64_t total_qty = 0;
    uint64_t order_count = 0;
};

// Summary of what happened to a submitted order.
struct AcceptResult {
    OrderStatus status = OrderStatus::NEW;
    uint64_t order_id = 0;
    uint64_t avg_fill_price = 0;  // volume-weighted avg execution price
    std::vector<Fill> fills;
    std::string reject_reason;
};

}  // namespace ome
