#ifndef CIRCLEBUFF
#define CIRCLEBUFF

#include <cstdint>
#include <utility>
#include <cstddef>
#include <stdexcept>

struct Triple {
    int16_t x, y, z;
};

struct TripleF {
    float x, y, z;
};

struct Sample {
    TripleF accel;
    TripleF gyro;
    unsigned long timestamp; // in milliseconds
};

template<typename T, size_t N>
class RingBuffer {
public:
    T buffer[N];
    size_t head = 0;   // next write position
    size_t tail = 0;   // next read position
    size_t count = 0;  // number of valid items

    // Push a new sample
    void push(const T& value) {
        buffer[head] = value;
        head = (head + 1) % N;

        if (count < N) {
            count++;
        } else {
            // Buffer full, overwrite oldest
            tail = (tail + 1) % N;
        }
    }

    // Check how many unread items are available
    size_t available() const {
        return count;
    }

    // Pop the oldest unread sample
    T pop() {
        if (count == 0) {
            throw std::runtime_error("RingBuffer empty!");
        }
        T val = buffer[tail];
        tail = (tail + 1) % N;
        count--;
        return val;
    }

    // Optional: access Nth newest sample for pre-hit copy
    T getFromEnd(size_t i) const {
        if (i >= count) {
            throw std::out_of_range("RingBuffer getFromEnd out of range");
        }
        size_t idx = (head + N - 1 - i) % N;
        return buffer[idx];
    }
};

#endif