// Studio Live Pro - DSP common utilities (header-only, realtime-safe helpers)
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#define SLP_HAS_SSE 1
#endif

namespace slp {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxBlock = 4096;

inline float dbToLin(float db) { return std::exp(db * 0.115129255f); }
inline double dbToLinD(double db) { return std::pow(10.0, db / 20.0); }
inline float linToDb(float l) { return 20.0f * std::log10(std::max(l, 1e-9f)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline bool finite(float v) { return std::isfinite(v); }

// Flush-to-zero / denormals-are-zero for the audio thread.
struct ScopedNoDenormals {
#ifdef SLP_HAS_SSE
    unsigned old;
    ScopedNoDenormals() : old(_mm_getcsr()) { _mm_setcsr(old | 0x8040u); }
    ~ScopedNoDenormals() { _mm_setcsr(old); }
#else
    ScopedNoDenormals() {}
#endif
};

// One-pole smoothing coefficient for a time constant in ms (0 => instant).
inline float timeCoef(float ms, double fs) {
    if (ms <= 0.0f) return 0.0f;
    return std::exp(-1.0f / (0.001f * ms * (float)fs));
}

struct Rng {
    uint32_t s = 0x12345678u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(int32_t)s * (1.0f / 2147483648.0f); }
};

// Fractional delay line (preallocated, power-of-two ring).
class DelayLine {
public:
    void prepare(int maxSamples) {
        int sz = 8;
        while (sz < maxSamples + 8) sz <<= 1;
        buf.assign((size_t)sz, 0.0f);
        mask = sz - 1; w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push(float x) { buf[(size_t)w] = x; w = (w + 1) & mask; }
    // d = 0 -> most recently pushed sample
    inline float readInt(int d) const { return buf[(size_t)((w - 1 - d) & mask)]; }
    inline float read(float d) const {
        int i = (int)d; float f = d - (float)i;
        float a = buf[(size_t)((w - 1 - i) & mask)];
        float b = buf[(size_t)((w - 2 - i) & mask)];
        return a + (b - a) * f;
    }
    int capacity() const { return mask + 1; }
private:
    std::vector<float> buf; int mask = 7, w = 0;
};

// Lock-free single-producer single-consumer queue (UI -> audio thread).
template <typename T, size_t N>
class SpscQueue {
public:
    bool push(const T& v) {
        size_t h = head.load(std::memory_order_relaxed), n = (h + 1) % N;
        if (n == tail.load(std::memory_order_acquire)) return false;
        data[h] = v; head.store(n, std::memory_order_release); return true;
    }
    bool pop(T& v) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        v = data[t]; tail.store((t + 1) % N, std::memory_order_release); return true;
    }
private:
    std::array<T, N> data{};
    std::atomic<size_t> head{0}, tail{0};
};

} // namespace slp
