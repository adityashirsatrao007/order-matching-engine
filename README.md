# 🔒 Order Matching Engine

A **low-latency limit order matching engine** with price-time priority,
written in C++17 with zero external dependencies. Built to be a
portfolio-grade demonstration of the systems that power electronic
trading: an immutable order model, a deterministic matching core,
a trade tape, and a WebSocket market-data feed.

```
                     ┌──────────────────────────┐
   Client order  ──▶ │   Matching Engine (C++)  │ ──▶ Trade tape
                     │  price-time priority     │
                     │  FIFO within level       │ ──▶ Order book depth
                     └──────────────────────────┘
                                   │
                                   ▼
                     ┌──────────────────────────┐
                     │   FastAPI + WebSocket    │
                     │   REST /orders /depth    │
                     │   WS /ws (live ticks)    │
                     └──────────────────────────┘
```

## Why this project

Electronic exchanges and trading firms run on matching engines.
This project demonstrates the core engineering problems that recur across
low-latency systems:

- **Price-time priority** — best price first, FIFO within a level
- **Order lifecycle** — GTC / IOC / FOK, partial fills, cancellations, rejects
- **Determinism** — identical inputs produce identical fills (no floats, integer quantities)
- **Observability** — a trade tape + book-depth snapshots via callbacks
- **Latency discipline** — benchmark harness measuring µs/order throughput

## Feature matrix

| Order type | Time-in-force | Support |
|---|---|---|
| `LIMIT`    | GTC / IOC / FOK | ✅ |
| `MARKET`   | GTC / IOC / FOK | ✅ |
| `STOP`     | GTC             | ✅ (triggered on reference price) |
| `STOP_LIMIT` | GTC          | ✅ (triggered on reference price) |

## Quick start

```bash
# 1. Build everything
make

# 2. Run the unit tests (no external framework)
make test

# 3. Interactive CLI — try:  S 10 100  →  B 10 50  →  M B 25  →  X <id>  →  q
./build/ome-cli

# 4. Latency / throughput benchmark (1,000,000 marketable orders)
make bench
```

### Expected benchmark shape (varies by machine)

```
bench: 1000000 marketable orders in 1234.56 ms (1.23 us/order, 810000 orders/sec)
```

## Live market-data feed (Python)

```bash
cd server
pip install -r requirements.txt
uvicorn main:app --reload --port 8000
```

- `POST /orders` — submit an order (JSON)
- `DELETE /orders/{id}` — cancel
- `GET /depth` — current book depth
- `WS /ws` — stream live trades + depth as they happen

## Project layout

```
├── engine/            # C++ core (no external deps)
│   ├── order.h        # order model, enums
│   ├── types.h        # fills, levels, accept results
│   ├── order_book.h   # price-time priority book interface
│   ├── order_book.cpp
│   ├── matching_engine.h / .cpp
│   └── main.cpp       # CLI demo + benchmark
├── tests/test_engine.cpp
├── server/            # FastAPI + WebSocket bridge
├── bench/             # benchmark notes
├── .github/workflows/ci.yml   # build + test on every PR
└── Makefile
```

## What I'd improve next

- Add a resting **stop-order book** (currently stops trigger on the last
  reference price only, which is a simplification)
- Lock-free queue between the engine and the feed publisher
- Persistence/audit log for every accepted order (WAL)
- Real `tcp_nodelay` WebSocket fan-out with per-client backpressure

## License

MIT — see [LICENSE](LICENSE).
