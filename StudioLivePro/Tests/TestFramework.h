#pragma once
#include <cstdio>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <chrono>
#include "../Source/DSP/Common.h"

struct TestCase { std::string name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, f}); } };
inline int& failCount() { static int f = 0; return f; }
inline int& checkCount() { static int f = 0; return f; }
#define TEST(name) static void name(); static Registrar reg_##name(#name, name); static void name()
#define CHECK(cond) do { ++checkCount(); if (!(cond)) { ++failCount(); std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++checkCount(); double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) { ++failCount(); std::printf("    FAIL %s:%d  %s=%.4f expected %.4f +/- %.4f\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); } } while (0)

namespace tu {
using std::vector;
constexpr double FS = 48000.0;
inline vector<float> sine(double f, double db, int n, double fs = FS, double phase = 0) { vector<float> v((size_t)n); double a = std::pow(10.0, db / 20.0); for (int i = 0; i < n; ++i) v[(size_t)i] = (float)(a * std::sin(2 * slp::kPi * f * i / fs + phase)); return v; }
inline vector<float> white(double db, int n, uint32_t seed = 1) { slp::Rng r; r.s = seed * 2654435761u + 1; vector<float> v((size_t)n); double a = std::pow(10.0, db / 20.0) * 1.7320508; for (auto& x : v) x = (float)(a * r.next()); return v; }
inline vector<float> pink(double db, int n, uint32_t seed = 7) {
    slp::Rng r; r.s = seed * 2654435761u + 1; vector<float> v((size_t)n); double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& x : v) { double w = r.next(); b0 = .99886 * b0 + w * .0555179; b1 = .99332 * b1 + w * .0750759; b2 = .969 * b2 + w * .153852; b3 = .8665 * b3 + w * .3104856; b4 = .55 * b4 + w * .5329522; b5 = -.7616 * b5 - w * .016898; x = (float)((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * .5362) * 0.11); b6 = w * .115926; }
    double rms = 0; for (float x : v) rms += x * x; rms = std::sqrt(rms / n); double g = std::pow(10.0, db / 20.0) / (rms * 1.0); for (auto& x : v) x = (float)(x * g * 1.0); return v; } // db = RMS dBFS
inline double rmsDb(const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double)x[i] * x[i]; return 10 * std::log10(s / std::max(1, n) + 1e-20); }
inline double peakDb(const float* x, int n) { double p = 0; for (int i = 0; i < n; ++i) p = std::max(p, (double)std::fabs(x[i])); return 20 * std::log10(p + 1e-20); }
inline bool allFinite(const float* x, int n) { for (int i = 0; i < n; ++i) if (!std::isfinite(x[i])) return false; return true; }
inline double dcOffset(const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += x[i]; return s / std::max(1, n); }
// Goertzel magnitude (linear amplitude of sinusoid at f)
inline double tone(const float* x, int n, double f, double fs = FS) { double re = 0, im = 0; for (int i = 0; i < n; ++i) { double w = 2 * slp::kPi * f * i / fs; double win = 0.5 - 0.5 * std::cos(2 * slp::kPi * i / n); re += x[i] * win * std::cos(w); im += x[i] * win * std::sin(w); } return 2.0 * std::sqrt(re * re + im * im) / (n * 0.5); }
// Independent reference true-peak (8x sinc interpolation, 64-tap per phase)
inline double truePeakRef(const float* x, int n) {
    double m = 0; for (int i = 32; i < n - 32; ++i) for (int p = 0; p < 8; ++p) { double a = 0; for (int j = -31; j <= 32; ++j) { double t = p / 8.0 - j; double s = std::fabs(t) < 1e-9 ? 1.0 : std::sin(slp::kPi * t) / (slp::kPi * t); double w = 0.5 + 0.5 * std::cos(slp::kPi * t / 33.0); a += x[i + j] * s * w; } m = std::max(m, std::fabs(a)); }
    return 20 * std::log10(m + 1e-20); }
inline long rssKb() { FILE* f = std::fopen("/proc/self/status", "r"); if (!f) return 0; char l[256]; long v = 0; while (std::fgets(l, sizeof l, f)) if (!std::strncmp(l, "VmRSS:", 6)) { std::sscanf(l + 6, "%ld", &v); break; } std::fclose(f); return v; }
inline double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// block runner for mono/stereo float**
template <class F> void runBlocks(vector<float>& l, vector<float>* r, int block, F&& f) {
    int n = (int)l.size(); for (int s = 0; s < n; s += block) { int len = std::min(block, n - s); float* p[2] = {l.data() + s, r ? r->data() + s : l.data() + s}; f(p, r ? 2 : 1, len); } }
}
