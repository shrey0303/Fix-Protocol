#ifndef FIX_PARSER_H
#define FIX_PARSER_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "FixMessage.h"
#include "MessagePool.h"

namespace fix {

class FixParser {
public:
    explicit FixParser(MessagePool& pool) noexcept;

    [[nodiscard]] ParseResult parse(const char* buf, size_t len,
                                     ParsedMessage*& out) noexcept;

    [[nodiscard]] size_t  bytesConsumed()    const noexcept;
    [[nodiscard]] int64_t nextExpectedSeq()  const noexcept;
    void resetSequence() noexcept;

private:
    MessagePool& pool_;
    int64_t      nextSeq_{1};
    size_t       consumed_{0};

    // Each group type declares its count tag, delimiter tag, and the
    // complete set of tags that can appear inside an entry. Group
    // termination checks this list — no hardcoded tag checks elsewhere.
    static constexpr int MAX_GROUP_FIELD_TAGS = 8;
    static constexpr struct GroupSpec {
        int countTag;
        int delimTag;
        int numFieldTags;
        int fieldTags[MAX_GROUP_FIELD_TAGS]; // all valid tags inside one entry
    } kGroupSpecs[] = {
        { tag::NoMDEntries, tag::MDEntryType, 4,
          { tag::MDEntryType, tag::MDEntryPx, tag::MDEntrySize, tag::MDUpdateAction } },
    };
    static constexpr int kNumGroupSpecs =
        sizeof(kGroupSpecs) / sizeof(kGroupSpecs[0]);

    [[nodiscard]] ParseResult parseFields(const char* buf, size_t len,
                                           ParsedMessage& msg) noexcept;

    // Returns the GroupSpec for a count tag, or nullptr if not a group tag
    [[nodiscard]] static const GroupSpec* findGroupSpec(int countTag) noexcept;

    [[nodiscard]] int parseTag(const char* buf, size_t pos,
                                size_t len, size_t& tagEnd) noexcept;

    [[nodiscard]] std::string_view parseValue(const char* buf, size_t pos,
                                               size_t len, size_t& valEnd) noexcept;
};

} // namespace fix

#endif

