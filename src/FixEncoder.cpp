#include "fix/FixEncoder.h"
#include "fix/FixTags.h"
#include <cstring>
#include <charconv>
#include <chrono>
#include <ctime>

namespace fix {

FixEncoder::FixEncoder(std::string_view senderCompID,
                       std::string_view targetCompID) noexcept {
    senderLen_ = (senderCompID.size() < sizeof(sender_))
                     ? senderCompID.size() : sizeof(sender_) - 1;
    std::memcpy(sender_, senderCompID.data(), senderLen_);
    sender_[senderLen_] = '\0';

    targetLen_ = (targetCompID.size() < sizeof(target_))
                     ? targetCompID.size() : sizeof(target_) - 1;
    std::memcpy(target_, targetCompID.data(), targetLen_);
    target_[targetLen_] = '\0';

    refreshTimestamp();
}

// std::to_chars — C++17, zero-alloc, no locale, no exceptions.
// Replaces all manual digit-reversal loops.

size_t FixEncoder::appendField(char* buf, size_t pos,
                                size_t maxLen, int tag,
                                std::string_view value) noexcept {
    char tagBuf[12];
    auto [tagPtr, tagEc] = std::to_chars(tagBuf, tagBuf + sizeof(tagBuf), tag);
    if (FIX_UNLIKELY(tagEc != std::errc{})) return 0;
    size_t tagLen = static_cast<size_t>(tagPtr - tagBuf);

    size_t needed = tagLen + 1 + value.size() + 1; // tag=value SOH
    if (FIX_UNLIKELY(pos + needed > maxLen)) return 0;

    std::memcpy(buf + pos, tagBuf, tagLen);
    pos += tagLen;
    buf[pos++] = '=';
    std::memcpy(buf + pos, value.data(), value.size());
    pos += value.size();
    buf[pos++] = SOH;
    return pos;
}

size_t FixEncoder::appendField(char* buf, size_t pos,
                                size_t maxLen, int tag,
                                int64_t value) noexcept {
    char tmp[24];
    auto [ptr, ec] = std::to_chars(tmp, tmp + sizeof(tmp), value);
    if (FIX_UNLIKELY(ec != std::errc{})) return 0;
    return appendField(buf, pos, maxLen, tag,
                       std::string_view(tmp, static_cast<size_t>(ptr - tmp)));
}

size_t FixEncoder::appendField(char* buf, size_t pos,
                                size_t maxLen, int tag,
                                double value, int decimals) noexcept {
    // Fixed-point: scale to integer, format integer+frac parts
    char valBuf[32];
    int valLen = 0;
    bool neg = value < 0.0;
    if (neg) { value = -value; valBuf[valLen++] = '-'; }

    int64_t mult = 1;
    for (int d = 0; d < decimals; ++d) mult *= 10;
    int64_t scaled = static_cast<int64_t>(value * static_cast<double>(mult) + 0.5);

    // Integer part via to_chars
    char intBuf[20];
    auto [intPtr, intEc] = std::to_chars(intBuf, intBuf + sizeof(intBuf),
                                          scaled / mult);
    if (FIX_UNLIKELY(intEc != std::errc{})) return 0;
    size_t intLen = static_cast<size_t>(intPtr - intBuf);
    std::memcpy(valBuf + valLen, intBuf, intLen);
    valLen += static_cast<int>(intLen);

    // Decimal point + fractional part (zero-padded)
    valBuf[valLen++] = '.';
    int64_t frac = scaled % mult;
    for (int d = decimals - 1; d >= 0; --d) {
        int64_t p = 1;
        for (int k = 0; k < d; ++k) p *= 10;
        valBuf[valLen++] = '0' + static_cast<char>((frac / p) % 10);
    }

    return appendField(buf, pos, maxLen, tag,
                       std::string_view(valBuf, static_cast<size_t>(valLen)));
}

size_t FixEncoder::appendCharField(char* buf, size_t pos,
                                    size_t maxLen, int tag,
                                    char value) noexcept {
    return appendField(buf, pos, maxLen, tag, std::string_view(&value, 1));
}

// Timestamp: YYYYMMDD-HH:MM:SS.mmm
// Called off hot path by refreshTimestamp(). system_clock::now() is
// a syscall — never call this per-message in production.

size_t FixEncoder::writeTimestamp(char* buf, size_t maxLen) noexcept {
    if (FIX_UNLIKELY(maxLen < 21)) return 0;

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    int millis = static_cast<int>(ms % 1000);

    struct tm t{};
#ifdef _WIN32
    gmtime_s(&t, &sec);
#else
    gmtime_r(&sec, &t);
#endif

    auto w2 = [](char* p, int v) {
        p[0] = '0' + static_cast<char>(v / 10);
        p[1] = '0' + static_cast<char>(v % 10);
    };

    // Year
    int y = t.tm_year + 1900;
    buf[0] = '0' + static_cast<char>(y / 1000);
    buf[1] = '0' + static_cast<char>((y / 100) % 10);
    buf[2] = '0' + static_cast<char>((y / 10) % 10);
    buf[3] = '0' + static_cast<char>(y % 10);
    w2(buf + 4, t.tm_mon + 1);
    w2(buf + 6, t.tm_mday);
    buf[8] = '-';
    w2(buf + 9, t.tm_hour);
    buf[11] = ':';
    w2(buf + 12, t.tm_min);
    buf[14] = ':';
    w2(buf + 15, t.tm_sec);
    buf[17] = '.';
    buf[18] = '0' + static_cast<char>(millis / 100);
    buf[19] = '0' + static_cast<char>((millis / 10) % 10);
    buf[20] = '0' + static_cast<char>(millis % 10);

    return 21;
}

void FixEncoder::refreshTimestamp() noexcept {
    cachedTsLen_ = writeTimestamp(cachedTimestamp_, sizeof(cachedTimestamp_));
}

size_t FixEncoder::writeHeaderFields(char* buf, size_t pos,
                                      size_t maxLen,
                                      std::string_view msgType) noexcept {
    pos = appendField(buf, pos, maxLen, tag::MsgType, msgType);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(buf, pos, maxLen, tag::SenderCompID,
                       std::string_view(sender_, senderLen_));
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(buf, pos, maxLen, tag::TargetCompID,
                       std::string_view(target_, targetLen_));
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(buf, pos, maxLen, tag::MsgSeqNum, seqNum_);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(buf, pos, maxLen, tag::SendingTime,
                       std::string_view(cachedTimestamp_, cachedTsLen_));
    if (FIX_UNLIKELY(pos == 0)) return 0;
    return pos;
}

// Single-pass with backfill: body written at HEADER_RESERVE offset,
// header computed after body is done, memmove to buf[0].
// Tradeoff: avoids two-pass at cost of one L1-hot memmove (~170B for NOS).
// Alternative: gather-write (writev) at the network layer.

size_t FixEncoder::finalise(char* buf, size_t bodyStart,
                             size_t bodyEnd, size_t maxLen) noexcept {
    size_t bodyLen = bodyEnd - bodyStart;

    char header[64];
    size_t hPos = 0;

    std::memcpy(header, "8=FIX.4.2", 9);
    hPos = 9;
    header[hPos++] = SOH;
    header[hPos++] = '9';
    header[hPos++] = '=';

    // Body length via to_chars
    char lenBuf[12];
    auto [lenPtr, lenEc] = std::to_chars(lenBuf, lenBuf + sizeof(lenBuf),
                                          bodyLen);
    if (FIX_UNLIKELY(lenEc != std::errc{})) return 0;
    size_t lenDigits = static_cast<size_t>(lenPtr - lenBuf);
    std::memcpy(header + hPos, lenBuf, lenDigits);
    hPos += lenDigits;
    header[hPos++] = SOH;

    if (FIX_UNLIKELY(hPos > bodyStart)) return 0;

    size_t msgStart = bodyStart - hPos;
    std::memcpy(buf + msgStart, header, hPos);

    uint8_t cs = computeChecksum(buf + msgStart, bodyEnd - msgStart);

    if (FIX_UNLIKELY(bodyEnd + 7 > maxLen)) return 0;
    buf[bodyEnd]     = '1';
    buf[bodyEnd + 1] = '0';
    buf[bodyEnd + 2] = '=';
    buf[bodyEnd + 3] = '0' + static_cast<char>(cs / 100);
    buf[bodyEnd + 4] = '0' + static_cast<char>((cs / 10) % 10);
    buf[bodyEnd + 5] = '0' + static_cast<char>(cs % 10);
    buf[bodyEnd + 6] = SOH;

    size_t totalLen = bodyEnd + 7 - msgStart;
    if (msgStart > 0)
        std::memmove(buf, buf + msgStart, totalLen);

    ++seqNum_;
    return totalLen;
}

uint8_t FixEncoder::computeChecksum(const char* buf, size_t len) noexcept {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum += static_cast<uint8_t>(buf[i]);
    return static_cast<uint8_t>(sum % 256);
}

// Message encoders

size_t FixEncoder::encodeNewOrderSingle(
    char* out, size_t outLen,
    std::string_view clOrdID, std::string_view symbol,
    char side, char ordType, int64_t qty, double price, char tif) noexcept {

    if (FIX_UNLIKELY(outLen < HEADER_RESERVE + 64)) return 0;
    size_t pos = HEADER_RESERVE;

    pos = writeHeaderFields(out, pos, outLen, msgtype::NewOrderSingle);
    if (FIX_UNLIKELY(pos == 0)) return 0;

    pos = appendField(out, pos, outLen, tag::ClOrdID, clOrdID);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendCharField(out, pos, outLen, tag::HandlInst, '1');
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::OrderQty, qty);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendCharField(out, pos, outLen, tag::OrdType, ordType);
    if (FIX_UNLIKELY(pos == 0)) return 0;

    if (ordType == '2' || ordType == '4') {
        pos = appendField(out, pos, outLen, tag::Price, price, 2);
        if (FIX_UNLIKELY(pos == 0)) return 0;
    }

    pos = appendCharField(out, pos, outLen, tag::Side, side);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::Symbol, symbol);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendCharField(out, pos, outLen, tag::TimeInForce, tif);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::TransactTime,
                       std::string_view(cachedTimestamp_, cachedTsLen_));
    if (FIX_UNLIKELY(pos == 0)) return 0;

    return finalise(out, HEADER_RESERVE, pos, outLen);
}

size_t FixEncoder::encodeOrderCancelRequest(
    char* out, size_t outLen,
    std::string_view clOrdID, std::string_view origClOrdID,
    std::string_view orderID, std::string_view symbol,
    char side, int64_t qty) noexcept {

    if (FIX_UNLIKELY(outLen < HEADER_RESERVE + 64)) return 0;
    size_t pos = HEADER_RESERVE;

    pos = writeHeaderFields(out, pos, outLen, msgtype::OrderCancelRequest);
    if (FIX_UNLIKELY(pos == 0)) return 0;

    pos = appendField(out, pos, outLen, tag::ClOrdID, clOrdID);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::OrigClOrdID, origClOrdID);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::OrderID, orderID);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::OrderQty, qty);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendCharField(out, pos, outLen, tag::Side, side);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::Symbol, symbol);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::TransactTime,
                       std::string_view(cachedTimestamp_, cachedTsLen_));
    if (FIX_UNLIKELY(pos == 0)) return 0;

    return finalise(out, HEADER_RESERVE, pos, outLen);
}

size_t FixEncoder::encodeLogon(char* out, size_t outLen,
                                int heartbeatInterval) noexcept {
    if (FIX_UNLIKELY(outLen < HEADER_RESERVE + 32)) return 0;
    size_t pos = HEADER_RESERVE;

    pos = writeHeaderFields(out, pos, outLen, msgtype::Logon);
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::EncryptMethod, int64_t(0));
    if (FIX_UNLIKELY(pos == 0)) return 0;
    pos = appendField(out, pos, outLen, tag::HeartBtInt,
                       static_cast<int64_t>(heartbeatInterval));
    if (FIX_UNLIKELY(pos == 0)) return 0;

    return finalise(out, HEADER_RESERVE, pos, outLen);
}

size_t FixEncoder::encodeHeartbeat(char* out, size_t outLen,
                                    std::string_view testReqID) noexcept {
    if (FIX_UNLIKELY(outLen < HEADER_RESERVE + 32)) return 0;
    size_t pos = HEADER_RESERVE;

    pos = writeHeaderFields(out, pos, outLen, msgtype::Heartbeat);
    if (FIX_UNLIKELY(pos == 0)) return 0;

    if (!testReqID.empty()) {
        pos = appendField(out, pos, outLen, tag::TestReqID, testReqID);
        if (FIX_UNLIKELY(pos == 0)) return 0;
    }

    return finalise(out, HEADER_RESERVE, pos, outLen);
}

} // namespace fix

