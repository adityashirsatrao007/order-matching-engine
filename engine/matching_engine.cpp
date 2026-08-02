#include "matching_engine.h"

#include <algorithm>
#include <chrono>

namespace ome {

namespace {
// Monotonic nanosecond clock (no wall-clock skew between fills).
uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

// Total quantity available to match against the given order at the prices
// it is allowed to trade at. Used to enforce Fill-Or-Kill semantics.
uint64_t available_liquidity(const OrderBook& book, const Order& o) {
    const auto& hits = (o.side == Side::BUY) ? book.asks() : book.bids();
    uint64_t total = 0;
    for (const auto& [price, level] : hits) {
        if (o.type == OrderType::LIMIT) {
            if (o.side == Side::BUY && o.price < price) break;
            if (o.side == Side::SELL && o.price > price) break;
        }
        for (const auto& bo : level) total += bo->order.remaining();
    }
    return total;
}
}  // namespace

MatchingEngine::MatchingEngine() {
    book_.set_engine(this);
}

AcceptResult MatchingEngine::submit(const Order& o) {
    AcceptResult out;
    if (o.quantity == 0) {
        out.status = OrderStatus::REJECTED;
        out.reject_reason = "quantity must be > 0";
        if (cb_.on_order_rejected) cb_.on_order_rejected(o);
        return out;
    }

    Order oo = o;
    if (oo.id == 0) oo.id = ++next_id_;
    out.order_id = oo.id;
    oo.timestamp_ns = now_ns();

    if (oo.type == OrderType::LIMIT || oo.type == OrderType::STOP_LIMIT) {
        if (oo.price <= 0) {
            out.status = OrderStatus::REJECTED;
            out.reject_reason = "invalid limit price";
            if (cb_.on_order_rejected) cb_.on_order_rejected(oo);
            return out;
        }
    }
    if ((oo.type == OrderType::STOP || oo.type == OrderType::STOP_LIMIT) &&
        oo.stop_price <= 0) {
        out.status = OrderStatus::REJECTED;
        out.reject_reason = "invalid stop price";
        if (cb_.on_order_rejected) cb_.on_order_rejected(oo);
        return out;
    }

    if (cb_.on_order_accepted) cb_.on_order_accepted(oo);

    // STOP orders don't join the book; they wait for a trigger. In this
    // engine a stop triggers when the last trade price crosses the stop
    // price (documented simplification vs a resting stop book).
    if (oo.type == OrderType::STOP || oo.type == OrderType::STOP_LIMIT) {
        if (!has_last_price_) {
            out.status = OrderStatus::REJECTED;
            out.reject_reason = "no reference price for stop order";
            if (cb_.on_order_rejected) cb_.on_order_rejected(oo);
            return out;
        }
        const bool triggered = (oo.side == Side::BUY)
                                   ? last_trade_price_ >= static_cast<uint64_t>(oo.stop_price)
                                   : last_trade_price_ <= static_cast<uint64_t>(oo.stop_price);
        if (!triggered) {
            out.status = OrderStatus::REJECTED;
            out.reject_reason = "stop not triggered (no resting stop book in this demo engine)";
            if (cb_.on_order_rejected) cb_.on_order_rejected(oo);
            return out;
        }
        oo.type = (oo.type == OrderType::STOP) ? OrderType::MARKET : OrderType::LIMIT;
    }

    // Fill-Or-Kill: verify the full quantity can be satisfied before any
    // execution. Violations reject atomically with zero fills.
    if (oo.tif == TimeInForce::FOK && oo.remaining() > 0) {
        const uint64_t avail = available_liquidity(book_, oo);
        if (avail < oo.quantity) {
            out.status = OrderStatus::REJECTED;
            out.reject_reason = "FOK: insufficient liquidity for full fill";
            if (cb_.on_order_rejected) cb_.on_order_rejected(oo);
            return out;
        }
    }

    match(oo, out);
    return out;
}

bool MatchingEngine::cancel(uint64_t order_id) {
    if (book_.cancel(order_id)) {
        if (cb_.on_order_cancelled) {
            Order o;
            o.id = order_id;
            cb_.on_order_cancelled(o);
        }
        return true;
    }
    return false;
}

void MatchingEngine::match(Order& o, AcceptResult& out) {
    match_against_book(o, out);

    // Volume-weighted average fill price.
    uint64_t traded_notional = 0;
    for (const auto& f : out.fills) traded_notional += f.price * f.quantity;
    if (o.filled > 0) out.avg_fill_price = traded_notional / o.filled;

    if (o.is_done()) {
        out.status = OrderStatus::FILLED;
        return;
    }

    if (o.tif == TimeInForce::IOC || o.tif == TimeInForce::FOK) {
        // IOC: remainder is rejected (already checked FOK liquidity).
        if (o.filled > 0) {
            out.status = OrderStatus::REJECTED;
            out.reject_reason = "unfilled remainder under IOC/FOK";
        } else {
            out.status = OrderStatus::REJECTED;
            out.reject_reason = "no liquidity under IOC/FOK";
        }
        return;
    }

    if (o.filled > 0) {
        out.status = OrderStatus::PARTIALLY_FILLED;
    } else {
        out.status = OrderStatus::NEW;
    }

    // Rest the remainder on the book.
    if (o.remaining() > 0) {
        Order rest = o;
        rest.quantity = o.remaining();
        rest.filled = 0;
        book_.add(rest);
    }
}

void MatchingEngine::match_against_book(Order& o, AcceptResult& out) {
    while (o.remaining() > 0) {
        const auto& hits = (o.side == Side::BUY) ? book_.asks() : book_.bids();
        if (hits.empty()) break;

        const int64_t best_price = (o.side == Side::BUY) ? hits.begin()->first
                                                         : hits.rbegin()->first;

        // For a LIMIT order, we can only cross if price is right.
        if (o.type == OrderType::LIMIT) {
            if (o.side == Side::BUY && o.price < best_price) break;
            if (o.side == Side::SELL && o.price > best_price) break;
        }

        // Pop the front of the best price level (FIFO within level).
        auto& level = const_cast<std::deque<LevelPtr>&>(
            (o.side == Side::BUY) ? hits.begin()->second : hits.rbegin()->second);
        LevelPtr maker = level.front();

        const uint64_t qty = std::min(o.remaining(), maker->order.remaining());
        const uint64_t px = static_cast<uint64_t>(best_price);

        execute(o, maker, px, qty, out);

        if (maker->order.is_done()) {
            book_.erase_id(maker->order.id);
        } else {
            // The maker still has quantity at this level; leave it. Since we
            // don't remove the deque entry, the loop's next `front()` picks it
            // up again if the taker still has size.
            continue;
        }
        if (level.empty()) {
            auto& m = const_cast<std::map<int64_t, std::deque<LevelPtr>>&>(hits);
            m.erase(best_price);
        }
    }
}

void MatchingEngine::execute(Order& taker, LevelPtr maker, uint64_t price,
                             uint64_t qty, AcceptResult& out) {
    taker.filled += qty;
    maker->order.filled += qty;

    Fill f{taker.id, maker->order.id, price, qty, now_ns()};
    out.fills.push_back(f);
    total_fills_++;
    total_shares_ += qty;

    last_trade_price_ = price;
    has_last_price_ = true;

    if (cb_.on_fill) cb_.on_fill(f);
}

}  // namespace ome
