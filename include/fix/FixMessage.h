#ifndef FIX_MESSAGE_H
#define FIX_MESSAGE_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <cstdlib>
#include "FixTags.h"

namespace fix {

// Branch hints — guides CPU branch predictor on error paths
#if defined(__GNUC__) || defined(__clang__)
#define FIX_LIKELY(x)      __builtin_expect(!!(x), 1)
#define FIX_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#define FIX_ALWAYS_INLINE  __attribute__((always_inline)) inline
#else
#define FIX_LIKELY(x)      (x)
#define FIX_UNLIKELY(x)    (x)
#define FIX_ALWAYS_INLINE  inline
#endif

enum class ParseResult : uint8_t {
    OK,
    CHECKSUM_ERROR,
    INVALID_FORMAT,
    MISSING_HEADER,
    REQUIRED_FIELD_MISSING,
    SEQ_GAP,
    DUPLICATE_SEQ,
    UNKNOWN_MSG_TYPE,
    BUFFER_TOO_SMALL,
    BODY_LENGTH_MISMATCH,
    POOL_EXHAUSTED,
    INVALID_REPEATING_GROUP,
    BUFFER_OVERFLOW
};

struct FieldView {
    int              tag{0};
    std::string_view value{};
};

// Flat storage for one repeating group instance (e.g. NoMDEntries).
// Pre-allocated — no heap. 32 entries × 16 fields covers all standard
// FIX repeating groups without dynamic sizing.
struct RepeatingGroup {
    static constexpr int MAX_ENTRIES = 32;
    static constexpr int MAX_ENTRY_FIELDS = 16;

    int       countTag{0};      // e.g. 268 (NoMDEntries)
    int       delimTag{0};      // e.g. 269 (MDEntryType) — first tag of each entry
    int       count{0};         // declared count from the count tag value
    int       entryCounts[MAX_ENTRIES]{};
    FieldView entries[MAX_ENTRIES][MAX_ENTRY_FIELDS]{};
};

// LIFETIME CONTRACT: All string_view fields (msgType, senderCompID,
// targetCompID, sendingTime, and every fields[].value) are non-owning
// views into the caller's raw message buffer. The caller MUST keep that
// buffer alive and unmodified for the entire lifetime of this
// ParsedMessage. If the buffer is freed, overwritten, or compacted
// while this object is live, every view silently dangles.
struct alignas(64) ParsedMessage {
    static constexpr int MAX_FIELDS  = 64;
    static constexpr int MAX_GROUPS  = 4;

    // Header shortcuts — populated during parse for O(1) access
    std::string_view rawBeginString{};
    std::string_view msgType{};
    char             msgTypeChar{0};
    std::string_view senderCompID{};
    std::string_view targetCompID{};
    int64_t          seqNum{0};
    std::string_view sendingTime{};

    // All parsed fields in wire order (excludes repeating group internals)
    FieldView        fields[MAX_FIELDS];
    int              fieldCount{0};

    // Repeating groups — parsed separately from flat fields
    RepeatingGroup   groups[MAX_GROUPS];
    int              groupCount{0};

    // Linear scan — N ≤ 64 fields fits in ~4 cache lines.
    // Faster than hash map for this working set due to sequential
    // access and hardware prefetching.
    [[nodiscard]] inline std::string_view getField(int tagNum) const noexcept {
        for (int i = 0; i < fieldCount; ++i) {
            if (fields[i].tag == tagNum)
                return fields[i].value;
        }
        return {};
    }

    [[nodiscard]] inline int64_t getInt(int tagNum) const noexcept {
        auto sv = getField(tagNum);
        if (sv.empty()) return 0;
        int64_t result = 0;
        bool neg = false;
        size_t i = 0;
        if (sv[0] == '-') { neg = true; ++i; }
        for (; i < sv.size(); ++i) {
            char c = sv[i];
            if (c < '0' || c > '9') break;
            result = result * 10 + (c - '0');
        }
        return neg ? -result : result;
    }

    // Floating-point parse — no locale, no allocation
    [[nodiscard]] inline double getDouble(int tagNum) const noexcept {
        auto sv = getField(tagNum);
        if (sv.empty()) return 0.0;
        double result = 0.0;
        bool neg = false;
        size_t i = 0;
        if (sv[0] == '-') { neg = true; ++i; }
        for (; i < sv.size() && sv[i] != '.'; ++i) {
            char c = sv[i];
            if (c < '0' || c > '9') break;
            result = result * 10.0 + (c - '0');
        }
        if (i < sv.size() && sv[i] == '.') {
            ++i;
            double divisor = 10.0;
            for (; i < sv.size(); ++i) {
                char c = sv[i];
                if (c < '0' || c > '9') break;
                result += (c - '0') / divisor;
                divisor *= 10.0;
            }
        }
        return neg ? -result : result;
    }

    [[nodiscard]] inline char getChar(int tagNum) const noexcept {
        auto sv = getField(tagNum);
        return sv.empty() ? '\0' : sv[0];
    }

    [[nodiscard]] inline bool hasField(int tagNum) const noexcept {
        for (int i = 0; i < fieldCount; ++i) {
            if (fields[i].tag == tagNum) return true;
        }
        return false;
    }

    // Find a repeating group by its count tag (e.g. 268)
    [[nodiscard]] inline const RepeatingGroup* getGroup(int countTag) const noexcept {
        for (int i = 0; i < groupCount; ++i) {
            if (groups[i].countTag == countTag)
                return &groups[i];
        }
        return nullptr;
    }

    // Only clear fields that parseFields() doesn't unconditionally overwrite.
    // rawBeginString, msgType, senderCompID, targetCompID, sendingTime are
    // all set from parsed fields before any read on the success path.
    // On error paths the message is released and the caller never sees it.
    void reset() noexcept {
        fieldCount  = 0;
        groupCount  = 0;
        seqNum      = 0;
        msgTypeChar = 0;
    }
};

static_assert(alignof(ParsedMessage) == 64,
    "ParsedMessage must be cache-line aligned");

} // namespace fix

#endif


