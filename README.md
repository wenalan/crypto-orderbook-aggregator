# README

> A runnable three-exchange (Binance / OKX / Kraken) order book aggregator that exposes a gRPC live consolidated book, plus three sample clients (BBO / Price Bands / Volume Bands).  
> Build, test, and run everything via Docker.

---

## 1) Quick Start

### 1.1 Build & Test

```bash
# Build and run all unit tests
docker compose build
docker compose run --rm aggregator ctest -V --test-dir build
```

### 1.2 Run the Full Stack

```bash
# Aggregator + three sample clients
docker compose build
docker compose up
```

**gRPC endpoint**

- Port: `50051`  
- Service: `BookFeed`  
- Method: `StreamBook`  
- **Full path:** `/bookfeed.BookFeed/StreamBook`

---

## 2) Architecture Overview

```
┌────────────┐   WS/REST   ┌────────────┐
│ Binance    │────────────▶│            │
├────────────┤             │            │
│ OKX        │────────────▶│ Aggregator │─── gRPC ───▶  clients/*
├────────────┤             │            │
│ Kraken     │────────────▶│            │
└────────────┘             └────────────┘
```

- **Adapter layer** (`aggregator/*_adapter.*`)  
  Each venue inherits `AdapterBase` (`adapter_base.h`), which encapsulates `start/stop` and the thread lifecycle.
- **Order book model** (`common/order_book.*`)  
  `std::map<double,double>` for bids/asks (price → size).
- **Consolidator** (`common/consolidator.*`)  
  Bucket by `tick`, cap by `topN`, and output the consolidated book.
- **gRPC** (`proto/bookfeed.proto`)  
  `StreamBook(SubscribeRequest) -> stream ConsolidatedBook`.
- **Sample clients**  
  - `clients/bbo`: consolidated BBO  
  - `clients/price_bands`: VWAP/qty around mid using ±bps bands  
  - `clients/volume_bands`: VWAP/qty for a given notional

---

## 3) Exchange Strategy

- **Binance — WS-first connect, REST snapshot baseline (required)**  
  1) Open WS first and **buffer** incoming updates (U/u/pu).  
  2) Fetch **REST snapshot (required)** to obtain `lastUpdateId = L`.  
  3) Drop buffered frames with `u <= L`. Find the first buffered frame that **bridges** the gap (`U <= L+1 && u >= L+1`, or `pu == L`), apply it, then apply the remaining buffered frames **in order**; set `last_id = u`.  
  4) Enter steady-state and continue applying live WS updates under the official continuity rules.

- **OKX — WS snapshot first**  
  Subscribe to `books*`; **ignore updates until the WS snapshot arrives** (`action:"snapshot"` or `prevSeqId == -1`). After baseline, require continuity: `prevSeqId == lastSeqId`, otherwise resubscribe.

- **Kraken — WS snapshot first**  
  Same pattern as OKX: wait for snapshot, then apply incremental updates; drop updates seen before snapshot.

---

## 4) Reading the Outputs

- **Crossed BBO (negative spread) can appear**  
  Consolidated best is `max(bid)` and `min(ask)` across venues; they can come from **different exchanges**, so `bid > ask` is a **real-market condition**, not a bug.

- **Price Bands may look unchanged across bps**  
  The consolidated book is depth-capped by `topN` (default **200**). If `+200bps` already consumes all visible levels, widening to `+1000bps` adds no new levels—VWAP/qty remain similar.  
  To increase “resolution,” raise `topN` (e.g., **500**) in `common/consolidator.h`.

- **Volume Bands plateau for large notionals**  
  We output the VWAP/qty you can actually **fill** given current depth; it does not guarantee consuming the entire target notional. When depth is exhausted (also limited by `topN`), results plateau.

---

## 5) Project Layout

```
/ (project root)
  aggregator/
    adapter_base.h(.cpp)
    binance_adapter.{h,cpp}
    okx_adapter.{h,cpp}
    kraken_adapter.{h,cpp}
    main.cpp                # gRPC server entrypoint
  common/
    order_book.{h,cpp}
    consolidator.{h,cpp}
    bands.h                 # price/volume band calculations
  clients/
    bbo/main.cpp
    price_bands/main.cpp
    volume_bands/main.cpp
  proto/
    bookfeed.proto
  tests/
    *.cpp                   # GTest unit tests
docker/
  Dockerfile.aggregator
  Dockerfile.client
docker-compose.yml
```

---

## 6) Future Work

### 6.1 Adapter abstraction (current & alternatives)
- **Current**: classic OO — `AdapterBase` + virtual methods (`run`, parsing, reconnect, etc.). Simple and extensible.  
- **CRTP (static polymorphism)**: no vtable; hot paths can inline; tighter loops devirtualize; higher template complexity.  
- **C++20 Concepts + `std::visit`**: define adapter Concepts, keep concrete types, and orchestrate via a `std::variant<Binance,OKX,Kraken>` with `std::visit`.

*Trade-off*: OO is simplest; CRTP/Concepts reduce overhead and centralize orchestration but raise template complexity. We can switch if performance becomes critical.

### 6.2 Consistency & ops
- **OKX checksum (`cs`)** after applying deltas; resubscribe on mismatch.  
- **Configuration**: make `tick / topN / subscription params / clamp-crossed policy` runtime-configurable.  
- **Backtesting**: offline replay (pcap/json) that uses the exact parsing/merge path.  
- **Liveness & graceful shutdown**: `is_open()` guard before closing, periodic ping/pong, read/write timeouts, and exponential backoff with jitter.

### 6.3 Single canonical symbol via YAML
Maintain one canonical symbol and let the aggregator map it per venue:

```yaml
# symbols.yaml
BTC_USDT:
  BINANCE: BTCUSDT
  OKX: BTC-USDT
  KRAKEN:
    ws:  XBT/USDT
    rest: XBTUSDT
```

Subscribe once with `BTC_USDT`; the aggregator resolves venue-specific wire symbols internally.
