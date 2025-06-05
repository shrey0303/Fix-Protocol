#ifndef FIX_ENCODER_H
#define FIX_ENCODER_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "FixMessage.h"

namespace fix {

// Outbound FIX encoder. Zero allocation — all formatting in caller's buffer.
// Single-pass with backfill: body at HEADER_RESERVE offset, header computed
// after, memmove to buf[0]. See finalise() for tradeoff discussion.
class FixEncoder {
public:
    static constexpr size_t BUF_SIZE = 4096;
    static constexpr size_t HEADER_RESERVE = 32;

    FixEncoder(std::string_view senderCompID,
               std::string_view targetCompID) noexcept;

    [[nodiscard]] size_t encodeNewOrderSingle(
        char* out, size_t outLen,
        std::string_view clOrdID, std::string_view symbol,
        char side, char ordType, int64_t qty, double price, char tif) noexcept;

    [[nodiscard]] size_t encodeOrderCancelRequest(
        char* out, size_t outLen,
        std::string_view clOrdID, std::string_view origClOrdID,
        std::string_view orderID, std::string_view symbol,
        char side, int64_t qty) noexcept;

    [[nodiscard]] size_t encodeLogon(char* out, size_t outLen,
                                      int heartbeatInterval) noexcept;

    [[nodiscard]] size_t encodeHeartbeat(char* out, size_t outLen,
                                          std::string_view testReqID = {}) noexcept;

    void    resetSequence()                noexcept { seqNum_ = 1; }
    int64_t currentSeqNum()          const noexcept { return seqNum_; }

    // Refresh cached timestamp from system clock.
    // Call from a timer thread (~1ms interval), never on hot path.
    void refreshTimestamp() noexcept;

private:
    char    sender_[32]{};
    size_t  senderLen_{0};
    char    target_[32]{};
    size_t  targetLen_{0};
    int64_t seqNum_{1};

    char    cachedTimestamp_[24]{};
    size_t  cachedTsLen_{0};

    [[nodiscard]] size_t appendField(char* buf, size_t pos, size_t maxLen,
                                      int tag, std::string_view value) noexcept;
    [[nodiscard]] size_t appendField(char* buf, size_t pos, size_t maxLen,
                                      int tag, int64_t value) noexcept;
    // CAUTION: Default decimals=2 truncates to 2 decimal places.
    // FX pairs (e.g. EUR/USD) routinely need 4-5 decimals.
    // Callers MUST pass decimals explicitly for non-equity instruments.
    [[nodiscard]] size_t appendField(char* buf, size_t pos, size_t maxLen,
                                      int tag, double value, int decimals = 2) noexcept;
    [[nodiscard]] size_t appendCharField(char* buf, size_t pos, size_t maxLen,
                                          int tag, char value) noexcept;

    [[nodiscard]] size_t finalise(char* buf, size_t bodyStart,
                                   size_t bodyEnd, size_t maxLen) noexcept;
    [[nodiscard]] uint8_t computeChecksum(const char* buf, size_t len) noexcept;
    [[nodiscard]] size_t writeHeaderFields(char* buf, size_t pos, size_t maxLen,
                                            std::string_view msgType) noexcept;
    [[nodiscard]] size_t writeTimestamp(char* buf, size_t maxLen) noexcept;
};

} // namespace fix

#endif

