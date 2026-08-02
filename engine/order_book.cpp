#include "order_book.h"

#include <algorithm>

namespace ome {

void OrderBook::add(const Order& o) {
    auto bo = std::make_shared<BookOrder>();
    bo->order = o;
    bo->resting = true;

    // An order that is fully filled at submission never rests on the book.
    if (o.remaining() == 0) {
        by_id_[o.id] = bo;
        return;
    }

    auto& book = (o.side == Side::BUY) ? bids_ : asks_;
    book[o.price].push_back(bo);
    by_id_[o.id] = bo;
}

bool OrderBook::cancel(uint64_t order_id) {
    return erase_id(order_id);
}

bool OrderBook::erase_id(uint64_t order_id) {
    auto it = by_id_.find(order_id);
    if (it == by_id_.end()) return false;

    LevelPtr bo = it->second;
    by_id_.erase(it);

    auto& book = (bo->order.side == Side::BUY) ? bids_ : asks_;
    auto level_it = book.find(bo->order.price);
    if (level_it == book.end()) return false;

    auto& q = level_it->second;
    auto oit = std::find(q.begin(), q.end(), bo);
    if (oit != q.end()) q.erase(oit);

    if (q.empty()) book.erase(level_it);
    return true;
}

std::vector<Level> OrderBook::bid_depth(uint32_t n) const {
    std::vector<Level> out;
    for (auto it = bids_.rbegin(); it != bids_.rend() && out.size() < n; ++it) {
        uint64_t total = 0;
        for (const auto& bo : it->second) total += bo->order.remaining();
        out.push_back(Level{it->first, total, static_cast<uint64_t>(it->second.size())});
    }
    return out;
}

std::vector<Level> OrderBook::ask_depth(uint32_t n) const {
    std::vector<Level> out;
    for (auto it = asks_.begin(); it != asks_.end() && out.size() < n; ++it) {
        uint64_t total = 0;
        for (const auto& bo : it->second) total += bo->order.remaining();
        out.push_back(Level{it->first, total, static_cast<uint64_t>(it->second.size())});
    }
    return out;
}

}  // namespace ome
