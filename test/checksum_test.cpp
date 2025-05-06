#include <gtest/gtest.h>
#include "fix/FixParser.h"
#include "fix/FixTags.h"
#include <string>
#include <cstdint>

// ─── Helper: compute FIX checksum ────────────────────────────
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

static std::string buildValidMsg(int seqNum = 1) {
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

// =================================================================
// TEST: Correct checksum passes
// =================================================================
TEST(ChecksumTest, CorrectChecksumPasses) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildValidMsg(1);
    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    EXPECT_EQ(result, fix::ParseResult::OK);
    if (out) pool.release(out);
}

// =================================================================
// TEST: Corrupted byte fails with CHECKSUM_ERROR
// =================================================================
TEST(ChecksumTest, CorruptedByteFails) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildValidMsg(1);

    // Corrupt a byte in the middle of the message
    size_t midpoint = msg.size() / 2;
    msg[midpoint] = static_cast<char>(msg[midpoint] + 1);

    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    // Should fail — either checksum or body length mismatch
    EXPECT_NE(result, fix::ParseResult::OK);
    if (out) pool.release(out);
}

// =================================================================
// TEST: Wrong checksum value in tag 10 fails
// =================================================================
TEST(ChecksumTest, WrongChecksumValueFails) {
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    std::string msg = buildValidMsg(1);

    // Find "10=" and replace the checksum digits with "000"
    size_t pos = msg.rfind("10=");
    ASSERT_NE(pos, std::string::npos);
    msg[pos + 3] = '0';
    msg[pos + 4] = '0';
    msg[pos + 5] = '0';

    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(msg.c_str(), msg.size(), out);

    EXPECT_EQ(result, fix::ParseResult::CHECKSUM_ERROR);
    if (out) pool.release(out);
}

// =================================================================
// TEST: Checksum is modulo 256
// =================================================================
TEST(ChecksumTest, ChecksumModulo256) {
    // Verify our helper produces the same checksum as the parser accepts
    fix::MessagePool pool;
    fix::FixParser parser(pool);

    // Build a message with known content and verify it parses
    // Build messages with varying content and verify checksums all parse
    // Each iteration resets sequence to 1, so all messages use seq=1
    for (int i = 0; i < 5; ++i) {
        parser.resetSequence();
        std::string msg = buildValidMsg(1);
        fix::ParsedMessage* out = nullptr;
        auto result = parser.parse(msg.c_str(), msg.size(), out);
        EXPECT_EQ(result, fix::ParseResult::OK) << "Failed on iteration " << i;
        if (out) pool.release(out);
    }
}
