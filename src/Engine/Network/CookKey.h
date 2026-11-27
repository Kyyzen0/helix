#pragma once
#include "Engine/Core/Types.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <string_view>

namespace helix::nt {

/**
 * @brief Builds a 64 bit key out of the values a cook reads.
 *
 * The cook cache (see nt::CookCache) files a node's output under a key that stands for everything
 * the cook depends on. Two cooks that read the same values in the same order get the same key,
 * and a change to any value, or to the order they were added in, changes it.
 *
 * Example
 *
 * ```
 * nt::KeyHasher hasher;
 * hasher.add(nodeTypeName);
 * hasher.add(frame);
 * const std::uint64_t key = hasher.value();
 * ```
 */
class KeyHasher
{
  public:
    /// @brief Mixes a whole number (or a bool) into the key.
    template <typename Integer>
        requires std::is_integral_v<Integer>
    void add(Integer value)
    {
        state_ = mix_(state_ ^ static_cast<std::uint64_t>(value)) + 0x9e3779b97f4a7c15ULL;
    }

    /// @brief Mixes in the bits of a float, so -0 and 0 stay apart and a NaN is stable.
    void add(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        add(bits);
    }

    /// @brief Mixes in the bits of a double.
    void add(double value)
    {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        add(bits);
    }

    /// @brief Mixes in a text, its length first so two runs of texts cannot run together.
    void add(std::string_view text)
    {
        add(static_cast<std::uint64_t>(text.size()));
        std::size_t offset = 0;
        for (; offset + sizeof(std::uint64_t) <= text.size(); offset += sizeof(std::uint64_t))
        {
            std::uint64_t word = 0;
            std::memcpy(&word, text.data() + offset, sizeof(word));
            add(word);
        }
        std::uint64_t tail = 0;
        if (offset < text.size()) std::memcpy(&tail, text.data() + offset, text.size() - offset);
        add(tail);
    }

    /// @brief Mixes in the string literal or std::string a caller passes.
    void add(const char* text) { add(std::string_view(text)); }

    /// @brief The key of everything added so far.
    std::uint64_t value() const { return mix_(state_); }

  private:
    // The finaliser of splitmix64, which spreads a change in one bit over the whole word.
    static std::uint64_t mix_(std::uint64_t word)
    {
        word = (word ^ (word >> 30)) * 0xbf58476d1ce4e5b9ULL;
        word = (word ^ (word >> 27)) * 0x94d049bb133111ebULL;
        return word ^ (word >> 31);
    }

    std::uint64_t state_ = 0x243f6a8885a308d3ULL;
};

} // namespace helix::nt
