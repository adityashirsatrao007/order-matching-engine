# Case Study — Order Matching Engine (C++17)

**Role:** Creator / Lead Engineer · **Benchmark:** ~1.3 µs/order — ~800K orders/sec single-threaded (bench/BENCHMARK.md)

## 1. The problem

The core of any electronic exchange is the matching engine: match buy and sell orders to produce trades in microseconds. Exchanges run matching with **price-time priority** — best price first, FIFO within a level — and the implementation has to be deterministic, auditable, and fast. Most teaching implementations are either toy-level or hidden inside a single giant event loop.

**Goal:** a self-contained, low-latency limit-order matching engine with zero external dependencies, that you can bench, extend, and wrap in a live API in minutes.

## 2. Approach

A small, deterministic design rather than a monolith:

- **Immutable order model** — every order carries a unique id, side, price, quantity, type, and timestamp. No shared mutable state bleeds across the core.
- **Price → FIFO queue maps** for bids and asks, giving O(log n) best-price lookup with FIFO within level.
- **Deterministic matching core** — matching runs the same way every time: compare best price, consume against FIFO queues, emit fills to a trade tape. This is what makes a matching engine auditable.
- **Order lifecycle** — GTC (rest until filled/cancelled), IOC (fill what it can, cancel the rest), FOK (all-or-nothing) at fill time.
- **Live feed** — FastAPI exposes `POST /orders`, cancellation, and order-book depth, plus a **WebSocket** stream of trade tape/ticks, so a front-end can show live market data.

```
   REST /orders ──▶  Matching Engine (C++)  ──▶ trade tape
                    price-time + FIFO           ──▶ order book depth
                          │
                   WebSocket /ws ──▶ live ticks
```

## 3. Why C++17 with zero dependencies

- One `g++ -std=c++17` invocation builds the CLI; the binary can move anywhere.
- Manual control over memory layout/allocations is what buys the ~800K orders/sec figure in the bench (single-threaded).
- The engine compiles to a standalone CLI (`--bench`) so performance is measurable and reproducible by anyone cloning the repo.

## 4. Results

- **~1.3 µs/order (~800K orders/sec)** single-threaded in the bundled bench — method, hardware and two timed runs recorded in `bench/BENCHMARK.md`.
- Full REST + WebSocket API on top, so the exact same engine powers a live trading-demo UI.
- Deterministic matching semantics — same inputs, same fills, every run (the property exchanges are legally required to have).

## 5. What I'd do differently

- NUMA-aware allocation and prefetching for cache-locality tuning at the 10^6 orders/sec scale.
- A true event-loop/actor layer to pipeline inbound order submission over the lock-free core.
- Order book snapshotting + journaling for crash-replay (production exchanges persist the tape).

## 6. What recruiters should ask in interviews

How FIFO-within-level is guaranteed, why price-time priority prevents front-running, the cost-model of the selected data structures, and how we benchmark ~800K orders/sec reproducibly.