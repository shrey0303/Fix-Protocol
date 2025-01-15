#include "fix/FixParser.h"
#include "fix/FixTags.h"
#include "fix/FixValidator.h"
#include <cstring>

namespace fix {

static FIX_ALWAYS_INLINE int64_t parseSVInt(std::string_view sv) noexcept {
    int64_t r = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') break;
        r = r * 10 + (c - '0');
    }
    return r;
}

FixParser::FixParser(MessagePool& pool) noexcept
    : pool_(pool) {}

size_t  FixParser::bytesConsumed()   const noexcept { return consumed_; }
int64_t FixParser::nextExpectedSeq() const noexcept { return nextSeq_; }
void    FixParser::resetSequence()         noexcept { nextSeq_ = 1; }

const FixParser::GroupSpec* FixParser::findGroupSpec(int countTag) noexcept {
    for (int i = 0; i < kNumGroupSpecs; ++i) {
        if (kGroupSpecs[i].countTag == countTag)
            return &kGroupSpecs[i];
    }
    return nullptr;
}

static bool isKnownGroupTag(const int* fieldTags, int numTags, int tag) noexcept {
    for (int i = 0; i < numTags; ++i) {
        if (fieldTags[i] == tag) return true;
    }
    return false;
}

// Body-length-based boundary detection: parse tags 8/9 structurally,
// compute where tag 10 must be from the declared body length.
// No scanning for SOH+"10=" — immune to adversarial field values.

ParseResult FixParser::parse(const char* buf, size_t len,
                              ParsedMessage*& out) noexcept {
    consumed_ = 0;

    if (FIX_UNLIKELY(!buf || len < 20))
        return ParseResult::BUFFER_TOO_SMALL;

    // Tag 8 must be first
    if (FIX_UNLIKELY(buf[0] != '8' || buf[1] != '='))
        return ParseResult::MISSING_HEADER;

    size_t pos = 2;
    while (pos < len && buf[pos] != SOH) ++pos;
    if (FIX_UNLIKELY(pos >= len)) return ParseResult::BUFFER_TOO_SMALL;
    size_t afterTag8 = pos + 1;

    // Tag 9 must be second
    if (FIX_UNLIKELY(afterTag8 + 2 >= len))
        return ParseResult::BUFFER_TOO_SMALL;
    if (FIX_UNLIKELY(buf[afterTag8] != '9' || buf[afterTag8 + 1] != '='))
        return ParseResult::MISSING_HEADER;

    pos = afterTag8 + 2;
    int64_t bodyLen = 0;
    while (pos < len && buf[pos] != SOH) {
        char c = buf[pos];
        if (FIX_UNLIKELY(c < '0' || c > '9'))
            return ParseResult::INVALID_FORMAT;
        bodyLen = bodyLen * 10 + (c - '0');
        ++pos;
    }
    if (FIX_UNLIKELY(pos >= len)) return ParseResult::BUFFER_TOO_SMALL;
    size_t bodyStart = pos + 1;

    // Tag 10 position is deterministic from body length
    size_t csStart = bodyStart + static_cast<size_t>(bodyLen);
    if (FIX_UNLIKELY(csStart + 7 > len))
        return ParseResult::BUFFER_TOO_SMALL;

    if (FIX_UNLIKELY(buf[csStart] != '1' || buf[csStart+1] != '0' || buf[csStart+2] != '='))
        return ParseResult::BODY_LENGTH_MISMATCH;

    pos = csStart + 3;
    while (pos < len && buf[pos] != SOH) ++pos;
    if (FIX_UNLIKELY(pos >= len)) return ParseResult::BUFFER_TOO_SMALL;
    size_t msgEnd = pos + 1;

    // Checksum: sum all bytes before tag 10
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < csStart; ++i)
            sum += static_cast<uint8_t>(buf[i]);
        uint8_t computed = static_cast<uint8_t>(sum % 256);

        int declared = 0;
        for (size_t i = csStart + 3; i < msgEnd && buf[i] != SOH; ++i)
            declared = declared * 10 + (buf[i] - '0');

        if (FIX_UNLIKELY(static_cast<uint8_t>(declared) != computed))
            return ParseResult::CHECKSUM_ERROR;
    }

    ParsedMessage* msg = pool_.acquire();
    if (FIX_UNLIKELY(!msg))
        return ParseResult::POOL_EXHAUSTED;

    ParseResult r = parseFields(buf, csStart, *msg);
    if (FIX_UNLIKELY(r != ParseResult::OK)) {
        pool_.release(msg);
        return r;
    }

    // Structural: 8, 9, 35 must be first three fields
    if (FIX_UNLIKELY(msg->fieldCount < 3 ||
                     msg->fields[0].tag != tag::BeginString ||
                     msg->fields[1].tag != tag::BodyLength ||
                     msg->fields[2].tag != tag::MsgType)) {
        pool_.release(msg);
        return ParseResult::MISSING_HEADER;
    }

    msg->rawBeginString = msg->fields[0].value;
    msg->msgType        = msg->fields[2].value;
    msg->msgTypeChar    = msg->fields[2].value.empty() ? '\0' : msg->fields[2].value[0];

    for (int i = 3; i < msg->fieldCount; ++i) {
        switch (msg->fields[i].tag) {
        case tag::SenderCompID: msg->senderCompID = msg->fields[i].value; break;
        case tag::TargetCompID: msg->targetCompID = msg->fields[i].value; break;
        case tag::MsgSeqNum:    msg->seqNum = parseSVInt(msg->fields[i].value); break;
        case tag::SendingTime:  msg->sendingTime = msg->fields[i].value; break;
        default: break;
        }
    }

    {
        switch (msg->msgTypeChar) {
        case '0': case '1': case '2': case '3': case '4': case '5':
        case '8': case 'A': case 'D': case 'F': case 'G': case 'W':
            break;
        default:
            pool_.release(msg);
            return ParseResult::UNKNOWN_MSG_TYPE;
        }
    }

    // Sequence tracking
    if (msg->msgTypeChar == '4') {
        int64_t ns = parseSVInt(msg->getField(tag::NewSeqNo));
        if (ns > 0) nextSeq_ = ns;
    } else if (msg->seqNum > 0) {
        if (FIX_UNLIKELY(msg->seqNum < nextSeq_)) {
            pool_.release(msg);
            return ParseResult::DUPLICATE_SEQ;
        }
        if (FIX_UNLIKELY(msg->seqNum > nextSeq_)) {
            pool_.release(msg);
            return ParseResult::SEQ_GAP;
        }
        nextSeq_ = msg->seqNum + 1;
    }

    r = FixValidator::validate(*msg);
    if (FIX_UNLIKELY(r != ParseResult::OK)) {
        pool_.release(msg);
        return r;
    }

    consumed_ = msgEnd;
    out = msg;
    return ParseResult::OK;
}

// Field parsing with repeating group detection.
// When a known count tag is encountered (e.g. 268=3), the subsequent
// fields are consumed as group entries until a non-group tag appears.

ParseResult FixParser::parseFields(const char* buf, size_t len,
                                    ParsedMessage& msg) noexcept {
    size_t pos = 0;
    msg.fieldCount = 0;
    msg.groupCount = 0;

    while (pos < len) {
        if (buf[pos] == SOH) { ++pos; continue; }

        size_t tagEnd = 0;
        int tagNum = parseTag(buf, pos, len, tagEnd);
        if (FIX_UNLIKELY(tagNum < 0))
            return ParseResult::INVALID_FORMAT;

        size_t valEnd = 0;
        std::string_view value = parseValue(buf, tagEnd, len, valEnd);

        // Check if this is a repeating group count tag
        const GroupSpec* spec = findGroupSpec(tagNum);
        if (spec && msg.groupCount < ParsedMessage::MAX_GROUPS) {
            int delimTag = spec->delimTag;

            int groupCount = 0;
            for (size_t k = 0; k < value.size(); ++k) {
                char c = value[k];
                if (c < '0' || c > '9') break;
                groupCount = groupCount * 10 + (c - '0');
            }

            auto& grp = msg.groups[msg.groupCount];
            grp.countTag = tagNum;
            grp.delimTag = delimTag;
            grp.count = groupCount;

            // Store the count tag itself as a flat field too
            if (msg.fieldCount < ParsedMessage::MAX_FIELDS) {
                msg.fields[msg.fieldCount] = {tagNum, value};
                ++msg.fieldCount;
            }

            // Parse group entries
            int entry = -1;
            pos = valEnd;

            while (pos < len && entry < groupCount) {
                size_t loopStartPos = pos;
                if (buf[pos] == SOH) { ++pos; continue; }

                size_t gTagEnd = 0;
                int gTag = parseTag(buf, pos, len, gTagEnd);
                if (FIX_UNLIKELY(gTag < 0))
                    return ParseResult::INVALID_FORMAT;


                if (gTag == delimTag) {
                    // Start of a new group entry
                    ++entry;
                    if (FIX_UNLIKELY(entry >= RepeatingGroup::MAX_ENTRIES))
                        return ParseResult::INVALID_REPEATING_GROUP;
                    grp.entryCounts[entry] = 0;
                } else if (!isKnownGroupTag(spec->fieldTags, spec->numFieldTags, gTag)) {
                    // Not a known group tag. Group has ended.
                    // Restore position so the flat message parser can consume this tag.
                    pos = loopStartPos;
                    break;
                }

                if (FIX_UNLIKELY(entry < 0)) {
                    // FIX spec: The first field of a repeating group MUST be the delimiter.
                    return ParseResult::INVALID_REPEATING_GROUP;
                }

                size_t gValEnd = 0;
                std::string_view gVal = parseValue(buf, gTagEnd, len, gValEnd);

                // Append field to current group entry
                int fc = grp.entryCounts[entry];
                if (fc < RepeatingGroup::MAX_ENTRY_FIELDS) {
                    grp.entries[entry][fc] = {gTag, gVal};
                    grp.entryCounts[entry] = fc + 1;
                }

                pos = gValEnd;
            }



            if (FIX_UNLIKELY(entry + 1 != groupCount))
                return ParseResult::INVALID_REPEATING_GROUP;

            ++msg.groupCount;
            continue;
        }

        // Normal flat field
        if (FIX_UNLIKELY(msg.fieldCount >= ParsedMessage::MAX_FIELDS))
            return ParseResult::BUFFER_OVERFLOW;
        msg.fields[msg.fieldCount] = {tagNum, value};
        ++msg.fieldCount;
        pos = valEnd;
    }

    return ParseResult::OK;
}

int FixParser::parseTag(const char* buf, size_t pos,
                         size_t len, size_t& tagEnd) noexcept {
    int tag = 0;
    size_t i = pos;
    while (i < len && buf[i] != '=') {
        char c = buf[i];
        if (FIX_UNLIKELY(c < '0' || c > '9')) return -1;
        tag = tag * 10 + (c - '0');
        ++i;
    }
    if (FIX_UNLIKELY(i >= len || buf[i] != '=')) return -1;
    tagEnd = i + 1;
    return tag;
}

std::string_view FixParser::parseValue(const char* buf, size_t pos,
                                        size_t len, size_t& valEnd) noexcept {
    size_t start = pos;
    size_t i = pos;
    while (i < len && buf[i] != SOH) ++i;
    valEnd = (i < len) ? i + 1 : i;
    return {buf + start, i - start};
}

} // namespace fix


