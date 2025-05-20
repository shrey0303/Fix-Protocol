# FIX Protocol Parser <img src="https://img.shields.io/badge/version-1.0.0-blue.svg"/>

<div>
  <img style="display: inline-block" src="https://img.shields.io/badge/C%2B%2B17-00599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white">
  <img style="display: inline-block" src="https://img.shields.io/badge/Zero--Allocation-green.svg?style=for-the-badge">
  <img style="display: inline-block" src="https://img.shields.io/badge/FIX%204.2%2F4.4-f38a3f.svg?style=for-the-badge">
  <img style="display: inline-block" src="https://img.shields.io/badge/Google%20Benchmark-yellow.svg?style=for-the-badge">
  <img style="display: inline-block" src="https://img.shields.io/badge/LibFuzzer-red.svg?style=for-the-badge">
  <img style="display: inline-block" src="https://img.shields.io/badge/License-MIT-brightgreen.svg?style=for-the-badge">
</div>

<br/>

Zero-allocation FIX message parser and encoder for latency-sensitive trading systems.

- Parses a fully validated NewOrderSingle in **276 ns** on Linux — checksum, body length, sequence number, required fields all verified.
- **0 bytes** heap-allocated per parse (verified by global `operator new` override counting 10,000 parses).
- 1,618 lines of implementation · 853 lines of tests · 25 test cases · fuzz target included.

## Architecture

### Parse pipeline

```
TCP recv()
    │
    ▼
┌──────────────────┐
│ TcpMessageBuffer │  Accumulates byte stream, extracts complete messages
│  (65 KB linear)  │  using body-length-based boundary detection.
│                  │  Auto-compacts at 50% read position (Aeron strategy).
└────────┬─────────┘
         │ const char* buf, size_t len
         ▼
┌──────────────────┐
│   FixParser      │  Single-pass parse loop:
│                  │
│  1. Structural   │  Tag 8 first, tag 9 second → compute tag 10 position
│     validation   │  deterministically from body length. No SOH scanning
│                  │  for "10=" — immune to adversarial field values.
│                  │
│  2. Checksum     │  Sum all bytes mod 256. Compare against declared tag 10.
│                  │  Returns CHECKSUM_ERROR on mismatch.
│                  │
│  3. Field parse  │  Linear scan: tag=value pairs into FieldView[64] array.
│                  │  All values are string_view into caller's buffer.
│                  │  No allocation, no copy.
│                  │
│  4. Repeating    │  Data-driven GroupSpec table. Delimiter-tag detection.
│     groups       │  32 entries × 16 fields pre-allocated per group.
│                  │
│  5. Header       │  Extract msgType, sender/target, seqNum, sendingTime
│     extraction   │  into named struct fields for O(1) access.
│                  │
│  6. Sequence     │  Per-session expected sequence tracking.
│     tracking     │  Returns SEQ_GAP or DUPLICATE_SEQ on violations.
│                  │  Handles SequenceReset (35=4).
│                  │
│  7. Required     │  Per-message-type field validation via constexpr
│     field check  │  tag arrays. Returns REQUIRED_FIELD_MISSING.
│                  │
└────────┬─────────┘
         │ ParsedMessage* (from pool)
         ▼
┌──────────────────┐
│  MessagePool     │  1024-slot slab allocator, 64-byte aligned.
│  (single-writer) │  acquire()/release() are O(1) pointer swaps.
│                  │  No malloc, no lock, no atomic on hot path.
└──────────────────┘
```

### Why each design decision exists

| Decision | Why | Tradeoff |
|----------|-----|----------|
| `string_view` over `std::string` | Parsing 15 fields into `std::string` means 15 heap allocations per message (above SSO threshold). `string_view` eliminates all of them. | Caller must keep the raw buffer alive while `ParsedMessage` is in use. Dangling views are silent. Documented on the struct. |
| Linear field scan over hash map | N ≤ 64 fields in contiguous `FieldView[]` fits in ~4 cache lines. Sequential scan with hardware prefetching beats hash map's bucket lookup + pointer chasing at this working set size. Hash map wins at N > ~200. FIX messages never have 200 fields. | O(N) worst case per lookup vs O(1) amortised for hash map. Acceptable because N is bounded. |
| Slab pool over `malloc` | `malloc` in multithreaded contexts takes a lock, maintains free-list metadata, and has unpredictable latency from page faults and coalescing. The pool pre-allocates everything at startup. `acquire()` is two pointer operations with deterministic O(1) latency. | Fixed capacity (1024 slots). Pool exhaustion returns `POOL_EXHAUSTED` — caller must handle backpressure. |
| Body-length boundary detection | Naive parsers scan for `SOH + "10="` to find the checksum tag. A malicious or corrupted field value containing that byte sequence causes a mismatch. Using tag 9's declared body length to compute the exact byte offset of tag 10 is immune to this. | Requires tag 9 to be correct. A corrupted body length causes `BODY_LENGTH_MISMATCH`, which is the correct failure mode. |
| `noexcept` everywhere | Exception handling has metadata overhead even when exceptions are not thrown — unwinding tables consume space and affect instruction cache. `noexcept` lets the compiler generate tighter code. | Error signaling via `ParseResult` enum. Caller must check return values (`[[nodiscard]]` enforces this). |
| `alignas(64)` on `ParsedMessage` | 64 bytes is the cache line width on x86. Misaligned structs can span two cache lines, requiring two L1 fetches for a single access. | Wastes up to 63 bytes of padding per object. Acceptable for a 1024-slot pool. |
| Single-pass encoder with header backfill | FIX requires tag 8 (BeginString) and tag 9 (BodyLength) before the body, but body length isn't known until the body is written. Writing the body at a reserved offset and backfilling the header avoids a two-pass approach. | One `memmove` of ~170 bytes (L1-hot). Alternative: gather-write (`writev`) at the network layer. |


## Benchmark Results

### Linux (WSL2)

**Environment**: Ubuntu 24.04 (WSL2) · GCC 13.3.0 · `-O2 -march=native` · 12th Gen Intel (12 cores, 2496 MHz, 48KB L1D, 1280KB L2, 12MB L3)

| Benchmark                | CPU Time | Throughput     |
|--------------------------|----------|----------------|
| ParseNewOrderSingle      | 276 ns   | 3.62 M msg/s   |
| ParseExecutionReport     | 382 ns   | 2.62 M msg/s   |
| ParseMarketDataSnapshot  | 281 ns   | 3.55 M msg/s   |
| EncoderNewOrderSingle    | 239 ns   | 4.18 M msg/s   |
| QuickFIX-style baseline  | 894 ns   | 1.12 M msg/s   |

Allocation per parse: **0 bytes** (verified by global `operator new` override counting 10,000 parses)

### Methodology

- All numbers are CPU time from a single `--benchmark_min_time=5s` run. CPU time excludes OS scheduling jitter.
- Single ~170-byte message buffer stays L1-hot. This measures parse throughput, not memory subsystem performance.
- `resetSequence()` called per iteration (~1ns store). Benchmark measures parse, not sequence tracking.
- Encoder uses cached timestamp and natural sequence increment. No `system_clock::now()` syscall in the loop.
- Checksum and body-length validation included in every parse — these are not optional and not benchmarked separately.

**About the QuickFIX baseline**: The baseline replicates QuickFIX's parsing architecture — `std::string` substr per field, `std::map<int, std::string>` storage, `std::stoi` for tag parsing, heap allocation per parse. It is a parser-core comparison only. Scope excludes QuickFIX's session layer, message factory, and FIX dictionary validation. The 3.2x speedup on Linux (vs 21.3x on Windows) reflects glibc's superior allocator performance — the baseline gets faster, not the parser getting slower. Real QuickFIX is slower still due to those additional layers.

## Supported Message Types

| Tag 35 | Message Type                  | Required Fields Validated |
|--------|-------------------------------|--------------------------|
| `D`    | NewOrderSingle                | ✅ 7 fields |
| `8`    | ExecutionReport               | ✅ 14 fields |
| `F`    | OrderCancelRequest            | ✅ 7 fields |
| `G`    | OrderCancelReplaceRequest     | ✅ 10 fields |
| `W`    | MarketDataSnapshotFullRefresh | ✅ 1 field + repeating groups |
| `A`    | Logon                         | ✅ 2 fields |
| `3`    | Reject                        | ✅ 1 field |
| `0`    | Heartbeat                     | — |
| `1`    | TestRequest                   | — |
| `2`    | ResendRequest                 | — |
| `4`    | SequenceReset                 | — |
| `5`    | Logout                        | — |

## Features

### Parser
- [x] Zero-copy field access (`string_view` over raw buffer)
- [x] Body-length-based boundary detection (immune to adversarial payloads)
- [x] Checksum validation (tag 10, sum mod 256)
- [x] Sequence number tracking with gap/duplicate detection
- [x] SequenceReset (35=4) handling
- [x] Per-message-type required field validation
- [x] Repeating group parsing (data-driven `GroupSpec` table)
- [x] Partial message handling (`BUFFER_TOO_SMALL` + `bytesConsumed()`)
- [x] Field overflow detection (`BUFFER_OVERFLOW` when `fieldCount >= MAX_FIELDS`)

### Encoder
- [x] NewOrderSingle, OrderCancelRequest, Logon, Heartbeat
- [x] Single-pass with header backfill (`memmove` to `buf[0]`)
- [x] `std::to_chars` for all integer formatting (zero-alloc, no locale)
- [x] Cached timestamp (call `refreshTimestamp()` from timer thread, not per-message)
- [x] Automatic sequence number increment

### Infrastructure
- [x] Pre-allocated slab pool (1024 slots, 64-byte aligned)
- [x] TCP stream reassembly buffer (65KB, auto-compact at 50%)
- [x] 25 Google Test cases (parse, checksum, sequence, repeating groups, TCP framing)
- [x] LibFuzzer target
- [x] Google Benchmark suite with P50/P99/P99.9 statistics
- [x] Zero-allocation verification test (global `operator new` override)
- [ ] Per-message tail latency via `rdtsc` (outside Google Benchmark's model)
- [ ] Multi-threaded benchmark (parser is single-writer by design)

## Building

```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build && cmake --build build --config Release
```

## Running Tests

```bash
./build/fix_tests    # 25 tests: parse, checksum, sequence, repeating groups, TCP framing
```

## Running Benchmarks

```bash
./build/fix_bench --benchmark_min_time=5s
./build/fix_bench --benchmark_min_time=2s --benchmark_repetitions=10  # inter-run variance
```

## Project Structure

```
fix-parser-cpp/                         1,618 lines implementation
├── CMakeLists.txt                      853 lines tests
├── include/fix/
│   ├── FixTags.h                       Tag constants (constexpr)
│   ├── FixMessage.h                    ParsedMessage, RepeatingGroup, ParseResult
│   ├── FixParser.h                     Parser with repeating group support
│   ├── FixValidator.h                  Required field validation
│   ├── FixEncoder.h                    Outbound message encoder
│   ├── MessagePool.h                   Slab allocator (single-writer, NOT thread-safe)
│   └── TcpMessageBuffer.h             TCP stream reassembly (NOT thread-safe)
├── src/
│   ├── FixParser.cpp                   Parse loop + repeating groups
│   ├── FixValidator.cpp                Per-message-type field validation
│   └── FixEncoder.cpp                  FIX encoder (zero-alloc, cached timestamp)
├── bench/
│   └── parser_bench.cpp                Google Benchmark (P50/P99/P99.9)
├── test/
│   ├── parser_test.cpp                 16 tests (parse, groups, TCP buffer)
│   ├── checksum_test.cpp               4 checksum tests
│   └── sequence_test.cpp               5 sequence tests
└── fuzz/
    └── fuzz_parser.cpp                 LibFuzzer target
```

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
