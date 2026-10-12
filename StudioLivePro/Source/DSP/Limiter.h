// Studio Live Pro - True-peak look-ahead limiter
#pragma once
#include "Common.h"

namespace slp {

// 4x inter-sample peak estimator (8-tap Hann-windowed sinc per phase). Output lags input by 4 samples.
class TruePeakDetector {
public:
    static constexpr int kHalf = 16, kTaps = 2 * kHalf, kLag = kHalf;
    TruePeakDetector() {
        for (int p = 1; p < 4; ++p) for (int j = 0; j < kTaps; ++j) {
            const double t = (kHalf - 1 - j) + p / 4.0, s = std::sin(kPi * t) / (kPi * t);
            const double w = 0.42 + 0.5 * std::cos(kPi * t / kHalf) + 0.08 * std::cos(2 * kPi * t / kHalf); // Blackman centred on the interval
            coef_[p - 1][j] = (float)(s * w);
        }
    }
    void reset() { h_.fill(0); }
    // Returns the peak magnitude around the sample kLag samples ago (inter-sample points included when interp==true).
    inline float process(float x, bool interp) {
        for (int i = kTaps - 1; i > 0; --i) h_[(size_t)i] = h_[(size_t)(i - 1)];
        h_[0] = x; // h_[k] = x[n-k]
        float m = std::fabs(h_[kLag]);
        if (interp) for (int p = 0; p < 3; ++p) {
            float a = 0; for (int j = 0; j < kTaps; ++j) a += coef_[p][j] * h_[(size_t)(kTaps - 1 - j)];
            m = std::max(m, std::fabs(a));
        }
        return m * 1.03f; // +0.26 dB safety margin: 32-tap interpolation under-reads dense HF content by up to ~0.25 dB
    }
private:
    std::array<float, kTaps> h_{}; float coef_[3][kTaps]{};
};

enum class LimStyle { Transparent, Punchy, Warm };
struct LimiterParams {
    float thresholdDb = -3.f, ceilingDb = -1.f, lookaheadMs = 3.f, releaseMs = 120.f, transparency = 0.7f, pumpingControl = 0.5f;
    bool truePeak = true; LimStyle style = LimStyle::Transparent;
    template <class V> void visit(V& v) {
        v.field("thr", thresholdDb); v.field("ceil", ceilingDb); v.field("look", lookaheadMs); v.field("rel", releaseMs);
        v.field("transp", transparency); v.field("pump", pumpingControl); v.field("tp", truePeak); v.enumField("style", style);
    }
};

class TruePeakLimiter {
public:
    static constexpr int kMaxLook = 1024;
    void prepare(double fs) { fs_ = fs; for (auto& d : dly_) d.prepare(kMaxLook + 16); dqIdx_.assign(kMaxLook + 4, 0); dqVal_.assign(kMaxLook + 4, 1.f); avg_.assign(kMaxLook + 4, 1.f); reset(); setParams(p_); }
    void reset() {
        for (auto& d : dly_) d.clear(); for (auto& t : tpd_) t.reset();
        std::fill(avg_.begin(), avg_.end(), 1.f); sum_ = N_; ai_ = 0; dqH_ = dqT_ = 0; n_ = 0; env_ = 1.f; grAvg_ = 0; grMeter = 0;
    }
    void setParams(const LimiterParams& p) {
        p_ = p;
        float lookMs = p.lookaheadMs * (0.4f + 0.6f * p.transparency) * (p.style == LimStyle::Punchy ? 0.6f : 1.f);
        const int N = std::min(std::max((int)(lookMs * 0.001 * fs_), 1), kMaxLook);
        if (N != N_) { N_ = N; reset(); }
        ceil_ = dbToLin(p.ceilingDb); gain_ = dbToLin(-p.thresholdDb);
        relC_ = timeCoef(p.releaseMs * (p.style == LimStyle::Punchy ? 0.5f : 1.f), fs_); grC_ = timeCoef(500.f, fs_);
        warm_ = p.style == LimStyle::Warm;
    }
    int latencySamples() const { return N_ - 1 + TruePeakDetector::kLag; }
    void process(float* const* x, int nCh, int n) {
        const int D = N_ - 1 + TruePeakDetector::kLag; float minG = 1.f;
        for (int i = 0; i < n; ++i) {
            float xs[2] = {0, 0}, tp = 0;
            for (int c = 0; c < nCh; ++c) {
                float v = x[c][i] * gain_; if (warm_) v = std::tanh(v * 0.7f) / 0.7f * 0.97f + v * 0.03f;
                xs[c] = v; tp = std::max(tp, tpd_[c].process(v, p_.truePeak));
            }
            const float tgt = tp > ceil_ ? ceil_ / tp : 1.f;
            while (dqT_ != dqH_ && dqVal_[(size_t)((dqT_ - 1 + cap()) % cap())] >= tgt) dqT_ = (dqT_ - 1 + cap()) % cap();
            dqIdx_[(size_t)dqT_] = n_; dqVal_[(size_t)dqT_] = tgt; dqT_ = (dqT_ + 1) % cap();
            while (dqIdx_[(size_t)dqH_] <= n_ - N_) dqH_ = (dqH_ + 1) % cap();
            const float m = dqVal_[(size_t)dqH_];
            if (m < env_) env_ = m;
            else { const float k = clampf(grAvg_ / 6.f, 0.f, 1.f); const float rc = std::pow(relC_, 1.f / (1.f + p_.pumpingControl * 4.f * k)); env_ = m + rc * (env_ - m); }
            sum_ += env_ - avg_[(size_t)ai_]; avg_[(size_t)ai_] = env_; ai_ = (ai_ + 1) % N_;
            const float g = (float)(sum_ / N_);
            grAvg_ = (-linToDb(g)) + grC_ * (grAvg_ - (-linToDb(g)));
            minG = std::min(minG, g);
            for (int c = 0; c < nCh; ++c) { dly_[c].push(xs[c]); x[c][i] = std::max(-ceil_, std::min(ceil_, dly_[c].readInt(D) * g)); }
            ++n_;
        }
        grMeter = linToDb(minG);
    }
    float grMeter = 0;
private:
    int cap() const { return (int)dqIdx_.size(); }
    double fs_ = 48000; LimiterParams p_; int N_ = 144; float ceil_ = 0.89f, gain_ = 1, relC_ = 0, grC_ = 0, env_ = 1, grAvg_ = 0; bool warm_ = false;
    DelayLine dly_[2]; TruePeakDetector tpd_[2]; std::vector<long long> dqIdx_; std::vector<float> dqVal_, avg_; int dqH_ = 0, dqT_ = 0, ai_ = 0; double sum_ = 0; long long n_ = 0;
};

} // namespace slp
