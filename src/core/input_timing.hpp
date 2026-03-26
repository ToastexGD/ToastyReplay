#ifndef _input_timing_hpp
#define _input_timing_hpp

#include <cstdint>

namespace InputTiming {
    uint64_t nowMicros();
    void queueTimestamp(uint64_t micros);
    void queueCurrentTimestamp();
}

#endif
