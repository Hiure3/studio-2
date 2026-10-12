// Studio Live Pro - 24-band parametric EQ + Dynamic EQ
#pragma once
#include "Biquad.h"

namespace slp {

struct EqBandParams {
    bool enabled = false; FilterType type = FilterType::Bell;
    float freqHz = 1000.f, gainDb = 0.f, q = 0.707f; int slope = 12; // slope: 12/24/36/48 dB/oct for HP/LP
    template <class V> void visit(V& v) {
        v.field("on", enabled); v.enumField("type", type); v.field("f", freqHz);
        v.field("g", gainDb); v.field("q", q); v.field("slope", slope);
    }
};

struct EqParams {
    std::array<EqBandParams, 24> band;
    float outputDb = 0.f; bool autoGain = false;
    template <class V> void visit(V& v) {
        for (size_t i = 0; i < band.size(); ++i) { v.push("b" + std::to_string(i)); band[i].visit(v); v.pop(); }
        v.field("out", outputDb); v.field("autogain", autoGain);
    }
};

class ParametricEQ {
public:
    static constexpr int kBands = 24, kStages = 4;
    void prepare(double fs) { fs_ = fs; reset(); }
    void reset() { for (auto& s : st_) { s.z[0].reset(); s.z[1].reset(); } }

    void setParams(const EqParams& p) {
        for (int b = 0; b < kBands; ++b) {
            n_[(size_t)b] = 0;
            const auto& bp = p.band[(size_t)b];
            if (!bp.enabled) continue;
            auto& n = n_[(size_t)b];
            auto S = [&](int k) -> Stage& { return st_[(size_t)(b * kStages + k)]; };
            if (bp.type == FilterType::HighPass || bp.type == FilterType::LowPass) {
                int stages = std::min(std::max(bp.slope / 12, 1), kStages), order = 2 * stages;
                for (int k = 0; k < stages; ++k) {
                    double q = 1.0 / (2.0 * std::sin(kPi * (2 * k + 1) / (2.0 * order)));
                    S(n++).c = BiquadCoeffs::make(bp.type, fs_, bp.freqHz, 0, q);
                }
            } else if (bp.type == FilterType::Tilt) {
                S(n++).c = BiquadCoeffs::make(FilterType::LowShelf, fs_, bp.freqHz, bp.gainDb * 0.5, 0.707);
                S(n++).c = BiquadCoeffs::make(FilterType::HighShelf, fs_, bp.freqHz, -bp.gainDb * 0.5, 0.707);
            } else {
                S(n++).c = BiquadCoeffs::make(bp.type, fs_, bp.freqHz, bp.gainDb, bp.q);
            }
        }
        autoGainDb_ = p.autoGain ? computeAutoGain() : 0.0f;
        outGain_ = dbToLin(p.outputDb + autoGainDb_);
    }

    void process(float* const* x, int nCh, int n) {
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < n_[(size_t)b]; ++k) {
                Stage& s = st_[(size_t)(b * kStages + k)];
                for (int c = 0; c < nCh; ++c) {
                    float* d = x[c];
                    for (int i = 0; i < n; ++i) d[i] = s.z[c].process(s.c, d[i]);
                }
            }
        if (outGain_ != 1.0f) for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) x[c][i] *= outGain_;
    }

    // Magnitude response of the whole EQ (dB), excluding output gain.
    double responseDb(double f) const {
        double m = 1.0;
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < n_[(size_t)b]; ++k) m *= st_[(size_t)(b * kStages + k)].c.magnitude(fs_, f);
        return 20.0 * std::log10(std::max(m, 1e-12));
    }
    float autoGainDb() const { return autoGainDb_; }
    bool anyActive() const { for (int v : n_) if (v) return true; return false; }

private:
    // Perceptual auto-gain: compensates by the change in A-weighted, pink-noise-weighted energy.
    float computeAutoGain() const {
        double num = 0, den = 0;
        for (int i = 0; i < 96; ++i) {
            const double f = 20.0 * std::pow(1000.0, i / 95.0); // 20 Hz .. 20 kHz log spaced (pink => uniform in log f)
            const double f2 = f * f, c1 = 12194.0 * 12194.0, c2 = 20.6 * 20.6, c3 = 107.7 * 107.7, c4 = 737.9 * 737.9;
            const double ra = (c1 * f2 * f2) / ((f2 + c2) * std::sqrt((f2 + c3) * (f2 + c4)) * (f2 + c1));
            const double w = ra * ra; // A-weighting power
            double m = 1.0;
            for (int b = 0; b < kBands; ++b)
                for (int k = 0; k < n_[(size_t)b]; ++k) m *= st_[(size_t)(b * kStages + k)].c.magnitude(fs_, f);
            num += w * m * m; den += w;
        }
        const double ratio = num / std::max(den, 1e-30);
        return (float)std::min(12.0, std::max(-12.0, -10.0 * std::log10(std::max(ratio, 1e-9))));
    }
    struct Stage { BiquadCoeffs c; BiquadState z[2]; };
    double fs_ = 48000.0; std::array<Stage, kBands * kStages> st_{}; std::array<int, kBands> n_{};
    float outGain_ = 1.0f, autoGainDb_ = 0.0f;
};

// ---------------- Dynamic EQ ----------------
struct DynEqBandParams {
    bool enabled = false; FilterType type = FilterType::Bell;
    float freqHz = 1000.f, q = 1.f, gainDb = 0.f, thresholdDb = -30.f, ratio = 2.f;
    float attackMs = 10.f, releaseMs = 100.f, rangeDb = 6.f; bool upward = false;
    template <class V> void visit(V& v) {
        v.field("on", enabled); v.enumField("type", type); v.field("f", freqHz); v.field("q", q);
        v.field("g", gainDb); v.field("thr", thresholdDb); v.field("ratio", ratio);
        v.field("att", attackMs); v.field("rel", releaseMs); v.field("range", rangeDb); v.field("up", upward);
    }
};
struct DynEqParams {
    std::array<DynEqBandParams, 4> band;
    template <class V> void visit(V& v) {
        for (size_t i = 0; i < band.size(); ++i) { v.push("d" + std::to_string(i)); band[i].visit(v); v.pop(); }
    }
};

class DynamicEQ {
public:
    static constexpr int kBands = 4, kSub = 16;
    void prepare(double fs) { fs_ = fs; reset(); setParams(p_); }
    void reset() { for (auto& b : bd_) { b.det = {}; b.env = 0; b.dyn = 0; b.z[0].reset(); b.z[1].reset(); } }
    void setParams(const DynEqParams& p) {
        p_ = p;
        for (int i = 0; i < kBands; ++i) {
            auto& b = bd_[(size_t)i]; const auto& bp = p.band[(size_t)i];
            b.detC = BiquadCoeffs::make(FilterType::BandPass, fs_, bp.freqHz, 0, std::max(bp.q, 0.3f));
            b.atk = timeCoef(bp.attackMs, fs_); b.rel = timeCoef(bp.releaseMs, fs_);
        }
    }
    void process(float* const* x, int nCh, int n) {
        for (int i = 0; i < kBands; ++i) {
            const auto& bp = p_.band[(size_t)i]; if (!bp.enabled) continue;
            auto& b = bd_[(size_t)i];
            for (int s0 = 0; s0 < n; s0 += kSub) {
                const int len = std::min(kSub, n - s0);
                for (int k = 0; k < len; ++k) {
                    float m = 0; for (int c = 0; c < nCh; ++c) m += x[c][s0 + k]; m /= (float)nCh;
                    const float a = std::fabs(b.det.process(b.detC, m));
                    b.env = a > b.env ? a + b.atk * (b.env - a) : a + b.rel * (b.env - a);
                }
                const float envDb = linToDb(b.env * 1.5708f); // bandpass-rectified avg -> approx peak
                float dyn = 0;
                if (!bp.upward) { float over = envDb - bp.thresholdDb; if (over > 0) dyn = -std::min(bp.rangeDb, over * (1.f - 1.f / std::max(bp.ratio, 1.f))); }
                else { float under = bp.thresholdDb - envDb; if (under > 0) dyn = std::min(bp.rangeDb, under * (1.f - 1.f / std::max(bp.ratio, 1.f))); }
                b.dyn = dyn;
                const auto c = BiquadCoeffs::make(bp.type, fs_, bp.freqHz, bp.gainDb + dyn, bp.q);
                for (int ch = 0; ch < nCh; ++ch) for (int k = 0; k < len; ++k) x[ch][s0 + k] = b.z[ch].process(c, x[ch][s0 + k]);
            }
        }
    }
    float dynGainDb(int band) const { return bd_[(size_t)band].dyn; }
private:
    struct BandData { BiquadCoeffs detC; BiquadState det; BiquadState z[2]; float env = 0, dyn = 0, atk = 0, rel = 0; };
    double fs_ = 48000; DynEqParams p_; std::array<BandData, kBands> bd_{};
};

} // namespace slp
