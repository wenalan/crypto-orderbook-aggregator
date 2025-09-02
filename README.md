# README

[![CI (docker-compose)](https://github.com/wenalan/crypto-orderbook-aggregator/actions/workflows/ci.yml/badge.svg)](https://github.com/wenalan/crypto-orderbook-aggregator/actions/workflows/ci.yml)

> A runnable three-exchange (Binance / OKX / Kraken) order book aggregator that exposes a gRPC live consolidated book, plus three sample clients (BBO / Price Bands / Volume Bands). 
> Build, test, and run everything via Docker.

---
## 0) Interpreting the Outputs

- **Merged BBO can look “locked/crossed”, that’s normal.**  
  It takes the **best bid across venues** and the **best ask across venues**. They can come from *different* exchanges, so `best_bid >= best_ask` is possible. This is not a consolidation bug. The counter shows **connected / configured** venues. Immediately after startup it may be `1/3` while adapters connect, snapshot, and resync, give it a few seconds.
  
  Example:
  ```
  agg                  | [AGG] src BBOs [bid/ask]  BINANCE 110110.820000@1.530130/110110.830000@8.477670  OKX 110112.700000@0.076343/110112.800000@1.295506  KRAKEN 110096.100000@0.058000/110097.900000@0.090821  (sources=3/3)
  agg                  | [AGG] merged BBO [bid/ask] 110112.600000@0.076343 / 110097.900000@0.090821
  ```

- **Price Bands may look identical when bands are too wide.**  
  If a band’s upper/lower price already covers all visible depth (`topN`), widening it (e.g., from +50 to +1000 bps) won’t add levels, **qty/VWAP remain unchanged**.  
  For the demo I **reduced** default bands to highlight differences; still, depending on tick alignment/spread, **5 bps** can overlap with **4 bps** and yield the same result.

  Example:
  ```
  client_pricebands-1  | ts=1756800985993 +1bps qty=15.585861 vwap=110111.858607 | -1bps qty=5.918813 vwap=110107.528456
  client_pricebands-1  | ts=1756800985993 +2bps qty=37.288590 vwap=110117.536954 | -2bps qty=28.410474 vwap=110092.053620
  client_pricebands-1  | ts=1756800985993 +3bps qty=64.651990 vwap=110124.042090 | -3bps qty=49.541464 vwap=110085.848624
  client_pricebands-1  | ts=1756800985993 +4bps qty=80.108762 vwap=110127.534850 | -4bps qty=56.544440 vwap=110084.096020
  client_pricebands-1  | ts=1756800985993 +5bps qty=80.108762 vwap=110127.534850 | -5bps qty=56.544440 vwap=110084.096020
  ```

- **Volume Bands plateau when target notional exceeds visible depth.**  
  If the notional is larger than what the current book can fill (also capped by `topN`), **VWAP/qty** will flatten and **filled_notional** stops increasing. The volume band defaults have been revised since the original bands in the question were too large. Without this adjustment, the volume levels would have appeared almost identical.

  ```
  client_volume-1      | ts=1756800985962 notional=50000.000000 qty=0.454111 vwap=110105.212815 filled_notional=50000.000000
  client_volume-1      | ts=1756800985962 notional=100000.000000 qty=0.908200 vwap=110107.932858 filled_notional=100000.000000
  client_volume-1      | ts=1756800985962 notional=200000.000000 qty=1.816375 vwap=110109.416409 filled_notional=200000.000000
  client_volume-1      | ts=1756800985962 notional=500000.000000 qty=4.540901 vwap=110110.306559 filled_notional=500000.000000
  client_volume-1      | ts=1756800985962 notional=1000000.000000 qty=9.081777 vwap=110110.604267 filled_notional=1000000.000000
  ```

---

## 1) Quick Start

### 1.1 Build & Test

```bash
# Build and run all unit tests
# from repo root
cd docker
docker compose build
docker compose run --rm aggregator ctest -V --test-dir build
```

### 1.2 Run the Full Stack

```bash
# Aggregator + three sample clients
# from repo root
cd docker
docker compose build
docker compose up
```

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
  `std::map<double,double>` for bids/asks (price -> size).
- **Consolidator** (`common/consolidator.*`)  
  Bucket by `tick`, cap by `topN`, and output the consolidated book.
- **gRPC** (`proto/bookfeed.proto`)  
  `StreamBook(SubscribeRequest) -> stream ConsolidatedBook`.
- **Sample clients**  
  - `clients/bbo`: consolidated BBO  
  - `clients/price_bands`: VWAP/qty around mid using +/-bps bands  
  - `clients/volume_bands`: VWAP/qty for a given notional

### gRPC endpoint

- Port: `50051`  
- Service: `BookFeed`  
- Method: `StreamBook`  
- **Full path:** `/bookfeed.BookFeed/StreamBook`

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

#### Official references

- **Binance (Spot)** — Diff. Depth stream & local order book maintenance:  
  [Diff. Depth Stream](https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams#diff-depth-stream),  
  [How to manage a local order book correctly](https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams#how-to-manage-a-local-order-book-correctly).  
  Notes:
  - Use the REST depth snapshot (`lastUpdateId`) plus WS diff depth events to build a local book.
  - Spot payloads contain `U`/`u`. Bridge the first buffered event so that `lastUpdateId` is within `[U, u]`, then apply subsequent events in order, updating the local update id to `u`. Older events (`u` <= local id) are discarded.

- **OKX (V5 WebSocket)** — Order book channel: `books` (400 levels). Variants: `books5`, `books50-l2-tbt`, `books-l2-tbt`.  
  [Public Channels – Books](https://app.okx.com/docs-v5/en/#order-book-trading-market-data-ws-order-book-channel).  
  Notes: first push carries `action:"snapshot"` with `prevSeqId = -1`. Subsequent messages are `action:"update"` and must satisfy `prevSeqId == previous seqId`. Checksum/sequence rules are documented in the same section.

- **Kraken (WebSocket v2)** — `book` channel:  
  [Book (Level 2)](https://docs.kraken.com/api/docs/websocket-v2/book)  
  Notes: snapshot-then-updates; see the guide for checksum and recovery:  
  [Spot Websockets (v2) – Book Checksum](https://docs.kraken.com/api/docs/guides/spot-ws-book-v2/).

---

## 4) Project Layout

```
/ (project root)
  aggregator/
    adapter_base.{h,cpp}
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

## 5) Future Work

### 5.1 Adapter abstraction (current & alternatives)
- **Current**: classic OO — `AdapterBase` + virtual methods (`run`, parsing, reconnect, etc.). Simple and extensible.  
- **CRTP (static polymorphism)**: no vtable; hot paths can inline; tighter loops devirtualize; higher template complexity.  
- **C++20 Concepts + `std::visit`**: define adapter Concepts, keep concrete types, and orchestrate via a `std::variant<Binance,OKX,Kraken>` with `std::visit`.

*Trade-off*: OO is simplest; CRTP/Concepts reduce overhead and centralize orchestration but raise template complexity. We can switch if performance becomes critical.

### 5.2 Consistency & ops
- **OKX checksum (`cs`)** after applying deltas; resubscribe on mismatch.  
- **Backtesting**: offline replay (pcap/json) that uses the exact parsing/merge path.  
- **Liveness**: periodic ping/pong, read/write timeouts, and exponential backoff with jitter.

### 5.3 Configuration

- Extract common toggles (trading pair, aggregation depth, emit frequency/TopN, etc.) into a lightweight YAML config, while keeping zero-config as the default. Users can switch pairs or tune display parameters without changing code.

**Example: `config.yaml`**
```yaml
pair: BTC-USDT

exchanges:
  binance:
    enabled: true
    symbol: BTCUSDT
    channel: depth@100ms
  okx:
    enabled: true
    instId: BTC-USDT
    channel: books
  kraken:
    enabled: true
    pair: BTC/USDT
    channel: book

aggregation:
  levels_per_side: 200       # maintained L2 depth per side
  emit_interval_ms: 200      # consolidated book emit cadence (ms)
  tick_size_override: 0.1    # optional normalized tick (e.g., 0.1); null = use exchange precision

clients:
  price_bands:
    bands_bps: [50,100,200,500,1000]  
  volume_bands:
    bands: [1,5,10,25,50]
    scale: M
```
