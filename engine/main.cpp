// order-matching-engine CLI demo
// ------------------------------
// Interactively exercises the matching engine and prints the trade tape.
// Usage:
//   ./ome-cli                 interactive demo
//   ./ome-cli --bench 100000  run latency/throughput benchmark
//
// The CLI is intentionally small; the interesting surface is the engine
// and its unit tests.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "matching_engine.h"
#include "order.h"
#include "types.h"

using namespace ome;

namespace {

void print_book(const MatchingEngine& e) {
    auto bids = e.bid_depth(3);
    auto asks = e.ask_depth(3);
    printf("    book:\n");
    printf("      asks\n");
    for (auto it = asks.rbegin(); it != asks.rend(); ++it)
        printf("        %llu @ %llu (%llu orders)\n",
               (unsigned long long)it->total_qty,
               (unsigned long long)it->price,
               (unsigned long long)it->order_count);
    printf("      bids\n");
    for (const auto& l : bids)
        printf("        %llu @ %llu (%llu orders)\n",
               (unsigned long long)l.total_qty,
               (unsigned long long)l.price,
               (unsigned long long)l.order_count);
}

void run_interactive() {
    MatchingEngine engine;
    EngineCallbacks cb;
    cb.on_fill = [](const Fill& f) {
        printf("    FILL taker=%llu maker=%llu %llu @ %llu\n",
               (unsigned long long)f.taker_id, (unsigned long long)f.maker_id,
               (unsigned long long)f.quantity, (unsigned long long)f.price);
    };
    engine.set_callbacks(cb);

    uint64_t next_id = 1;
    printf("== Order Matching Engine — interactive demo ==\n");
    printf("Commands: B <price> <qty> | S <price> <qty> | M B/S <qty> | X <order_id> | q\n");

    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[16] = {0};
        char side_c = 0;
        long long a = 0, b = 0;
        if (sscanf(line, "%15s", cmd) != 1) continue;

        Order o;
        if (strcmp(cmd, "B") == 0 && sscanf(line, "B %lld %lld", &a, &b) == 2) {
            o.id = next_id++;
            o.side = Side::BUY;
            o.type = OrderType::LIMIT;
            o.price = a;
            o.quantity = b;
        } else if (strcmp(cmd, "S") == 0 && sscanf(line, "S %lld %lld", &a, &b) == 2) {
            o.id = next_id++;
            o.side = Side::SELL;
            o.type = OrderType::LIMIT;
            o.price = a;
            o.quantity = b;
        } else if (strcmp(cmd, "M") == 0 && sscanf(line, "M %c %lld", &side_c, &b) == 2) {
            o.id = next_id++;
            o.side = (side_c == 'B') ? Side::BUY : Side::SELL;
            o.type = OrderType::MARKET;
            o.quantity = b;
        } else if (strcmp(cmd, "X") == 0 && sscanf(line, "X %lld", &a) == 1) {
            bool ok = engine.cancel((uint64_t)a);
            printf("    cancel %llu -> %s\n", (unsigned long long)a, ok ? "ok" : "not found");
            continue;
        } else if (strcmp(cmd, "q") == 0) {
            break;
        } else {
            printf("    ?\n");
            continue;
        }

        AcceptResult r = engine.submit(o);
        printf("    order %llu -> %s", (unsigned long long)r.order_id, status_str(r.status));
        if (!r.reject_reason.empty()) printf(" (%s)", r.reject_reason.c_str());
        printf("\n");
        print_book(engine);
    }
    printf("stats: fills=%llu shares=%llu\n",
           (unsigned long long)engine.total_fills(),
           (unsigned long long)engine.total_shares_traded());
}

void run_bench(uint64_t n) {
    MatchingEngine engine;
    // Seed the book with resting liquidity at every price we'll hit.
    const uint64_t mid = 1000000;
    for (uint64_t i = 1; i <= 100; ++i) {
        Order bid;
        bid.side = Side::BUY;
        bid.type = OrderType::LIMIT;
        bid.price = (int64_t)(mid - i * 100);
        bid.quantity = 1000;
        engine.submit(bid);
    }
    for (uint64_t i = 0; i < 100; ++i) {
        Order ask;
        ask.side = Side::SELL;
        ask.type = OrderType::LIMIT;
        ask.price = (int64_t)(mid + i * 100);
        ask.quantity = 1000;
        engine.submit(ask);
    }

    // Benchmark: alternating marketable buy/sell against the book.
    auto t0 = std::chrono::steady_clock::now();
    uint64_t matched = 0;
    for (uint64_t i = 0; i < n; ++i) {
        Order o;
        o.side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        o.type = OrderType::LIMIT;
        o.price = (int64_t)mid;
        o.quantity = 1;
        AcceptResult r = engine.submit(o);
        matched += r.fills.size();
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double us_per_order = (ms * 1000.0) / (double)n;
    printf("bench: %llu marketable orders in %.2f ms (%.2f us/order, %.0f orders/sec)\n",
           (unsigned long long)n, ms, us_per_order, (double)n / (ms / 1000.0));
    printf("bench: %llu fills, %llu shares traded\n",
           (unsigned long long)engine.total_fills(),
           (unsigned long long)engine.total_shares_traded());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "--bench") == 0) {
        run_bench(std::stoull(argv[2]));
        return 0;
    }
    run_interactive();
    return 0;
}
