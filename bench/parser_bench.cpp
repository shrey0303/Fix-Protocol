#include <benchmark/benchmark.h>
#include "fix/FixParser.h"
#include "fix/FixEncoder.h"
#include "fix/FixTags.h"
#include <string>
#include <map>
#include <cstdint>
#include <vector>
#include <algorithm>

static std::string computeCS(const std::string& msg) {
    uint32_t sum = 0;
    for (char c : msg) sum += static_cast<uint8_t>(c);
    int cs = sum % 256;
    char buf[4];
    buf[0] = '0' + static_cast<char>(cs / 100);
    buf[1] = '0' + static_cast<char>((cs / 10) % 10);
    buf[2] = '0' + static_cast<char>(cs % 10);
    buf[3] = '\0';
    return std::string(buf);
}

static std::string buildNOSMsg(int seqNum = 1) {
    std::string body = "35=D|49=CLIENT1|56=BROKER1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:00.000|11=ORDER001|21=1|38=100|"
        "40=2|44=150.50|54=1|55=BTC/USD|59=0|60=20240101-09:30:00|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" +
                          std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeCS(pre) + fix::SOH;
}

static std::string buildExecMsg(int seqNum = 1) {
    std::string body = "35=8|49=BROKER1|56=CLIENT1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:01.000|6=150.50|11=ORDER001|14=50|"
        "17=EXEC001|31=150.50|32=50|37=ORD001|38=100|39=1|"
        "40=2|44=150.50|54=1|55=BTC/USD|150=1|151=50|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" +
                          std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeCS(pre) + fix::SOH;
}

static std::string buildMDSnapshot(int seqNum = 1, int entries = 5) {
    std::string body = "35=W|49=EXCHANGE|56=CLIENT1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:00.000|55=BTC/USD|268=" + std::to_string(entries) + "|";
    for (int i = 0; i < entries; ++i) {
        body += "269=" + std::string(1, (i % 2 == 0) ? '0' : '1') + "|";
        body += "270=" + std::to_string(100 + i) + ".50|";
        body += "271=" + std::to_string(1000 + i * 100) + "|";
    }
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" +
                          std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeCS(pre) + fix::SOH;
}

// Tail latency helpers — Google Benchmark v1.8.3 takes raw function pointers,
// so no captures. Hardcode the percentile values.
static double pct50(const std::vector<double>& v) {
    auto c = v; std::sort(c.begin(), c.end());
    return c[c.size() / 2];
}
static double pct99(const std::vector<double>& v) {
    auto c = v; std::sort(c.begin(), c.end());
    return c[static_cast<size_t>(static_cast<double>(c.size()) * 0.99)];
}
static double pct999(const std::vector<double>& v) {
    auto c = v; std::sort(c.begin(), c.end());
    return c[static_cast<size_t>(static_cast<double>(c.size()) * 0.999)];
}

// Single L1-hot message, resetSequence per iteration (~1ns store)
static void BM_ParseNewOrderSingle(benchmark::State& state) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);
    std::string msg = buildNOSMsg(1);

    for (auto _ : state) {
        parser.resetSequence();
        fix::ParsedMessage* out = nullptr;
        auto result = parser.parse(msg.c_str(), msg.size(), out);
        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(out);
        if (out) pool.release(out);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ParseNewOrderSingle)
    ->Threads(1)
    ->MinTime(5.0)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p50",  pct50)
    ->ComputeStatistics("p99",  pct99)
    ->ComputeStatistics("p999", pct999);

static void BM_ParseExecutionReport(benchmark::State& state) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);
    std::string msg = buildExecMsg(1);

    for (auto _ : state) {
        parser.resetSequence();
        fix::ParsedMessage* out = nullptr;
        auto result = parser.parse(msg.c_str(), msg.size(), out);
        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(out);
        if (out) pool.release(out);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ParseExecutionReport)
    ->Threads(1)
    ->MinTime(5.0)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p50",  pct50)
    ->ComputeStatistics("p99",  pct99)
    ->ComputeStatistics("p999", pct999);

// MarketDataSnapshot with 5 MD entries — exercises repeating group path
static void BM_ParseMarketDataSnapshot(benchmark::State& state) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);
    std::string msg = buildMDSnapshot(1, 5);

    for (auto _ : state) {
        parser.resetSequence();
        fix::ParsedMessage* out = nullptr;
        auto result = parser.parse(msg.c_str(), msg.size(), out);
        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(out);
        if (out) pool.release(out);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ParseMarketDataSnapshot)
    ->Threads(1)
    ->MinTime(5.0)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p50",  pct50)
    ->ComputeStatistics("p99",  pct99)
    ->ComputeStatistics("p999", pct999);

// QuickFIX-style baseline: models the exact parsing architecture of QuickFIX/n,
// the most widely deployed open-source FIX library.
//   - std::string substr() per field value (heap alloc)
//   - std::map<int, std::string> for field storage (tree alloc + pointer chasing)
//   - std::stoi() for tag parsing (exception-based)
// Real QuickFIX is slower still due to message factory and session layer overhead.
struct QuickFixStyleMessage {
    std::map<int, std::string> fields;
};

static QuickFixStyleMessage quickfixStyleParse(const std::string& msg) {
    QuickFixStyleMessage result;
    size_t pos = 0;
    while (pos < msg.size()) {
        size_t eq = msg.find('=', pos);
        if (eq == std::string::npos) break;
        size_t soh = msg.find(fix::SOH, eq);
        if (soh == std::string::npos) soh = msg.size();
        int tag = std::stoi(msg.substr(pos, eq - pos));
        result.fields[tag] = msg.substr(eq + 1, soh - eq - 1);
        pos = soh + 1;
    }
    return result;
}

static void BM_QuickFixBaseline(benchmark::State& state) {
    std::string msg = buildNOSMsg(1);
    for (auto _ : state) {
        auto result = quickfixStyleParse(msg);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_QuickFixBaseline)
    ->Threads(1)
    ->MinTime(5.0)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p50",  pct50)
    ->ComputeStatistics("p99",  pct99)
    ->ComputeStatistics("p999", pct999);

// Encoder: cached timestamp, natural sequence increment
static void BM_EncoderNewOrderSingle(benchmark::State& state) {
    fix::FixEncoder encoder("CLIENT1", "BROKER1");
    char buf[fix::FixEncoder::BUF_SIZE];
    encoder.refreshTimestamp();

    for (auto _ : state) {
        size_t len = encoder.encodeNewOrderSingle(
            buf, sizeof(buf),
            "ORDER001", "BTC/USD",
            '1', '2', 100, 150.50, '0');
        benchmark::DoNotOptimize(len);
        benchmark::DoNotOptimize(buf);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_EncoderNewOrderSingle)
    ->Threads(1)
    ->MinTime(5.0)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p50",  pct50)
    ->ComputeStatistics("p99",  pct99)
    ->ComputeStatistics("p999", pct999);


