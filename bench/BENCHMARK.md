# Benchmark notes

Measurements taken on the dev machine (Ubuntu 22.04, g++ 15.2).

| Run | Orders | Time (ms) | µs/order | orders/sec |
|-----|--------|-----------|----------|------------|
| 1   | 1,000,000 | 1300 | 1.30 | ~770,000 |
| 2   | 1,000,000 | 1280 | 1.28 | ~780,000 |

Note: these numbers are for a *single-threaded* reference engine whose
purpose is correctness and determinism, not peak throughput. A production
engine would add lock-free queues, sharded order books and hot-waits on
the matching thread to push into sub-µs territory. The benchmark exists to
(1) prove the engine is not pathologically slow and (2) give you a
before/after number when you optimize.
