# Crypto Orderbook Aggregator

Aggregate BTCUSDT order books from Binance / OKX / Kraken into a single consolidated book and stream updates over gRPC. Three demo clients are included: **BBO**, **Price Bands**, and **Volume Bands**.

## Project Layout

```
alan_project/
├─ aggregator/         # Aggregator: connects exchange adapters, consolidates books, streams via gRPC
├─ clients/
│  ├─ bbo/             # Prints best bid/offer
│  ├─ price_bands/     # VWAP at BBO-centered ±bps bands
│  └─ volume_bands/    # VWAP when buying nominal quote amounts (partial fills on last level)
├─ common/
│  ├─ adapters.*       # Exchange adapters (HTTP snapshot + WS diffs, reconnect, gap detection)
│  ├─ order_book.*     # Raw order book structure and apply_deltas
│  ├─ consolidator.*   # Consolidation (fixed-point tick indexing, TopN applied after merge)
│  └─ bands.h          # Reusable calculations for BBO / PriceBands / VolumeBands
├─ proto/              # gRPC definitions and generated code
├─ tests/              # gtest unit tests
└─ docker/             # Dockerfiles and docker-compose.yml
```

## Quick Start (Docker recommended)

```bash
# Build images (includes compilation)
docker compose build --no-cache aggregator

# Run tests
docker compose run --rm aggregator ctest -V --test-dir build

# Start the aggregator (background)
docker compose up -d aggregator

# Run the three clients (choose any)
docker compose run --rm client_bbo
docker compose run --rm client_pricebands
docker compose run --rm client_volume
```

## Local Build (optional)

Dependencies: `cmake`, `g++`, `protobuf`, `grpc`, `boost`, `nlohmann-json`, `openssl`

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
ctest --output-on-failure
```

## Example Output

- **client_bbo**: `ts=... bid=100.0@3 ask=100.5@4`
- **client_pricebands**: for each bps band (50/100/200/500/1000), prints VWAP and qty for `+bps` and `-bps`
- **client_volume**: for each nominal notional (1e6/5e6/1e7/2.5e7/5e7), prints VWAP and qty (last level proportionally filled)

> For tidy formatting, use `std::fixed` with an appropriate `std::setprecision(N)` derived from `tick`.

## Architecture & Key Decisions

### Exchange Adapters (`common/adapters.*`)
- **Snapshot + Diffs**: fetch HTTP depth snapshot first, then attach to WS diff streams; validate sequence (e.g., Binance `U/u/pu`), re-snapshot on gaps.
- **Resilience**: TLS for HTTP/WS (Boost.Beast + OpenSSL), backoff reconnects, errors surfaced to the aggregator.
- **Unified callback**: adapter emits `OrderBook` with `std::map<double,double, std::greater/less>` (bids desc / asks asc); `size==0` deletes a level.

### Consolidator (`common/consolidator.*`)
- **Fixed-point tick indexing (core)**  
  We _do not_ use `double` as the key during consolidation. Instead we convert prices to an **integer micro-price domain** and bucket by **integer tick id**:
  - `SCALE = 1e8`, `tick_i = llround(tick * SCALE)`
  - **bids**: `id = floor(p*SCALE) / tick_i`
  - **asks**: `id = (ceil(p*SCALE) + tick_i - 1) / tick_i` → equivalently `ceil_div(p_i, tick_i)`
- **Merge-first, TopN-after**  
  Aggregate all sources into integer buckets first, then apply `TopN` truncation on the merged result (per side).
- **Canonical output**  
  Convert back to display price **once**: `price = (id * tick_i) / SCALE`. This removes floating-point jitter at the root (no mis-bucketing like `9.999` vs `10.0001`), while keeping external interfaces as `double`.
- **Config**: `ConsolidationCfg{ tick, topN }`

### Reusable Bands (`common/bands.h`)
- `compute_bbo(book, ...)` — best bid/ask with presence checks.
- `vwap_asks_to_price(asks, up_px, &qty)` — VWAP by consuming asks ≤ `up_px`.
- `vwap_bids_to_price(bids, dn_px, &qty)` — VWAP by consuming bids ≥ `dn_px`.
- `vwap_for_notional_asks(asks, notional, &qty)` — buy notional quote from lowest asks upward; last level partially filled.

### gRPC API (`proto/`)
- Streaming RPC: `StreamBook(SubscribeRequest) returns (stream ConsolidatedBook)`
- `ConsolidatedBook`: ordered `bids[]` (desc) and `asks[]` (asc), each with `price`/`size`, plus `ts_ms`

## Tests

```bash
docker compose run --rm aggregator ctest -V --test-dir build
```

Coverage highlights:
- **OrderBook**: `apply_deltas` (add/overwrite/delete)
- **Consolidator**: cross-source merge, tick alignment, **TopN after merge**
- **Tick edge cases**: epsilon jitter around boundaries (e.g., `99.999999999` / `100.0000000001`), exact boundary stability
- **Bands**: PriceBands (±bps) and VolumeBands (notional) VWAP & quantities

> (Optional) Coverage reports via `--coverage` or `llvm-cov` can be added if needed.

## Configuration & Parameters

- **tick**: price grid size (e.g., `0.5`). Set in `ConsolidationCfg`.
- **topN**: number of levels to keep per side after consolidation.
- **Symbol mapping (optional)**: a simple YAML for cross-exchange symbol aliases, e.g.:
  ```yaml
  BTCUSDT:
    BINANCE: BTCUSDT
    OKX:     BTC-USDT
    KRAKEN:  XBT/USDT
  ```
  > The current code subscribes a single logical symbol across three exchanges. If YAML is enabled, adaptors can map it to venue-specific symbols. If the key is missing, fail fast.

## Scalability & Robustness

- Threaded adapters; snapshot+diff re-sync on gaps; backoff reconnects.
- Consolidation is `O(M log M)` (ordered maps). Could be optimized with pre-bucketing/vectors if needed.
- gRPC keepalive/backpressure can be tuned via env variables/flags.

## Known Limitations

- Demo focuses on BTCUSDT with three venues. Adding/removing venues currently requires a small code change.
- YAML symbol mapping is documented but not enabled by default.
- CI/coverage is not enabled by default (see TODO).
- Adapter I/O has minimal error isolation for malformed frames (can be extended).

## TODO (prioritized)

**High**
- [ ] Complete this README polish and ensure “one-click” eval works as described.
- [ ] Publish to GitHub (public repo) for reviewers.
- [ ] Offline adapter parsing tests (fixtures):  
  - Binance: `U/u/pu` chaining and gap-triggered re-snapshot;  
  - OKX/Kraken: snapshot→diff merge, zero-qty deletes, duplicate price consolidation.

**Medium**
- [ ] GitHub Actions: build + tests (optional coverage).
- [ ] Client output formatting with `std::fixed` + `setprecision` based on `tick`.
- [ ] Expose `tick`/`topN` via CLI flags or env.

**Low**
- [ ] YAML symbol mapping loader (fail on missing keys).
- [ ] Observability: minimal metrics (aggregation latency, update rate, resnapshot count).
- [ ] Code style & static analysis (`clang-format`, `clang-tidy`).
