#include <gtest/gtest.h>
#include "fix/FixParser.h"
#include "fix/FixEncoder.h"
#include "fix/FixTags.h"
#include <string>
#include <cstring>
#include <cstdint>

// ─── Helper: replace | with SOH ──────────────────────────────
static std::string toFix(const std::string& readable) {
    std::string result = readable;
    for (auto& c : result) {
        if (c == '|') c = fix::SOH;
    }
    return result;
}

// ─── Helper: compute FIX checksum ────────────────────────────
static std::string computeChecksum(const std::string& msg) {
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

// ─── Helper: build a valid NewOrderSingle with correct checksum ─
static std::string buildNOS(int seqNum = 1) {
    // Body: everything from tag 35 to before tag 10
    std::string body = "35=D|49=CLIENT1|56=BROKER1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:00.000|11=ORDER001|21=1|38=100|"
        "40=2|44=150.50|54=1|55=BTC/USD|59=0|60=20240101-09:30:00|";

    // Convert to SOH
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;

    size_t bodyLen = bodyFix.size();

    // Build header: 8=FIX.4.2 SOH 9=<len> SOH
    std::string header = "8=FIX.4.2";
    header += fix::SOH;
    header += "9=" + std::to_string(bodyLen);
    header += fix::SOH;

    // Compute checksum over header + body
    std::string preChecksum = header + bodyFix;
    std::string cs = computeChecksum(preChecksum);

    // Append trailer
    std::string trailer = "10=";
    trailer += cs;
    trailer += fix::SOH;

    return preChecksum + trailer;
}

// ─── Helper: build a valid ExecutionReport ───────────────────
static std::string buildExecReport(int seqNum = 1) {
    std::string body = "35=8|49=BROKER1|56=CLIENT1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:01.000|6=150.50|11=ORDER001|14=50|"
        "17=EXEC001|31=150.50|32=50|37=ORD001|38=100|39=1|"
        "40=2|44=150.50|54=1|55=BTC/USD|150=1|151=50|";

    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    size_t bodyLen = bodyFix.size();

    std::string header = "8=FIX.4.2";
    header += fix::SOH;
    header += "9=" + std::to_string(bodyLen);
    header += fix::SOH;

    std::string preChecksum = header + bodyFix;
    std::string cs = computeChecksum(preChecksum);

    std::string trailer = "10=";
    trailer += cs;
    trailer += fix::SOH;
    return preChecksum + trailer;
}

// ─── Helper: build Logon ─────────────────────────────────────
static std::string buildLogon(int seqNum = 1) {
    std::string body = "35=A|49=CLIENT1|56=BROKER1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:00:00.000|98=0|108=30|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" + std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeChecksum(pre) + fix::SOH;
}

// ─── Helper: build Heartbeat ─────────────────────────────────
static std::string buildHeartbeat(int seqNum = 1, const std::string& testReqID = "") {
    std::string body = "35=0|49=CLIENT1|56=BROKER1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:00:00.000|";
    if (!testReqID.empty()) body += "112=" + testReqID + "|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" + std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeChecksum(pre) + fix::SOH;
}

// =================================================================
// TEST 1: Correct parse of valid NewOrderSingle
// =================================================================
TEST(ParserTest, ParseValidNewOrderSingle) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildNOS(1);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    ASSERT_EQ(result, fix::ParseResult::OK);
    ASSERT_NE(out, nullptr);

    // Verify message type
    EXPECT_EQ(out->msgType, "D");

    // Verify header fields
    EXPECT_EQ(out->senderCompID, "CLIENT1");
    EXPECT_EQ(out->targetCompID, "BROKER1");
    EXPECT_EQ(out->seqNum, 1);

    // Verify body fields via getField
    EXPECT_EQ(out->getField(fix::tag::ClOrdID), "ORDER001");
    EXPECT_EQ(out->getField(fix::tag::Symbol), "BTC/USD");
    EXPECT_EQ(out->getChar(fix::tag::Side), '1');
    EXPECT_EQ(out->getChar(fix::tag::OrdType), '2');
    EXPECT_EQ(out->getChar(fix::tag::HandlInst), '1');
    EXPECT_EQ(out->getChar(fix::tag::TimeInForce), '0');

    // Typed accessors
    EXPECT_EQ(out->getInt(fix::tag::OrderQty), 100);
    EXPECT_DOUBLE_EQ(out->getDouble(fix::tag::Price), 150.50);

    // hasField
    EXPECT_TRUE(out->hasField(fix::tag::ClOrdID));
    EXPECT_FALSE(out->hasField(fix::tag::ExecID));

    pool.release(out);
}

// =================================================================
// TEST 2: Correct parse of ExecutionReport
// =================================================================
TEST(ParserTest, ParseValidExecutionReport) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildExecReport(1);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    ASSERT_EQ(result, fix::ParseResult::OK);
    ASSERT_NE(out, nullptr);

    EXPECT_EQ(out->msgType, "8");
    EXPECT_EQ(out->getField(fix::tag::ExecID), "EXEC001");
    EXPECT_EQ(out->getField(fix::tag::OrderID), "ORD001");
    EXPECT_EQ(out->getInt(fix::tag::CumQty), 50);
    EXPECT_EQ(out->getInt(fix::tag::LastQty), 50);
    EXPECT_EQ(out->getInt(fix::tag::LeavesQty), 50);
    EXPECT_DOUBLE_EQ(out->getDouble(fix::tag::AvgPx), 150.50);
    EXPECT_DOUBLE_EQ(out->getDouble(fix::tag::LastPx), 150.50);
    EXPECT_EQ(out->getChar(fix::tag::OrdStatus), '1');
    EXPECT_EQ(out->getChar(fix::tag::ExecType), '1');

    pool.release(out);
}

// =================================================================
// TEST 3: Parse Logon message
// =================================================================
TEST(ParserTest, ParseLogon) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildLogon(1);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    ASSERT_EQ(result, fix::ParseResult::OK);
    EXPECT_EQ(out->msgType, "A");
    EXPECT_EQ(out->getInt(fix::tag::EncryptMethod), 0);
    EXPECT_EQ(out->getInt(fix::tag::HeartBtInt), 30);

    pool.release(out);
}

// =================================================================
// TEST 4: Parse Heartbeat with TestReqID
// =================================================================
TEST(ParserTest, ParseHeartbeat) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildHeartbeat(1, "TEST123");
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    ASSERT_EQ(result, fix::ParseResult::OK);
    EXPECT_EQ(out->msgType, "0");
    EXPECT_EQ(out->getField(fix::tag::TestReqID), "TEST123");

    pool.release(out);
}

// =================================================================
// TEST 5: Required field validation — NOS missing Symbol
// =================================================================
TEST(ParserTest, MissingRequiredFieldNOS) {
    // Build a NOS without tag 55 (Symbol)
    std::string body = "35=D|49=CLIENT1|56=BROKER1|34=1|"
        "52=20240101-09:30:00.000|11=ORDER001|21=1|38=100|"
        "40=2|44=150.50|54=1|59=0|60=20240101-09:30:00|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" + std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    std::string msg = pre + "10=" + computeChecksum(pre) + fix::SOH;

    fix::MessagePool pool;
    fix::FixParser parser(pool);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    EXPECT_EQ(result, fix::ParseResult::REQUIRED_FIELD_MISSING);
}

// =================================================================
// TEST 6: Required field validation — ExecReport missing ExecID
// =================================================================
TEST(ParserTest, MissingRequiredFieldExecReport) {
    // Build an ExecReport without tag 17 (ExecID)
    std::string body = "35=8|49=BROKER1|56=CLIENT1|34=1|"
        "52=20240101-09:30:01.000|6=150.50|11=ORDER001|14=50|"
        "31=150.50|32=50|37=ORD001|38=100|39=1|"
        "40=2|44=150.50|54=1|55=BTC/USD|150=1|151=50|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" + std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    std::string msg = pre + "10=" + computeChecksum(pre) + fix::SOH;

    fix::MessagePool pool;
    fix::FixParser parser(pool);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    EXPECT_EQ(result, fix::ParseResult::REQUIRED_FIELD_MISSING);
}

// =================================================================
// TEST 7: Partial message → BUFFER_TOO_SMALL
// =================================================================
TEST(ParserTest, PartialMessage) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string fullMsg = buildNOS(1);
    // Send only first half
    size_t halfLen = fullMsg.size() / 2;

    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(fullMsg.c_str(), halfLen, out);

    EXPECT_EQ(result, fix::ParseResult::BUFFER_TOO_SMALL);
    EXPECT_EQ(parser.bytesConsumed(), 0u);
}

// =================================================================
// TEST 8: Back-to-back messages
// =================================================================
TEST(ParserTest, BackToBackMessages) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg1 = buildNOS(1);
    std::string msg2 = buildNOS(2);
    std::string msg3 = buildNOS(3);
    std::string combined = msg1 + msg2 + msg3;

    const char* buf = combined.c_str();
    size_t remaining = combined.size();
    size_t offset = 0;

    for (int i = 0; i < 3; ++i) {
        fix::ParsedMessage* out = nullptr;
        auto result = parser.parse(buf + offset, remaining, out);
        ASSERT_EQ(result, fix::ParseResult::OK) << "Failed on message " << (i + 1);
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(out->seqNum, i + 1);

        size_t consumed = parser.bytesConsumed();
        EXPECT_GT(consumed, 0u);
        offset += consumed;
        remaining -= consumed;

        pool.release(out);
    }
}

// =================================================================
// TEST 9: Pool exhaustion
// =================================================================
TEST(ParserTest, PoolExhaustion) {
    fix::MessagePool pool;

    // Acquire all slots
    fix::ParsedMessage* msgs[fix::MessagePool::CAPACITY];
    for (size_t i = 0; i < fix::MessagePool::CAPACITY; ++i) {
        msgs[i] = pool.acquire();
        ASSERT_NE(msgs[i], nullptr) << "Failed to acquire slot " << i;
    }

    // Pool should be exhausted
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.acquire(), nullptr);

    // Release one slot
    pool.release(msgs[0]);
    EXPECT_EQ(pool.available(), 1u);

    // Should be able to acquire again
    auto* reclaimed = pool.acquire();
    EXPECT_NE(reclaimed, nullptr);
    pool.release(reclaimed);

    // Release remaining
    for (size_t i = 1; i < fix::MessagePool::CAPACITY; ++i) {
        pool.release(msgs[i]);
    }
    EXPECT_EQ(pool.available(), fix::MessagePool::CAPACITY);
}

// =================================================================
// TEST 10: MISSING_HEADER — tag 8 not first
// =================================================================
TEST(ParserTest, MissingHeader) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = toFix("35=D|8=FIX.4.2|9=5|10=000|");
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    EXPECT_EQ(result, fix::ParseResult::MISSING_HEADER);
}

// =================================================================
// TEST 11: Zero allocation verification
// =================================================================
// We track allocations via a global counter.
static int64_t g_allocCount = 0;
static bool g_trackAlloc = false;

// Override global operator new to count allocations
void* operator new(std::size_t size) {
    if (g_trackAlloc) ++g_allocCount;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

TEST(ParserTest, ZeroAllocationVerification) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    // Pre-build the message (allocation happens here, before tracking)
    std::string msg = buildNOS(1);

    // Reset and start tracking
    g_allocCount = 0;
    g_trackAlloc = true;

    // Parse 10,000 messages — must have zero allocations
    for (int i = 0; i < 10000; ++i) {
        fix::ParsedMessage* out = nullptr;
        parser.resetSequence();
        auto result = parser.parse(msg.c_str(), msg.size(), out);
        ASSERT_EQ(result, fix::ParseResult::OK);
        pool.release(out);
    }

    g_trackAlloc = false;

    EXPECT_EQ(g_allocCount, 0)
        << "Hot path allocated " << g_allocCount << " times during 10,000 parses";
}

// =================================================================
// TEST 12: Encoder round-trip
// =================================================================
TEST(ParserTest, EncoderRoundTrip) {
    fix::FixEncoder encoder("CLIENT1", "BROKER1");
    char buf[fix::FixEncoder::BUF_SIZE];

    size_t len = encoder.encodeNewOrderSingle(
        buf, sizeof(buf),
        "ORD001", "BTC/USD",
        '1',  // Buy
        '2',  // Limit
        100,
        150.50,
        '0'   // Day
    );
    ASSERT_GT(len, 0u);

    // Parse the encoded message
    fix::MessagePool pool;
    fix::FixParser parser(pool);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(buf, len, out);

    ASSERT_EQ(result, fix::ParseResult::OK);
    EXPECT_EQ(out->msgType, "D");
    EXPECT_EQ(out->getField(fix::tag::ClOrdID), "ORD001");
    EXPECT_EQ(out->getField(fix::tag::Symbol), "BTC/USD");
    EXPECT_EQ(out->getInt(fix::tag::OrderQty), 100);

    pool.release(out);
}

// TEST 13: MarketDataSnapshotFullRefresh with repeating groups
static std::string buildMarketDataSnapshot(int seqNum = 1, int numEntries = 3) {
    std::string body = "35=W|49=EXCHANGE|56=CLIENT1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:00.000|55=BTC/USD|268=" + std::to_string(numEntries) + "|";

    // Build MD entries: alternating bid/offer
    for (int i = 0; i < numEntries; ++i) {
        char type = (i % 2 == 0) ? '0' : '1'; // 0=Bid, 1=Offer
        body += "269=" + std::string(1, type) + "|";
        body += "270=" + std::to_string(100 + i) + ".50|";
        body += "271=" + std::to_string(1000 + i * 100) + "|";
    }

    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" +
                          std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeChecksum(pre) + fix::SOH;
}

TEST(ParserTest, ParseMarketDataSnapshot) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildMarketDataSnapshot(1, 3);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    ASSERT_EQ(result, fix::ParseResult::OK);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->msgType, "W");
    EXPECT_EQ(out->getField(fix::tag::Symbol), "BTC/USD");

    // Verify repeating group was parsed
    ASSERT_EQ(out->groupCount, 1);
    const auto* grp = out->getGroup(fix::tag::NoMDEntries);
    ASSERT_NE(grp, nullptr);
    EXPECT_EQ(grp->countTag, fix::tag::NoMDEntries);
    EXPECT_EQ(grp->delimTag, fix::tag::MDEntryType);
    EXPECT_EQ(grp->count, 3);

    // Verify entry 0: Bid at 100.50, size 1000
    EXPECT_EQ(grp->entryCounts[0], 3);
    EXPECT_EQ(grp->entries[0][0].tag, fix::tag::MDEntryType);
    EXPECT_EQ(grp->entries[0][0].value, "0");
    EXPECT_EQ(grp->entries[0][1].tag, fix::tag::MDEntryPx);
    EXPECT_EQ(grp->entries[0][1].value, "100.50");
    EXPECT_EQ(grp->entries[0][2].tag, fix::tag::MDEntrySize);

    // Verify entry 1: Offer
    EXPECT_EQ(grp->entries[1][0].value, "1");

    // Verify entry 2: Bid
    EXPECT_EQ(grp->entries[2][0].value, "0");

    pool.release(out);
}

// TEST 14: Invalid repeating group count
TEST(ParserTest, InvalidRepeatingGroupCount) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    // Declare 3 entries but only provide 2
    std::string body = "35=W|49=EXCHANGE|56=CLIENT1|34=1|"
        "52=20240101-09:30:00.000|55=BTC/USD|268=3|"
        "269=0|270=100.50|271=1000|"
        "269=1|270=101.50|271=2000|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" +
                          std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    std::string msg = pre + "10=" + computeChecksum(pre) + fix::SOH;

    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);
    EXPECT_EQ(result, fix::ParseResult::INVALID_REPEATING_GROUP);
}

// TEST 15: TcpMessageBuffer — fragmented delivery
#include "fix/TcpMessageBuffer.h"

TEST(ParserTest, TcpMessageBufferFragmented) {
    fix::TcpMessageBuffer tcpBuf;
    std::string msg = buildNOS(1);

    // Feed message in 3 fragments
    size_t third = msg.size() / 3;
    tcpBuf.append(msg.c_str(), third);
    tcpBuf.append(msg.c_str() + third, third);
    tcpBuf.append(msg.c_str() + 2 * third, msg.size() - 2 * third);

    size_t msgLen = 0;
    const char* extracted = tcpBuf.nextMessage(msgLen);
    ASSERT_NE(extracted, nullptr);
    EXPECT_EQ(msgLen, msg.size());

    // Parse the extracted message
    fix::MessagePool pool;
    fix::FixParser parser(pool);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(extracted, msgLen, out);
    ASSERT_EQ(result, fix::ParseResult::OK);
    EXPECT_EQ(out->msgType, "D");
    pool.release(out);
}

// TEST 16: TcpMessageBuffer — two messages in one recv()
TEST(ParserTest, TcpMessageBufferBackToBack) {
    fix::TcpMessageBuffer tcpBuf;
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg1 = buildNOS(1);
    std::string msg2 = buildExecReport(2);
    std::string combined = msg1 + msg2;

    tcpBuf.append(combined.c_str(), combined.size());

    // Extract first message
    size_t len1 = 0;
    const char* p1 = tcpBuf.nextMessage(len1);
    ASSERT_NE(p1, nullptr);

    fix::ParsedMessage* out = nullptr;
    auto r1 = parser.parse(p1, len1, out);
    ASSERT_EQ(r1, fix::ParseResult::OK);
    EXPECT_EQ(out->msgType, "D");
    pool.release(out);

    // Extract second message
    size_t len2 = 0;
    const char* p2 = tcpBuf.nextMessage(len2);
    ASSERT_NE(p2, nullptr);

    out = nullptr;
    auto r2 = parser.parse(p2, len2, out);
    ASSERT_EQ(r2, fix::ParseResult::OK);
    EXPECT_EQ(out->msgType, "8");
    pool.release(out);

    // No more messages
    size_t len3 = 0;
    EXPECT_EQ(tcpBuf.nextMessage(len3), nullptr);
}


