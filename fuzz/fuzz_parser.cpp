#include "fix/FixParser.h"
#include "fix/MessagePool.h"
#include <cstdint>
#include <cstddef>

// LibFuzzer entry point.
// Feeds arbitrary bytes into the FIX parser to verify it never
// crashes, hangs, or triggers undefined behavior.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static fix::MessagePool pool;
    fix::FixParser parser(pool);

    fix::ParsedMessage* out = nullptr;
    auto result = parser.parse(
        reinterpret_cast<const char*>(data), size, out);

    if (out) {
        // Exercise accessors to catch UB
        (void)out->getField(55);
        (void)out->getInt(38);
        (void)out->getDouble(44);
        (void)out->getChar(54);
        (void)out->hasField(35);
        pool.release(out);
    }

    parser.resetSequence();
    return 0;
}
