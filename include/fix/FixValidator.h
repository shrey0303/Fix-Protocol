#ifndef FIX_VALIDATOR_H
#define FIX_VALIDATOR_H

#include "FixMessage.h"

namespace fix {

// Per-message-type required field validation.
// constexpr tag arrays — resolved at compile time, zero runtime cost.
class FixValidator {
public:
    [[nodiscard]] static ParseResult validate(const ParsedMessage& msg) noexcept;

private:
    [[nodiscard]] static ParseResult checkRequired(const ParsedMessage& msg,
                                                    const int* tags,
                                                    size_t count) noexcept;
};

} // namespace fix

#endif
