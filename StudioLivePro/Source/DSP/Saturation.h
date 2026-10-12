// Studio Live Pro - Oversampler, Saturation (8 models), Tape engine, Soft clipper
#pragma once
#include "Biquad.h"

namespace slp {

// Linear-phase FIR oversampler (cascaded 2x stages, 97-tap Blackman-windowed sinc).
class Oversampler {
public:
    static constexpr int kTaps = 97;
    // Allocates for the maximum (4x) once; setLevels() switches without allocating (realtime-safe).
    void prepare(int levels, int maxBlock, int nCh) {
        levels_ = std::min(std::max(levels, 0), 2); nCh_ = nCh;
        h_.assign(kTaps, 0.f); const int M = (kTaps - 1) / 2; double sum = 0; std::vector<double> t(kTaps);
        for (int n = 0; n < kTaps; ++n) {
            const double k = n - M, x = 0.5 * k; // cutoff 0.25 (fraction of the high rate)
            const double s = (k == 0) ? 0.5 : std::sin(kPi * x) / (kPi * k);
            const double w = 0.42 - 0.5 * std::cos(2 * kPi * n / (kTaps - 1)) + 0.08 * std::cos(4 * kPi * n / (kTaps - 1));
            t[(size_t)n] = s * w; sum += t[(size_t)n];
        }
        for (int n = 0; n < kTaps; ++n) h_[(size_t)n] = (float)(t[(size_t)n] / sum);
        st_.assign((size_t)(nCh * 2), Stage());
        for (auto& s : st_) { s.up.assign(2 * kTaps, 0.f); s.dn.assign(2 * kTaps, 0.f); s.upPos = s.dnPos = 0; }
        tmp1_.assign((size_t)maxBlock * 2, 0.f); tmp2_.assign((size_t)maxBlock * 4, 0.f);
    }
    int levels() const { return levels_; }
    void setLevels(int l) {
        l = std::min(std::max(l, 0), 2); if (l == levels_) return; levels_ = l;
        for (auto& s : st_) { std::fill(s.up.begin(), s.up.end(), 0.f); std::fill(s.dn.begin(), s.dn.end(), 0.f); s.upPos = s.dnPos = 0; }
    }
    int latencySamples() const { int l = 0; for (int s = 0; s < levels_; ++s) l += (kTaps - 1) >> (s + 1); return l; }

    template <class F> void process(int ch, float* x, int n, F&& shaper) {
        if (levels_ == 0) { for (int i = 0; i < n; ++i) x[i] = shaper(x[i]); return; }
        Stage& s0 = st_[(size_t)(ch * 2)]; Stage& s1 = st_[(size_t)(ch * 2 + 1)];
        up(s0, x, tmp1_.data(), n);
        if (levels_ == 1) {
            for (int i = 0; i < 2 * n; ++i) tmp1_[(size_t)i] = shaper(tmp1_[(size_t)i]);
            down(s0, tmp1_.data(), x, n);
        } else {
            up(s1, tmp1_.data(), tmp2_.data(), 2 * n);
            for (int i = 0; i < 4 * n; ++i) tmp2_[(size_t)i] = shaper(tmp2_[(size_t)i]);
            down(s1, tmp2_.data(), tmp1_.data(), 2 * n);
            down(s0, tmp1_.data(), x, n);
        }
    }
private:
    struct Stage { std::vector<float> up, dn; int upPos = 0, dnPos = 0; };
    static inline void pushHist(std::vector<float>& h, int& pos, float v) {
        pos = (pos + kTaps - 1) % kTaps; h[(size_t)pos] = v; h[(size_t)(pos + kTaps)] = v;
    }
    void up(Stage& s, const float* in, float* out, int n) {
        for (int i = 0; i < n; ++i) {
            pushHist(s.up, s.upPos, in[i]);
            const float* hp = &s.up[(size_t)s.upPos]; float a = 0, b = 0;
            for (int j = 0; 2 * j < kTaps; ++j) a += h_[(size_t)(2 * j)] * hp[j];
            for (int j = 0; 2 * j + 1 < kTaps; ++j) b += h_[(size_t)(2 * j + 1)] * hp[j];
            out[2 * i] = 2.f * a; out[2 * i + 1] = 2.f * b;
        }
    }
    void down(Stage& s, const float* in, float* out, int n) {
        for (int i = 0; i < n; ++i) {
            pushHist(s.dn, s.dnPos, in[2 * i]); pushHist(s.dn, s.dnPos, in[2 * i + 1]);
            const float* hp = &s.dn[(size_t)s.dnPos]; float a = 0;
            for (int k = 0; k < kTaps; ++k) a += h_[(size_t)k] * hp[k];
            out[i] = a;
        }
    }
    int levels_ = 0, nCh_ = 2; std::vector<float> h_, tmp1_, tmp2_; std::vector<Stage> st_;
};

// ---------------- Saturation ----------------
enum class SatModel { Clean, Tube, Tape, Transformer, Console, FET, Warm, Vintage };
struct SatParams {
    SatModel model = SatModel::Tube; float driveDb = 6.f, tone = 0.f, mix = 1.f, outputDb = 0.f; int oversample = 1; // 1,2,4
    template <class V> void visit(V& v) {
        v.enumField("model", model); v.field("drive", driveDb); v.field("tone", tone); v.field("mix", mix);
        v.field("out", outputDb); v.field("os", oversample);
    }
};
inline float satShape(SatModel m, float x) {
    switch (m) {
    case SatModel::Clean: return x / (1.f + std::fabs(x));
    case SatModel::Tube: case SatModel::Warm: { const float b = 0.15f, k = 1.f - std::tanh(b) * std::tanh(b); return (std::tanh(x + b) - std::tanh(b)) / k; }
    case SatModel::Tape: return std::tanh(x);
    case SatModel::Transformer: { const float c = std::max(-1.f, std::min(1.f, x)); return c - c * c * c * (1.f / 3.f); }
    case SatModel::Console: return x / std::sqrt(1.f + x * x);
    case SatModel::FET: return x > 0 ? std::tanh(x) : std::tanh(0.85f * x) / 0.85f;
    case SatModel::Vintage: { const float c = std::max(-1.f, std::min(1.f, x)); return 0.6f * std::tanh(x) + 0.4f * (c - c * c * c * (1.f / 3.f)); }
    }
    return x;
}
class Saturator {
public:
    void prepare(double fs, int maxBlock) { fs_ = fs; maxBlock_ = maxBlock; ov_.prepare(2, maxBlock, 2); ov_.setLevels(0); for (auto& d : dry_) d.prepare(maxBlock + 512); reset(); setParams(p_); }
    void reset() { for (int c = 0; c < 2; ++c) { dc_[c] = 0; dcx_[c] = 0; lp_[c] = 0; } t1_.reset(); t2_.reset(); for (auto& d : dry_) d.clear(); }
    void setParams(const SatParams& p) {
        p_ = p;
        const int lv = p.oversample >= 4 ? 2 : (p.oversample >= 2 ? 1 : 0);
        if (lv != ov_.levels()) { ov_.setLevels(lv); for (auto& d : dry_) d.clear(); }
        drive_ = dbToLin(p.driveDb); out_ = dbToLin(p.outputDb);
        t1_.set(FilterType::HighShelf, fs_, 2500.0, p.tone * 6.0, 0.7); t2_.set(FilterType::LowShelf, fs_, 400.0, -p.tone * 4.0, 0.7);
        float lpHz = (p.model == SatModel::Tape) ? 14000.f : (p.model == SatModel::Warm) ? 9000.f : (p.model == SatModel::Vintage) ? 11000.f : 0.f;
        lpA_ = lpHz > 0 ? 1.f - std::exp(-2.f * (float)kPi * lpHz / (float)fs_) : 1.f;
        dcR_ = 1.f - 2.f * (float)kPi * 5.f / (float)fs_;
    }
    int latencySamples() const { return ov_.latencySamples(); }
    void process(float* const* x, int nCh, int n) {
        const int lat = ov_.latencySamples();
        for (int c = 0; c < nCh; ++c) {
            float* d = x[c];
            if (p_.mix < 0.999f) for (int i = 0; i < n; ++i) { dry_[c].push(d[i]); }
            const SatModel m = p_.model; const float dr = drive_;
            ov_.process(c, d, n, [m, dr](float s) { return satShape(m, s * dr) / std::max(dr, 1.f); });
            for (int i = 0; i < n; ++i) {
                float y = d[i];
                lp_[c] += lpA_ * (y - lp_[c]); y = lpA_ < 1.f ? lp_[c] : y;
                const float o = y - dcx_[c] + dcR_ * dc_[c]; dcx_[c] = y; dc_[c] = o; y = o;
                if (p_.tone != 0.f) y = t2_.process(c, t1_.process(c, y));
                y *= out_;
                if (p_.mix < 0.999f) { const float dry = dry_[c].readInt(n - 1 - i + lat); y = dry * (1.f - p_.mix) + y * p_.mix; }
                d[i] = y;
            }
        }
    }
private:
    double fs_ = 48000; int maxBlock_ = kMaxBlock; SatParams p_; Oversampler ov_; DelayLine dry_[2];
    float drive_ = 1, out_ = 1, lpA_ = 1, dcR_ = 0.999f, dc_[2]{}, dcx_[2]{}, lp_[2]{}; Biquad2ch t1_, t2_;
};

// ---------------- Tape engine ----------------
struct TapeParams {
    int speedIps = 15; float biasDb = 0.f, saturation = 0.4f, compression = 0.3f, wow = 0.2f, flutter = 0.2f,
        hysteresis = 0.2f, headBumpDb = 2.f, hfRolloffHz = 14000.f, noiseDb = -90.f, mix = 1.f; int oversample = 2;
    template <class V> void visit(V& v) {
        v.field("speed", speedIps); v.field("bias", biasDb); v.field("sat", saturation); v.field("comp", compression);
        v.field("wow", wow); v.field("flutter", flutter); v.field("hyst", hysteresis); v.field("bump", headBumpDb);
        v.field("hf", hfRolloffHz); v.field("noise", noiseDb); v.field("mix", mix); v.field("os", oversample);
    }
};
class TapeEngine {
public:
    void prepare(double fs, int maxBlock) { fs_ = fs; maxBlock_ = maxBlock; ov_.prepare(2, maxBlock, 2); ov_.setLevels(1); for (int c = 0; c < 2; ++c) { mod_[c].prepare((int)(fs * 0.01) + 16); dry_[c].prepare(maxBlock + 1024); } reset(); setParams(p_); }
    void reset() { phW_ = phF_ = 0; for (int c = 0; c < 2; ++c) { mod_[c].clear(); dry_[c].clear(); hys_[c] = 0; env_[c] = 0; lp_[c] = 0; } bump_.reset(); hf_.reset(); }
    void setParams(const TapeParams& p) {
        p_ = p; const int lv = p.oversample >= 4 ? 2 : (p.oversample >= 2 ? 1 : 0);
        if (lv != ov_.levels()) ov_.setLevels(lv);
        const double bumpHz = p.speedIps <= 7 ? 90.0 : (p.speedIps <= 15 ? 60.0 : 45.0);
        bump_.set(FilterType::Bell, fs_, bumpHz, p.headBumpDb, 0.9);
        const float speedHf = p.speedIps <= 7 ? 0.75f : (p.speedIps <= 15 ? 1.f : 1.2f);
        hf_.set(FilterType::LowPass, fs_, std::min(p.hfRolloffHz * speedHf, (float)(fs_ * 0.45)), 0, 0.6);
        drive_ = 1.f + 5.f * p.saturation; biasAsym_ = 0.05f * (p.biasDb + 3.f) * 0.3f; noiseG_ = dbToLin(p.noiseDb);
        compAtk_ = timeCoef(5.f, fs_); compRel_ = timeCoef(120.f, fs_);
    }
    int latencySamples() const { return ov_.latencySamples(); }
    void process(float* const* x, int nCh, int n) {
        const int lat = ov_.latencySamples(); const float wowDepth = p_.wow * 0.0006f * (float)fs_ * 0.5f, flDepth = p_.flutter * 0.00008f * (float)fs_;
        const float center = wowDepth + flDepth + 4.f;
        for (int c = 0; c < nCh; ++c) {
            float* d = x[c]; float phW = phW_, phF = phF_;
            for (int i = 0; i < n; ++i) { dry_[c].push(d[i]); }
            for (int i = 0; i < n; ++i) {   // wow/flutter (modulated delay)
                phW += 2.f * (float)kPi * 0.6f / (float)fs_; phF += 2.f * (float)kPi * 9.f / (float)fs_;
                if (phW > 2 * (float)kPi) phW -= 2 * (float)kPi; if (phF > 2 * (float)kPi) phF -= 2 * (float)kPi;
                mod_[c].push(d[i]);
                d[i] = mod_[c].read(center + wowDepth * std::sin(phW) + flDepth * std::sin(phF));
            }
            if (c == nCh - 1) { phW_ = phW; phF_ = phF; }
            for (int i = 0; i < n; ++i) {   // tape compression (soft, program dependent)
                const float a = std::fabs(d[i]); env_[c] = a > env_[c] ? a + compAtk_ * (env_[c] - a) : a + compRel_ * (env_[c] - a);
                const float red = 1.f / (1.f + p_.compression * 2.f * env_[c]); d[i] *= red;
            }
            const float dr = drive_, as = biasAsym_, hy = p_.hysteresis; float h = hys_[c];
            ov_.process(c, d, n, [dr, as, hy, &h](float s) {
                const float v = std::tanh(s * dr + as) - std::tanh(as);
                const float y = v * (1.f - hy * 0.5f) + h * hy * 0.5f;  // magnetic memory
                h = y; return y / dr;
            });
            hys_[c] = h;
            for (int i = 0; i < n; ++i) {
                float y = bump_.process(c, d[i]); y = hf_.process(c, y);
                if (p_.noiseDb > -119.f) y += noise_.next() * noiseG_;
                const float dry = dry_[c].readInt(n - 1 - i + center_int() + lat);
                d[i] = dry * (1.f - p_.mix) + y * p_.mix;
            }
        }
    }
private:
    int center_int() const { const float wd = p_.wow * 0.0006f * (float)fs_ * 0.5f, fd = p_.flutter * 0.00008f * (float)fs_; return (int)(wd + fd + 4.f); }
    double fs_ = 48000; int maxBlock_ = kMaxBlock; TapeParams p_; Oversampler ov_; DelayLine mod_[2], dry_[2]; Biquad2ch bump_, hf_; Rng noise_;
    float phW_ = 0, phF_ = 0, hys_[2]{}, env_[2]{}, lp_[2]{}, drive_ = 1, biasAsym_ = 0, noiseG_ = 0, compAtk_ = 0, compRel_ = 0;
};

// ---------------- Soft clipper ----------------
enum class ClipMode { Stereo, MidSide };
struct ClipParams {
    float thresholdDb = -3.f, ceilingDb = -1.f, softness = 0.7f, mix = 1.f; int oversample = 2; ClipMode mode = ClipMode::Stereo;
    template <class V> void visit(V& v) { v.field("thr", thresholdDb); v.field("ceil", ceilingDb); v.field("soft", softness); v.field("mix", mix); v.field("os", oversample); v.enumField("mode", mode); }
};
class SoftClipper {
public:
    void prepare(double fs, int maxBlock) { fs_ = fs; maxBlock_ = maxBlock; ov_.prepare(2, maxBlock, 2); ov_.setLevels(1); for (auto& d : dry_) d.prepare(maxBlock + 512); setParams(p_); }
    void setParams(const ClipParams& p) {
        p_ = p; const int lv = p.oversample >= 4 ? 2 : (p.oversample >= 2 ? 1 : 0);
        if (lv != ov_.levels()) ov_.setLevels(lv);
    }
    int latencySamples() const { return ov_.latencySamples(); }
    void process(float* const* x, int nCh, int n) {
        const float t = dbToLin(p_.thresholdDb), c = std::max(dbToLin(p_.ceilingDb), t + 1e-4f), s = p_.softness; const int lat = ov_.latencySamples();
        auto shape = [t, c, s](float v) {
            const float a = std::fabs(v); if (a <= t) return v;
            const float soft = t + (c - t) * std::tanh((a - t) / (c - t)), hard = std::min(a, c);
            const float y = hard + (soft - hard) * s; return v < 0 ? -y : y; };
        if (nCh == 2 && p_.mode == ClipMode::MidSide) for (int i = 0; i < n; ++i) { const float m = (x[0][i] + x[1][i]) * 0.5f, sd = (x[0][i] - x[1][i]) * 0.5f; x[0][i] = m; x[1][i] = sd; }
        for (int ch = 0; ch < nCh; ++ch) {
            if (p_.mix < 0.999f) for (int i = 0; i < n; ++i) dry_[ch].push(x[ch][i]);
            ov_.process(ch, x[ch], n, shape);
            if (p_.mix < 0.999f) for (int i = 0; i < n; ++i) x[ch][i] = dry_[ch].readInt(n - 1 - i + lat) * (1.f - p_.mix) + x[ch][i] * p_.mix;
        }
        if (nCh == 2 && p_.mode == ClipMode::MidSide) for (int i = 0; i < n; ++i) { const float l = x[0][i] + x[1][i], r = x[0][i] - x[1][i]; x[0][i] = l; x[1][i] = r; }
    }
private:
    double fs_ = 48000; int maxBlock_ = kMaxBlock; ClipParams p_; Oversampler ov_; DelayLine dry_[2];
};

} // namespace slp
