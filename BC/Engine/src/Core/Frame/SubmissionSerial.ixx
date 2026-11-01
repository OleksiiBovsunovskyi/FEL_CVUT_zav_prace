module;

#include <cstdint>

export module SubmissionSerial;

/**
 * Counter of frames submited so far, used to ensure resource validity
 */
export using SubmissionSerial = uint64_t;
