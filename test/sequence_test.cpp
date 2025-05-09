#include <gtest/gtest.h>
#include "fix/FixParser.h"
#include "fix/FixTags.h"
#include <string>
#include <cstdint>

// ─── Helper ──────────────────────────────────────────────────
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

static std::string buildNOS(int seqNum) {
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

static std::string buildSequenceReset(int seqNum, int newSeqNo) {
    std::string body = "35=4|49=CLIENT1|56=BROKER1|34=" + std::to_string(seqNum) +
        "|52=20240101-09:30:00.000|36=" + std::to_string(newSeqNo) + "|";
    std::string bodyFix;
    for (char c : body) bodyFix += (c == '|') ? fix::SOH : c;
    std::string header = std::string("8=FIX.4.2") + fix::SOH + "9=" +
                          std::to_string(bodyFix.size()) + fix::SOH;
    std::string pre = header + bodyFix;
    return pre + "10=" + computeCS(pre) + fix::SOH;
}

// =================================================================
// TEST: Sequential messages pass
// =================================================================
TEST(SequenceTest, SequentialMessagePass) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    for (int seq = 1; seq <= 5; ++seq) {
        std::string msg = buildNOS(seq);
        fix::ParsedMessage* out = nullptr;
        auto result = parser.parse(msg.c_str(), msg.size(), out);
        ASSERT_EQ(result, fix::ParseResult::OK) << "Failed at seq=" << seq;
        EXPECT_EQ(out->seqNum, seq);
        pool.release(out);
    }
    EXPECT_EQ(parser.nextExpectedSeq(), 6);
}

// =================================================================
// TEST: Gap in sequence returns SEQ_GAP
// =================================================================
TEST(SequenceTest, SequenceGap) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    // Parse seq 1 OK
    std::string msg1 = buildNOS(1);
    fix::ParsedMessage* out = nullptr;
    auto r1 = parser.parse(msg1.c_str(), msg1.size(), out);
    ASSERT_EQ(r1, fix::ParseResult::OK);
    pool.release(out);

    // Skip seq 2, send seq 3 → SEQ_GAP
    std::string msg3 = buildNOS(3);
    out = nullptr;
    auto r3 = parser.parse(msg3.c_str(), msg3.size(), out);
    EXPECT_EQ(r3, fix::ParseResult::SEQ_GAP);
}

// =================================================================
// TEST: Duplicate sequence returns DUPLICATE_SEQ
// =================================================================
TEST(SequenceTest, DuplicateSequence) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg1 = buildNOS(1);
    fix::ParsedMessage* out = nullptr;
    auto r1 = parser.parse(msg1.c_str(), msg1.size(), out);
    ASSERT_EQ(r1, fix::ParseResult::OK);
    pool.release(out);

    // Send seq 1 again → DUPLICATE_SEQ
    out = nullptr;
    auto r2 = parser.parse(msg1.c_str(), msg1.size(), out);
    EXPECT_EQ(r2, fix::ParseResult::DUPLICATE_SEQ);
}

// =================================================================
// TEST: SequenceReset resets expected sequence
// =================================================================
TEST(SequenceTest, SequenceReset) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    // Parse seq 1
    std::string msg1 = buildNOS(1);
    fix::ParsedMessage* out = nullptr;
    (void)parser.parse(msg1.c_str(), msg1.size(), out);
    pool.release(out);
    EXPECT_EQ(parser.nextExpectedSeq(), 2);

    // Send SequenceReset with NewSeqNo=10
    std::string reset = buildSequenceReset(2, 10);
    out = nullptr;
    auto r = parser.parse(reset.c_str(), reset.size(), out);
    ASSERT_EQ(r, fix::ParseResult::OK);
    pool.release(out);

    // Next expected should be 10
    EXPECT_EQ(parser.nextExpectedSeq(), 10);

    // Parse seq 10 should succeed
    std::string msg10 = buildNOS(10);
    out = nullptr;
    auto r10 = parser.parse(msg10.c_str(), msg10.size(), out);
    EXPECT_EQ(r10, fix::ParseResult::OK);
    if (out) pool.release(out);
}

// =================================================================
// TEST: resetSequence() API
// =================================================================
TEST(SequenceTest, ResetSequenceAPI) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    // Parse seq 1
    std::string msg1 = buildNOS(1);
    fix::ParsedMessage* out = nullptr;
    (void)parser.parse(msg1.c_str(), msg1.size(), out);
    pool.release(out);

    EXPECT_EQ(parser.nextExpectedSeq(), 2);

    // Reset
    parser.resetSequence();
    EXPECT_EQ(parser.nextExpectedSeq(), 1);

    // Parse seq 1 again should succeed now
    out = nullptr;
    auto result = parser.parse(msg1.c_str(), msg1.size(), out);
    EXPECT_EQ(result, fix::ParseResult::OK);
    if (out) pool.release(out);
}
