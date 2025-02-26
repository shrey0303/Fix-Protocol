#ifndef MESSAGE_POOL_H
#define MESSAGE_POOL_H

#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <new>
#include "FixMessage.h"

namespace fix {

// Slab allocator for ParsedMessage objects.
// Single contiguous allocation, intrusive free-list via separate pointer array.
// acquire/release are O(1) pointer swaps — no malloc, no lock on hot path.
// Designed for single-writer (LMAX-style) — no atomic ops needed.

// NOT thread-safe — single-writer only.
// Designed for LMAX-style architectures where one thread owns the pool.
// If you need concurrent access, add external synchronisation.
class MessagePool {
public:
    static constexpr size_t CAPACITY = 1024;

    MessagePool() {
        const size_t bytes = CAPACITY * sizeof(ParsedMessage);
        const size_t aligned = (bytes + 63) & ~size_t(63);

#if defined(_MSC_VER) || defined(_WIN32)
        slab_ = static_cast<ParsedMessage*>(_aligned_malloc(aligned, 64));
#else
        slab_ = static_cast<ParsedMessage*>(std::aligned_alloc(64, aligned));
#endif
        if (!slab_) throw std::bad_alloc();

        for (size_t i = 0; i < CAPACITY; ++i)
            new (&slab_[i]) ParsedMessage();

        next_ = new ParsedMessage*[CAPACITY];
        head_ = &slab_[0];
        for (size_t i = 0; i < CAPACITY - 1; ++i)
            next_[idx(&slab_[i])] = &slab_[i + 1];
        next_[idx(&slab_[CAPACITY - 1])] = nullptr;
        avail_ = CAPACITY;
    }

    ~MessagePool() {
        delete[] next_;
        if (slab_) {
#if defined(_MSC_VER) || defined(_WIN32)
            _aligned_free(slab_);
#else
            std::free(slab_);
#endif
        }
    }

    MessagePool(const MessagePool&) = delete;
    MessagePool& operator=(const MessagePool&) = delete;
    MessagePool(MessagePool&&) = delete;
    MessagePool& operator=(MessagePool&&) = delete;

    [[nodiscard]] ParsedMessage* acquire() noexcept {
        if (FIX_UNLIKELY(!head_)) return nullptr;
        ParsedMessage* m = head_;
        head_ = next_[idx(m)];
        m->reset();
        --avail_;
        return m;
    }

    void release(ParsedMessage* m) noexcept {
        if (FIX_UNLIKELY(!m)) return;
        assert(m >= slab_ && m < slab_ + CAPACITY &&
               "release(): pointer does not belong to this pool");
        next_[idx(m)] = head_;
        head_ = m;
        ++avail_;
    }

    [[nodiscard]] size_t available() const noexcept { return avail_; }

private:
    ParsedMessage*  slab_{nullptr};
    ParsedMessage** next_{nullptr};
    ParsedMessage*  head_{nullptr};
    size_t          avail_{0};

    size_t idx(ParsedMessage* m) const noexcept {
        return static_cast<size_t>(m - slab_);
    }
};

} // namespace fix

#endif


