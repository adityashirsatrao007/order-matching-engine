#pragma once

#include <cstdint>
#include <string>
#include <cassert>

namespace ome {

// Order side.
enum class Side : uint8_t {
    BUY = 0,
    SELL = 1,
};

// Order time-in-force semantics.
enum class TimeInForce : uint8_t {
    GTC = 0,  // Good Till Cancel
    IOC = 1,  // Immediate Or Cancel
    FOK = 2,  // Fill Or Kill
};

// Order type.
enum class OrderType : uint8_t {
    LIMIT = 0,
    MARKET = 1,
    STOP = 2,
    STOP_LIMIT = 3,
};

// Outcome of a single accepted order.
enum class OrderStatus : uint8_t {
    NEW = 0,
    PARTIALLY_FILLED = 1,
    FILLED = 2,
    CANCELLED = 3,
    REJECTED = 4,
};

// A single order. Orders are immutable after submission except for
// remaining quantity which is only mutated by the matching engine.
struct Order {
    uint64_t id = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    TimeInForce tif = TimeInForce::GTC;
    int64_t price = 0;      // limit price; ignored for MARKET
    int64_t stop_price = 0; // trigger price for STOP/STOP_LIMIT
    uint64_t quantity = 0;  // total quantity
    uint64_t filled = 0;    // cumulative filled quantity
    uint64_t timestamp_ns = 0;

    uint64_t remaining() const { return quantity - filled; }
    bool is_done() const { return remaining() == 0; }
};

}  // namespace ome
