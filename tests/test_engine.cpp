// Unit tests for the matching engine — no external framework, plain asserts.
// Build with: make test  (or run the compiled ./ome-tests binary)

#include <cassert>
#include <cstdio>
#include <vector>

#include "matching_engine.h"
#include "order.h"
#include "types.h"

using namespace ome;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

void test_basic_fill_price_time_priority() {
    printf("test_basic_fill_price_time_priority\n");
    MatchingEngine e;

    // Two sellers at the same price: 100 @ 10 (earlier), 100 @ 10 (later).
    Order s1; s1.side = Side::SELL; s1.type = OrderType::LIMIT; s1.price = 10; s1.quantity = 100;
    Order s2; s2.side = Side::SELL; s2.type = OrderType::LIMIT; s2.price = 10; s2.quantity = 100;
    auto r1 = e.submit(s1);
    auto r2 = e.submit(s2);
    CHECK(r1.status == OrderStatus::NEW);
    CHECK(r2.status == OrderStatus::NEW);

    // One big buyer takes 150 — must consume seller 1 first (FIFO).
    Order b; b.side = Side::BUY; b.type = OrderType::LIMIT; b.price = 10; b.quantity = 150;
    auto rb = e.submit(b);
    CHECK(rb.status == OrderStatus::FILLED);
    CHECK(rb.fills.size() == 2);
    CHECK(rb.fills[0].maker_id == r1.order_id);  // earlier seller first
    CHECK(rb.fills[1].maker_id == r2.order_id);
    CHECK(rb.avg_fill_price == 10);
    CHECK(e.resting_count() == 1);  // r2 has 50 remaining
}

void test_price_priority_before_time_priority() {
    printf("test_price_priority_before_time_priority\n");
    MatchingEngine e;

    Order s1; s1.side = Side::SELL; s1.type = OrderType::LIMIT; s1.price = 12; s1.quantity = 100;  // later, worse
    Order s2; s2.side = Side::SELL; s2.type = OrderType::LIMIT; s2.price = 11; s2.quantity = 100;  // earlier, better
    auto r1 = e.submit(s1);
    auto r2 = e.submit(s2);

    Order b; b.side = Side::BUY; b.type = OrderType::LIMIT; b.price = 12; b.quantity = 150;
    auto rb = e.submit(b);
    CHECK(rb.fills[0].maker_id == r2.order_id);  // best price first (11), despite arriving later
    CHECK(rb.fills[0].price == 11);
}

void test_market_order() {
    printf("test_market_order\n");
    MatchingEngine e;
    Order s; s.side = Side::SELL; s.type = OrderType::LIMIT; s.price = 10; s.quantity = 50;
    e.submit(s);

    Order m; m.side = Side::BUY; m.type = OrderType::MARKET; m.quantity = 30;
    auto rm = e.submit(m);
    CHECK(rm.status == OrderStatus::FILLED);
    CHECK(rm.fills.size() == 1);
    CHECK(rm.fills[0].price == 10);
    CHECK(e.resting_count() == 1);  // 20 left on the ask
}

void test_ioc_partial_and_fok() {
    printf("test_ioc_partial_and_fok\n");
    MatchingEngine e;
    Order s; s.side = Side::SELL; s.type = OrderType::LIMIT; s.price = 10; s.quantity = 40;
    e.submit(s);

    // IOC larger than liquidity -> fills 40 then rejects remainder, no rest.
    Order ioc; ioc.side = Side::BUY; ioc.type = OrderType::LIMIT; ioc.tif = TimeInForce::IOC;
    ioc.price = 10; ioc.quantity = 100;
    auto ri = e.submit(ioc);
    CHECK(ri.status == OrderStatus::REJECTED);
    CHECK(ri.fills.size() == 1);
    CHECK(e.resting_count() == 0);  // ask fully consumed, nothing rested

    // FOK that cannot fully fill -> nothing executes.
    Order s2; s2.side = Side::SELL; s2.type = OrderType::LIMIT; s2.price = 10; s2.quantity = 50;
    e.submit(s2);
    Order fok; fok.side = Side::BUY; fok.type = OrderType::LIMIT; fok.tif = TimeInForce::FOK;
    fok.price = 10; fok.quantity = 100;
    auto rf = e.submit(fok);
    CHECK(rf.status == OrderStatus::REJECTED);
    CHECK(rf.fills.empty());
    CHECK(e.resting_count() == 1);  // ask still there
}

void test_cancel() {
    printf("test_cancel\n");
    MatchingEngine e;
    Order s; s.side = Side::SELL; s.type = OrderType::LIMIT; s.price = 10; s.quantity = 100;
    auto r = e.submit(s);
    CHECK(e.resting_count() == 1);
    CHECK(e.cancel(r.order_id) == true);
    CHECK(e.resting_count() == 0);
    CHECK(e.cancel(r.order_id) == false);  // idempotent
}

void test_reject_invalid() {
    printf("test_reject_invalid\n");
    MatchingEngine e;
    Order z; z.side = Side::BUY; z.type = OrderType::LIMIT; z.price = 0; z.quantity = 10;
    auto rz = e.submit(z);
    CHECK(rz.status == OrderStatus::REJECTED);
}

void test_zero_quantity_rejected() {
    printf("test_zero_quantity_rejected\n");
    MatchingEngine e;
    Order o; o.side = Side::BUY; o.type = OrderType::LIMIT; o.price = 10; o.quantity = 0;
    CHECK(e.submit(o).status == OrderStatus::REJECTED);
}

void test_resting_partial_fill_then_consume() {
    printf("test_resting_partial_fill_then_consume\n");
    MatchingEngine e;
    Order b; b.side = Side::BUY; b.type = OrderType::LIMIT; b.price = 10; b.quantity = 100;
    e.submit(b);  // rests
    Order s; s.side = Side::SELL; s.type = OrderType::LIMIT; s.price = 10; s.quantity = 40;
    auto rs = e.submit(s);  // fills 40 against buyer, rests 60? no — seller fully consumed
    CHECK(rs.status == OrderStatus::FILLED);
    CHECK(e.resting_count() == 1);  // buyer with 60 left
    Order s2; s2.side = Side::SELL; s2.type = OrderType::MARKET; s2.quantity = 60;
    auto rs2 = e.submit(s2);
    CHECK(rs2.status == OrderStatus::FILLED);
    CHECK(e.resting_count() == 0);
}

void test_trade_tape_callback() {
    printf("test_trade_tape_callback\n");
    MatchingEngine e;
    int n_fills = 0;
    uint64_t last_price = 0;
    EngineCallbacks cb;
    cb.on_fill = [&](const Fill& f) {
        ++n_fills;
        last_price = f.price;
    };
    e.set_callbacks(cb);

    Order s; s.side = Side::SELL; s.type = OrderType::LIMIT; s.price = 25; s.quantity = 10;
    e.submit(s);
    Order b; b.side = Side::BUY; b.type = OrderType::MARKET; b.quantity = 10;
    e.submit(b);
    CHECK(n_fills == 1);
    CHECK(last_price == 25);
}

}  // namespace

int main() {
    test_basic_fill_price_time_priority();
    test_price_priority_before_time_priority();
    test_market_order();
    test_ioc_partial_and_fok();
    test_cancel();
    test_reject_invalid();
    test_zero_quantity_rejected();
    test_resting_partial_fill_then_consume();
    test_trade_tape_callback();

    if (g_failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d TEST(S) FAILED\n", g_failures);
    return 1;
}
