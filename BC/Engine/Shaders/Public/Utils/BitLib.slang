/** Bit masks, and bit fields packed across a stream of 32-bit words; Slang twin of the BitLib C++ module. */
module BitLib;

/// One word of a bit stream.
public typealias Word = uint;
/// Bits in a Word.
public static const uint WORD_BITS = 32;

/**
 * @param width mask width, 0..63
 * @return the low width bits set.
 */
public uint64_t lowBitsMask(uint width) {
    return (uint64_t(1) << width) - 1;
}

/**
 * Two adjacent words of a bit stream as one 64-bit value.
 * The first word is in the low 32 bits, the next word in the high 32 bits.
 * Shifts in field() count from bit 0 of the first word.
 */
struct WordPair {
    uint64_t bits;

    /**
     * @param low first word
     * @param high next word
     */
    static WordPair fromWords(Word low, Word high) {
        WordPair pair;
        pair.bits = uint64_t(low) | (uint64_t(high) << WORD_BITS);
        return pair;
    }

    /**
     * @param words bit stream
     * @param word index of the first word
     * @note The word after the first word must exist.
     */
    static WordPair load(Ptr<Word> words, uint word) {
        return fromWords(words[word], words[word + 1]);
    }

    /// @return width bits starting at bit shift.
    Word field(uint shift, uint width) {
        return Word((bits >> shift) & lowBitsMask(width));
    }
};

/**
 * Bits are counted from bit 0 of a word upward, then on into the next word.
 * @param words word stream
 * @param firstWord word bitOffset counts from
 * @param bitOffset bits from bit 0 of firstWord
 * @param width bits to read, 1..32
 * @return the bits as an unsigned value.
 * @note The word after the one holding the first bit must exist.
 */
public Word readBits(Ptr<Word> words, uint firstWord, uint bitOffset, uint width) {
    return WordPair::load(words, firstWord + bitOffset / WORD_BITS).field(bitOffset % WORD_BITS, width);
}
