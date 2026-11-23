module;
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

export module BitLib;
/* Bit masks, and bit fields packed across a stream of 32-bit words. */

/**
 * @param width mask width, 0..63
 *
 * @code lowBitsMask(5) -> 0b11111 @endcode
 * @return the low width bits set.
 */
export constexpr uint64_t lowBitsMask(uint32_t width) {
    assert(width < 64);
    return (uint64_t{1} << width) - 1;
}

/// One word of a bit stream.
export using Word = uint32_t;
/// Bits in a Word.
export constexpr uint32_t WORD_BITS = 32;

/**
 * Two adjacent words of a bit stream as one 64-bit value.
 * The first word is in the low 32 bits, the next word in the high 32 bits.
 * Shifts in field() and setField() count from bit 0 of the first word.
 */
struct WordPair {
    uint64_t bits = 0;

    /**
     * @param low first word
     * @param high next word
     */
    static WordPair fromWords(Word low, Word high) {
        return {uint64_t{low} | uint64_t{high} << WORD_BITS};
    }
    /// @return the first word.
    [[nodiscard]] Word lowWord() const { return static_cast<Word>(bits); }
    /// @return the next word.
    [[nodiscard]] Word highWord() const { return static_cast<Word>(bits >> WORD_BITS); }

    /**
     * @param words bit stream
     * @param word index of the first word
     * @note A word past the end of the stream reads as 0.
     */
    static WordPair load(std::span<const Word> words, size_t word) {
        return fromWords(words[word], word + 1 < words.size() ? words[word + 1] : 0);
    }
    /**
     * @param words bit stream
     * @param word index of the first word
     * @note A word past the end of the stream is not written.
     */
    void store(std::span<Word> words, size_t word) const {
        words[word] = lowWord();
        if (word + 1 < words.size()) words[word + 1] = highWord();
    }

    /// @return width bits starting at bit shift.
    [[nodiscard]] Word field(uint32_t shift, uint32_t width) const {
        return static_cast<Word>(bits >> shift & lowBitsMask(width));
    }
    /// Overwrites width bits starting at bit shift with the low width bits of value.
    void setField(uint32_t shift, uint32_t width, Word value) {
        const uint64_t mask = lowBitsMask(width) << shift;
        bits = (bits & ~mask) | (uint64_t{value} << shift & mask);
    }
};

/// @return true when width bits starting at bitOffset lie within words.
bool fieldFits(size_t wordCount, size_t firstWord, size_t bitOffset, uint32_t width) {
    return width >= 1 && width <= WORD_BITS && (firstWord * WORD_BITS + bitOffset + width) <= wordCount * WORD_BITS;
}

/**
 * Bits are counted from bit 0 of a word upward, then on into the next word.
 * @param words word stream
 * @param firstWord word bitOffset counts from
 * @param bitOffset bits from bit 0 of firstWord
 * @param width bits to read, 1..32
 * @return the bits as an unsigned value.
 */
export Word readBits(std::span<const Word> words, size_t firstWord, size_t bitOffset, uint32_t width) {
    assert(fieldFits(words.size(), firstWord, bitOffset, width));
    const size_t word = firstWord + bitOffset / WORD_BITS;
    const uint32_t shift = bitOffset % WORD_BITS;
    return WordPair::load(words, word).field(shift, width);
}

/**
 * Overwrites width bits with the low width bits of value; other bits of the touched words are kept.
 * Bits are counted from bit 0 of a word upward, then on into the next word.
 * @param words word stream
 * @param firstWord word bitOffset counts from
 * @param bitOffset bits from bit 0 of firstWord
 * @param width bits to write, 1..32
 * @param value value whose low width bits are written
 */
export void writeBits(std::span<Word> words, size_t firstWord, size_t bitOffset, uint32_t width, Word value) {
    assert(fieldFits(words.size(), firstWord, bitOffset, width));
    const size_t word = firstWord + bitOffset / WORD_BITS;
    const uint32_t shift = bitOffset % WORD_BITS;
    WordPair pair = WordPair::load(words, word);
    pair.setField(shift, width, value);
    pair.store(words, word);
}
