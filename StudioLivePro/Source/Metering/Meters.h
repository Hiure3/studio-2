// Studio Live Pro - Metering: peak/RMS/crest, ITU-R BS.1770-4 loudness (LUFS), true peak, correlation/phase/width
#pragma once
#include "../DSP/Biquad.h"
#include "../DSP/Limiter.h"

namespace slp {

class LevelMeter {
public:
    void prepare(double fs) { fs_ = fs; decay_ = std::exp(-1.f / (1.5f * (float)fs)); rmsC_ = timeCoef(300.f, fs); reset(); }
    void reset() { peakHold = 0; rmsPow = 0; maxPeak = 0; maxPow = 0; }
    void process(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            const float a = std::fabs(x[i]); peakHold = a > peakHold ? a : peakHold * decay_; maxPeak = std::max(maxPeak, a);
            rmsPow = x[i] * x[i] + rmsC_ * (rmsPow - x[i] * x[i]); maxPow = std::max(maxPow, rmsPow);
        }
    }
    float peakDb() const { return linToDb(peakHold); }
    float rmsDb() const { return 10.f * std::log10(rmsPow + 1e-12f); }
    float crestDb() const { return linToDb(maxPeak) - 10.f * std::log10(maxPow + 1e-12f); }
    float peakHold = 0, rmsPow = 0, maxPeak = 0, maxPow = 0;
private:
    double fs_ = 48000; float decay_ = 0.999f, rmsC_ = 0.99f;
};

class LoudnessMeter {
public:
    void prepare(double fs, int channels = 2) {
        fs_ = fs; nCh_ = std::min(channels, 2); sub_ = (int)(fs * 0.1);
        { // stage 1: high shelf (+4 dB, ~1.68 kHz); stage 2: RLB high-pass (38 Hz)
            double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            double K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
            s1_.b0 = (Vh + Vb * K / Q + K * K) / a0; s1_.b1 = 2.0 * (K * K - Vh) / a0; s1_.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s1_.a1 = 2.0 * (K * K - 1.0) / a0; s1_.a2 = (1.0 - K / Q + K * K) / a0;
            f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan(kPi * f0 / fs); a0 = 1.0 + K / Q + K * K;
            s2_.b0 = 1.0; s2_.b1 = -2.0; s2_.b2 = 1.0; s2_.a1 = 2.0 * (K * K - 1.0) / a0; s2_.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() {
        for (auto& z : z1_) z.reset(); for (auto& z : z2_) z.reset(); maxPk_ = maxTp_ = 0; acc_ = 0; cnt_ = 0; blocks_ = 0; ring_.fill(0.0);
        histI_.fill(0); histS_.fill(0); mom = st = -120.f; tpTrue = -120.f; peak = -120.f; tp_[0].reset(); tp_[1].reset();
    }
    void process(const float* const* x, int n) {
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < nCh_; ++c) {
                const float v = x[c][i]; const float a = std::fabs(v);
                if (a > maxPk_) maxPk_ = a;
                const float t = tp_[c].process(v, true); if (t > maxTp_) maxTp_ = t;
                const float y = z2_[c].process(s2_, z1_[c].process(s1_, v)); acc_ += (double)y * y;
            }
            if (++cnt_ >= sub_) { blockDone(); }
        }
        peak = linToDb(maxPk_); tpTrue = linToDb(maxTp_);
    }
    float momentary() const { return mom; } float shortTerm() const { return st; } float truePeakDb() const { return tpTrue; } float peakDb() const { return peak; }
    float integrated() const { return gated(histI_, -10.0); }
    float loudnessRange() const {
        const float absThr = -70.f; double sum = 0; long cnt = 0;
        for (int b = 0; b < kBins; ++b) { const float l = binLoud(b); if (l > absThr && histS_[(size_t)b]) { sum += histS_[(size_t)b] * std::pow(10.0, (l + 0.691) / 10.0); cnt += histS_[(size_t)b]; } }
        if (!cnt) return 0.f; const double rel = 10.0 * std::log10(sum / cnt) - 0.691 - 20.0; long tot = 0;
        for (int b = 0; b < kBins; ++b) if (binLoud(b) > rel) tot += histS_[(size_t)b];
        if (!tot) return 0.f; long run = 0; float lo = 0, hi = 0; bool gl = false, gh = false;
        for (int b = 0; b < kBins; ++b) if (binLoud(b) > rel) { run += histS_[(size_t)b]; if (!gl && run >= 0.10 * tot) { lo = binLoud(b); gl = true; } if (!gh && run >= 0.95 * tot) { hi = binLoud(b); gh = true; } }
        return hi - lo;
    }
    float mom = -120.f, st = -120.f;
private:
    static constexpr int kBins = 800; // 0.1 LU bins covering -70 .. +10 LUFS
    static float binLoud(int b) { return -70.f + 0.1f * ((float)b + 0.5f); }
    static int binOf(float l) { return std::min(kBins - 1, std::max(0, (int)((l + 70.f) * 10.f))); }
    float gated(const std::array<uint32_t, kBins>& h, double relLU) const {
        double sum = 0; long cnt = 0;
        for (int b = 0; b < kBins; ++b) if (h[(size_t)b]) { sum += h[(size_t)b] * std::pow(10.0, (binLoud(b) + 0.691) / 10.0); cnt += h[(size_t)b]; }
        if (!cnt) return -120.f; const double thr = 10.0 * std::log10(sum / cnt) - 0.691 + relLU; sum = 0; cnt = 0;
        for (int b = 0; b < kBins; ++b) if (h[(size_t)b] && binLoud(b) > thr) { sum += h[(size_t)b] * std::pow(10.0, (binLoud(b) + 0.691) / 10.0); cnt += h[(size_t)b]; }
        return cnt ? (float)(10.0 * std::log10(sum / cnt) - 0.691) : -120.f;
    }
    void blockDone() {
        const double p = acc_ / sub_; acc_ = 0; cnt_ = 0; ring_[(size_t)(blocks_ % 30)] = p; ++blocks_;
        if (blocks_ >= 4) {
            double s = 0; for (int k = 0; k < 4; ++k) s += ring_[(size_t)((blocks_ - 1 - k) % 30)];
            mom = (float)(-0.691 + 10.0 * std::log10(s / 4 + 1e-12)); if (mom > -70.f) ++histI_[(size_t)binOf(mom)];
        }
        if (blocks_ >= 30) {
            double s = 0; for (int k = 0; k < 30; ++k) s += ring_[(size_t)k];
            st = (float)(-0.691 + 10.0 * std::log10(s / 30 + 1e-12)); if (st > -70.f) ++histS_[(size_t)binOf(st)];
        }
    }
    double fs_ = 48000; int nCh_ = 2, sub_ = 4800, cnt_ = 0; long blocks_ = 0; double acc_ = 0; BiquadCoeffs s1_, s2_; BiquadState z1_[2], z2_[2];
    std::array<double, 30> ring_{}; std::array<uint32_t, kBins> histI_{}, histS_{}; float maxPk_ = 0, maxTp_ = 0, tpTrue = -120.f, peak = -120.f; TruePeakDetector tp_[2];
};

class CorrelationMeter {
public:
    void prepare(double fs) { c_ = timeCoef(300.f, fs); reset(); }
    void reset() { ll_ = rr_ = lr_ = mm_ = ss_ = 0; }
    void process(const float* l, const float* r, int n) {
        for (int i = 0; i < n; ++i) {
            const float a = l[i], b = r[i], m = 0.5f * (a + b), s = 0.5f * (a - b);
            ll_ = a * a + c_ * (ll_ - a * a); rr_ = b * b + c_ * (rr_ - b * b); lr_ = a * b + c_ * (lr_ - a * b);
            mm_ = m * m + c_ * (mm_ - m * m); ss_ = s * s + c_ * (ss_ - s * s);
        }
    }
    float correlation() const { const float d = std::sqrt(ll_ * rr_); return d > 1e-12f ? clampf(lr_ / d, -1.f, 1.f) : 0.f; }
    float width() const { const float m = std::sqrt(mm_), s = std::sqrt(ss_); return (m + s) > 1e-9f ? s / (m + s) : 0.f; } // 0 mono .. 1 side-only
    float monoLossDb() const { const float avg = 0.5f * (ll_ + rr_); return avg > 1e-12f ? 10.f * std::log10(std::max(mm_, 1e-12f) / avg) : 0.f; } // 0 dB = mono-safe
    bool monoCompatible() const { return monoLossDb() > -3.f; }
private:
    float c_ = 0.99f, ll_ = 0, rr_ = 0, lr_ = 0, mm_ = 0, ss_ = 0;
};

} // namespace slp
