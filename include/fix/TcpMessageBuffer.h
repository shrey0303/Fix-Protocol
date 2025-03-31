#ifndef TCP_MESSAGE_BUFFER_H
#define TCP_MESSAGE_BUFFER_H

#include <cstddef>
#include <cstring>
#include <cstdint>
#include "FixMessage.h"

namespace fix {

// Linear buffer for TCP stream reassembly.
//
// TCP delivers a byte stream with no message boundaries. A single
// recv() may contain a partial message, exactly one message, or
// multiple back-to-back messages. This buffer accumulates bytes
// and uses body-length-based boundary detection (same as the parser)
// to extract complete FIX messages.
//
// Not a ring buffer — uses compact() to avoid wrap-around complexity.
// In practice, compact() runs once per batch of recv() calls when
// readPos_ drifts past CAPACITY/2. This is the same strategy used
// by Aeron and other high-performance transport layers.

// NOT thread-safe — single-writer, single-reader assumed.
// Caller must ensure no concurrent append() / nextMessage() / compact().
class TcpMessageBuffer {
public:
    static constexpr size_t CAPACITY = 65536;

    // Append incoming bytes from recv(). Returns bytes actually copied.
    [[nodiscard]] size_t append(const char* data, size_t len) noexcept {
        size_t space = CAPACITY - writePos_;
        size_t n = (len < space) ? len : space;
        std::memcpy(buf_ + writePos_, data, n);
        writePos_ += n;
        return n;
    }

    // Extract next complete FIX message from the buffer.
    // Returns pointer to message start in internal buffer and sets msgLen.
    // Returns nullptr if no complete message is available.
    // On success, advances readPos_ past the consumed message.
    //
    // Uses body-length-based boundary detection:
    //   1. Find "8=...SOH9=<len>SOH" at readPos_
    //   2. Compute total message length from body length + header + trailer
    //   3. Check if enough bytes are buffered
    //
    // The returned pointer is valid until the next compact() or append()
    // that triggers a compact. Caller must consume or copy before then.
    [[nodiscard]] const char* nextMessage(size_t& msgLen) noexcept {
        size_t avail = writePos_ - readPos_;
        if (FIX_UNLIKELY(avail < 20)) return nullptr;

        const char* p = buf_ + readPos_;

        // Tag 8 must start the message
        if (FIX_UNLIKELY(p[0] != '8' || p[1] != '='))
            return nullptr;

        // Find end of tag 8
        size_t i = 2;
        while (i < avail && p[i] != '\x01') ++i;
        if (i >= avail) return nullptr;
        ++i; // past SOH

        // Tag 9 must follow
        if (FIX_UNLIKELY(i + 2 >= avail || p[i] != '9' || p[i+1] != '='))
            return nullptr;
        i += 2;

        // Parse body length
        int64_t bodyLen = 0;
        while (i < avail && p[i] != '\x01') {
            char c = p[i];
            if (c < '0' || c > '9') return nullptr;
            bodyLen = bodyLen * 10 + (c - '0');
            ++i;
        }
        if (i >= avail) return nullptr;
        size_t bodyStart = i + 1;

        // Total = header bytes + body + "10=XXX\x01" (7 bytes)
        size_t totalNeeded = bodyStart + static_cast<size_t>(bodyLen) + 7;
        if (totalNeeded > avail) return nullptr;

        // Verify tag 10 is at expected position
        size_t csPos = bodyStart + static_cast<size_t>(bodyLen);
        if (FIX_UNLIKELY(p[csPos] != '1' || p[csPos+1] != '0' || p[csPos+2] != '='))
            return nullptr;

        // Find actual end (SOH after checksum value)
        size_t end = csPos + 3;
        while (end < avail && p[end] != '\x01') ++end;
        if (end >= avail) return nullptr;
        ++end; // include trailing SOH

        msgLen = end;
        const char* result = p;
        readPos_ += end;

        // Auto-compact when read position passes halfway
        if (readPos_ > CAPACITY / 2)
            compact();

        return result;
    }

    [[nodiscard]] size_t readable() const noexcept {
        return writePos_ - readPos_;
    }

    [[nodiscard]] size_t writable() const noexcept {
        return CAPACITY - writePos_;
    }

    // Move unread data to front of buffer. O(readable()) memcpy.
    // Called automatically when readPos_ drifts past half capacity.
    void compact() noexcept {
        size_t unread = writePos_ - readPos_;
        if (readPos_ > 0 && unread > 0)
            std::memmove(buf_, buf_ + readPos_, unread);
        readPos_ = 0;
        writePos_ = unread;
    }

private:
    char   buf_[CAPACITY]{};
    size_t readPos_{0};
    size_t writePos_{0};
};

} // namespace fix

#endif

