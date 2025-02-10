#include "fix/FixValidator.h"
#include "fix/FixTags.h"

namespace fix {

static constexpr int HEADER_TAGS[] = {
    tag::SenderCompID, tag::TargetCompID,
    tag::MsgSeqNum,    tag::SendingTime
};
static constexpr size_t HEADER_COUNT = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

static constexpr int NOS_TAGS[] = {
    tag::ClOrdID, tag::HandlInst, tag::OrderQty,
    tag::OrdType, tag::Side, tag::Symbol, tag::TransactTime
};
static constexpr size_t NOS_COUNT = sizeof(NOS_TAGS) / sizeof(NOS_TAGS[0]);

static constexpr int OCR_TAGS[] = {
    tag::ClOrdID, tag::OrderID, tag::OrderQty,
    tag::OrigClOrdID, tag::Side, tag::Symbol, tag::TransactTime
};
static constexpr size_t OCR_COUNT = sizeof(OCR_TAGS) / sizeof(OCR_TAGS[0]);

static constexpr int OCRR_TAGS[] = {
    tag::ClOrdID, tag::OrderID, tag::OrderQty,
    tag::OrdType, tag::OrigClOrdID, tag::Price,
    tag::Side, tag::Symbol, tag::TimeInForce, tag::TransactTime
};
static constexpr size_t OCRR_COUNT = sizeof(OCRR_TAGS) / sizeof(OCRR_TAGS[0]);

static constexpr int EXEC_TAGS[] = {
    tag::AvgPx, tag::ClOrdID, tag::CumQty, tag::ExecID,
    tag::LastPx, tag::LastQty, tag::OrderID, tag::OrderQty,
    tag::OrdStatus, tag::OrdType, tag::Side, tag::Symbol,
    tag::ExecType, tag::LeavesQty
};
static constexpr size_t EXEC_COUNT = sizeof(EXEC_TAGS) / sizeof(EXEC_TAGS[0]);

static constexpr int LOGON_TAGS[] = { tag::EncryptMethod, tag::HeartBtInt };
static constexpr size_t LOGON_COUNT = sizeof(LOGON_TAGS) / sizeof(LOGON_TAGS[0]);

static constexpr int REJECT_TAGS[] = { tag::RefSeqNum };
static constexpr size_t REJECT_COUNT = sizeof(REJECT_TAGS) / sizeof(REJECT_TAGS[0]);

// MarketDataSnapshotFullRefresh (35=W) — requires Symbol
static constexpr int MDS_TAGS[] = { tag::Symbol };
static constexpr size_t MDS_COUNT = sizeof(MDS_TAGS) / sizeof(MDS_TAGS[0]);

ParseResult FixValidator::checkRequired(const ParsedMessage& msg,
                                         const int* tags,
                                         size_t count) noexcept {
    // Single pass over fields — O(N+M) instead of O(N×M).
    // count is always <= 14 (EXEC_TAGS), so seen[] fits in a register.
    bool seen[16]{};
    for (int i = 0; i < msg.fieldCount; ++i) {
        int t = msg.fields[i].tag;
        for (size_t j = 0; j < count; ++j)
            if (tags[j] == t) { seen[j] = true; break; }
    }
    for (size_t j = 0; j < count; ++j)
        if (!seen[j]) return ParseResult::REQUIRED_FIELD_MISSING;
    return ParseResult::OK;
}

ParseResult FixValidator::validate(const ParsedMessage& msg) noexcept {
    ParseResult r = checkRequired(msg, HEADER_TAGS, HEADER_COUNT);
    if (FIX_UNLIKELY(r != ParseResult::OK)) return r;

    switch (msg.msgTypeChar) {
    case 'D': return checkRequired(msg, NOS_TAGS,    NOS_COUNT);
    case 'F': return checkRequired(msg, OCR_TAGS,    OCR_COUNT);
    case 'G': return checkRequired(msg, OCRR_TAGS,   OCRR_COUNT);
    case '8': return checkRequired(msg, EXEC_TAGS,   EXEC_COUNT);
    case 'A': return checkRequired(msg, LOGON_TAGS,  LOGON_COUNT);
    case '3': return checkRequired(msg, REJECT_TAGS, REJECT_COUNT);
    case 'W': return checkRequired(msg, MDS_TAGS,    MDS_COUNT);
    default:  return ParseResult::OK;
    }
}

} // namespace fix

