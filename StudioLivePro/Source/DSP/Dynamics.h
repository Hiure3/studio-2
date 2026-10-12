// Studio Live Pro - Dynamics: Compressor (7 models), Gate/Expander, De-Esser, Transient Designer, Multiband
#pragma once
#include "Biquad.h"

namespace slp {

enum class CompModel { Clean, VCA, FET, Optical, Tube, Bus, Master };

struct CompParams {
    CompModel model = CompModel::Clean;
    float thresholdDb = -18.f, ratio = 3.f, attackMs = 10.f, releaseMs = 100.f, kneeDb = 6.f;
    float makeupDb = 0.f, mix = 1.f, sidechainHpHz = 0.f, lookaheadMs = 0.f;
    bool autoGain = false, externalSidechain = false;
    template <class V> void visit(V& v) {
        v.enumField("model", model); v.field("thr", thresholdDb); v.field("ratio", ratio); v.field("att", attackMs);
        v.field("rel", releaseMs); v.field("knee", kneeDb); v.field("makeup", makeupDb); v.field("mix", mix);
        v.field("schp", sidechainHpHz); v.field("look", lookaheadMs); v.field("auto", autoGain); v.field("ext", externalSidechain);
    }
};

class Compressor {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (auto& d : dly_) d.prepare((int)(fs * 0.012) + 8);
        reset(); setParams(p_);
    }
    void reset() {
        gr_ = 0; pw_ = 0; grAvg_ = 0; inPow_ = outPow_ = 1e-6f; autoDb_ = 0;
        for (auto& s : hp_) s.reset();
        for (auto& d : dly_) d.clear();
        grMeter = 0; inDb = outDb = -120.f;
    }
    void setParams(const CompParams& p) {
        p_ = p; eff_ = p;
        peak_ = true; progRel_ = false; sat_ = 0; float rmsMs = 0;
        switch (p.model) {
        case CompModel::Clean:   peak_ = false; rmsMs = 3; break;
        case CompModel::VCA:     eff_.attackMs = std::max(0.2f, p.attackMs); eff_.kneeDb = std::min(p.kneeDb, 3.f); break;
        case CompModel::FET: {   eff_.attackMs = std::max(0.02f, p.attackMs * 0.2f); eff_.kneeDb = 0;
            const float r[4] = {4, 8, 12, 20}; float best = r[0];
            for (float v : r) if (std::fabs(v - p.ratio) < std::fabs(best - p.ratio)) best = v;
            eff_.ratio = best; sat_ = 0.12f; break; }
        case CompModel::Optical: peak_ = false; rmsMs = 10; eff_.attackMs = std::max(10.f, p.attackMs);
            eff_.kneeDb = std::max(10.f, p.kneeDb); eff_.ratio = std::max(1.5f, std::min(p.ratio * 0.7f, 6.f)); progRel_ = true; break;
        case CompModel::Tube:    peak_ = false; rmsMs = 8; eff_.attackMs = std::max(5.f, p.attackMs);
            eff_.kneeDb = std::max(6.f, p.kneeDb); sat_ = 0.25f; break;
        case CompModel::Bus:     peak_ = false; rmsMs = 5; eff_.ratio = std::min(p.ratio, 10.f);
            eff_.kneeDb = std::max(3.f, p.kneeDb); progRel_ = true; break;
        case CompModel::Master:  peak_ = false; rmsMs = 20; eff_.ratio = std::min(p.ratio, 3.f);
            eff_.attackMs = std::max(15.f, p.attackMs); eff_.kneeDb = std::max(8.f, p.kneeDb); break;
        }
        rmsC_ = timeCoef(rmsMs, fs_); atk_ = timeCoef(eff_.attackMs, fs_); rel_ = timeCoef(eff_.releaseMs, fs_);
        relFast_ = timeCoef(eff_.releaseMs * 0.3f, fs_); relSlow_ = timeCoef(eff_.releaseMs * 4.f, fs_);
        avgC_ = timeCoef(1000.f, fs_); autoC_ = timeCoef(2000.f, fs_); powC_ = timeCoef(1000.f, fs_);
        look_ = std::min((int)(p.lookaheadMs * 0.001 * fs_), (int)(fs_ * 0.011));
        if (p.sidechainHpHz > 10.f) { hpC_ = BiquadCoeffs::make(FilterType::HighPass, fs_, p.sidechainHpHz, 0, 0.7071); hpOn_ = true; }
        else hpOn_ = false;
    }
    int latencySamples() const { return look_; }

    // x: nCh channels (<=2), sc: optional external sidechain (mono)
    void process(float* const* x, int nCh, int n, const float* sc = nullptr) {
        const float T = eff_.thresholdDb, K = eff_.kneeDb, slope = 1.f / std::max(eff_.ratio, 1.f) - 1.f;
        float maxGr = 0, sIn = 0, sOut = 0;
        for (int i = 0; i < n; ++i) {
            float d = 0, dryIn[2] = {0, 0};
            for (int c = 0; c < nCh; ++c) {
                float s = (sc && p_.externalSidechain) ? sc[i] : x[c][i];
                dryIn[c] = x[c][i];
                if (hpOn_) s = hp_[c].process(hpC_, s);
                d = std::max(d, std::fabs(s));
            }
            float level;
            if (peak_) level = d; else { pw_ = d * d + rmsC_ * (pw_ - d * d); level = std::sqrt(pw_); }
            const float lDb = 20.f * std::log10(level + 1e-9f);
            const float over = lDb - T; float target;
            if (2 * over < -K) target = 0;
            else if (2 * over <= K && K > 0) target = slope * (over + K / 2) * (over + K / 2) / (2 * K);
            else target = slope * over;
            float relC = rel_;
            if (progRel_) { const float k = clampf(grAvg_ / 8.f, 0.f, 1.f); relC = relFast_ + (relSlow_ - relFast_) * k; }
            gr_ = target < gr_ ? target + atk_ * (gr_ - target) : target + relC * (gr_ - target);
            grAvg_ = (-gr_) + avgC_ * (grAvg_ + gr_);
            maxGr = std::min(maxGr, gr_);
            const float gLin = std::exp(gr_ * 0.115129255f);
            const float mk = dbToLin(eff_.makeupDb + (p_.autoGain ? autoDb_ : 0.f));
            float pin = 0, pout = 0;
            for (int c = 0; c < nCh; ++c) {
                float dry = dryIn[c];
                if (look_ > 0) { dly_[c].push(dry); dry = dly_[c].readInt(look_); }
                float wet = dry * gLin;
                pin += dry * dry; pout += wet * wet;
                if (sat_ > 0) { const float a = sat_ * (1.f + (-gr_) / 6.f); wet = std::tanh(wet * (1.f + a)) / (1.f + a); }
                wet *= mk;
                x[c][i] = (p_.mix >= 0.999f) ? wet : dry * (1.f - p_.mix) + wet * p_.mix;
                sOut += x[c][i] * x[c][i];
            }
            sIn += pin;
            inPow_ = pin / nCh + powC_ * (inPow_ - pin / nCh);
            outPow_ = pout / nCh + powC_ * (outPow_ - pout / nCh);
            const float tgt = clampf(10.f * std::log10((inPow_ + 1e-9f) / (outPow_ + 1e-9f)), 0.f, 12.f);
            autoDb_ = tgt + autoC_ * (autoDb_ - tgt);
        }
        grMeter = maxGr;
        inDb = 10.f * std::log10(sIn / std::max(1, n * nCh) + 1e-12f);
        outDb = 10.f * std::log10(sOut / std::max(1, n * nCh) + 1e-12f);
    }
    float grMeter = 0, inDb = -120.f, outDb = -120.f;   // block-level meters (GR in dB <= 0)
private:
    double fs_ = 48000; CompParams p_, eff_; bool peak_ = true, progRel_ = false, hpOn_ = false; float sat_ = 0;
    float rmsC_ = 0, atk_ = 0, rel_ = 0, relFast_ = 0, relSlow_ = 0, avgC_ = 0, autoC_ = 0, powC_ = 0;
    float gr_ = 0, pw_ = 0, grAvg_ = 0, inPow_ = 1e-6f, outPow_ = 1e-6f, autoDb_ = 0; int look_ = 0;
    BiquadCoeffs hpC_; BiquadState hp_[2]; DelayLine dly_[2];
};

// ---------------- Gate / Expander ----------------
struct GateParams {
    float thresholdDb = -50.f, ratio = 4.f, attackMs = 1.f, holdMs = 50.f, releaseMs = 150.f, rangeDb = -60.f, hysteresisDb = 3.f;
    template <class V> void visit(V& v) {
        v.field("thr", thresholdDb); v.field("ratio", ratio); v.field("att", attackMs); v.field("hold", holdMs);
        v.field("rel", releaseMs); v.field("range", rangeDb); v.field("hyst", hysteresisDb);
    }
};
class Gate {
public:
    void prepare(double fs) { fs_ = fs; reset(); setParams(p_); }
    void reset() { env_ = 0; gr_ = 0; hold_ = 0; open_ = false; grMeter = 0; }
    void setParams(const GateParams& p) {
        p_ = p; atk_ = timeCoef(p.attackMs, fs_); rel_ = timeCoef(p.releaseMs, fs_);
        holdN_ = (int)(p.holdMs * 0.001 * fs_); envA_ = timeCoef(0.5f, fs_); envR_ = timeCoef(20.f, fs_);
    }
    void process(float* const* x, int nCh, int n) {
        float minGr = 0;
        for (int i = 0; i < n; ++i) {
            float d = 0; for (int c = 0; c < nCh; ++c) d = std::max(d, std::fabs(x[c][i]));
            env_ = d > env_ ? d + envA_ * (env_ - d) : d + envR_ * (env_ - d);
            const float lDb = 20.f * std::log10(env_ + 1e-9f);
            if (lDb >= p_.thresholdDb) { open_ = true; hold_ = holdN_; }
            else if (lDb < p_.thresholdDb - p_.hysteresisDb) { if (hold_ > 0) --hold_; else open_ = false; }
            else if (hold_ > 0) --hold_;
            float target = 0;
            if (!open_) target = std::max(p_.rangeDb, -(p_.thresholdDb - lDb) * (std::max(p_.ratio, 1.f) - 1.f));
            gr_ = target < gr_ ? target + atk_ * 0.f + (gr_ - target) * rel_ : target + (gr_ - target) * atk_;
            const float g = std::exp(gr_ * 0.115129255f);
            for (int c = 0; c < nCh; ++c) x[c][i] *= g;
            minGr = std::min(minGr, gr_);
        }
        grMeter = minGr;
    }
    float grMeter = 0;
private:
    double fs_ = 48000; GateParams p_; float env_ = 0, gr_ = 0, atk_ = 0, rel_ = 0, envA_ = 0, envR_ = 0; int hold_ = 0, holdN_ = 0; bool open_ = false;
};

// ---------------- De-Esser ----------------
enum class SibType { S, SH, CH, Custom };
inline float sibDefaultFreq(SibType t) { return t == SibType::S ? 7500.f : t == SibType::SH ? 5000.f : t == SibType::CH ? 6200.f : 7000.f; }
struct DeEssParams {
    SibType type = SibType::S; float freqHz = 7500.f, thresholdDb = -30.f, rangeDb = 10.f, attackMs = 0.5f, releaseMs = 40.f, q = 1.5f;
    template <class V> void visit(V& v) {
        v.enumField("type", type); v.field("f", freqHz); v.field("thr", thresholdDb); v.field("range", rangeDb);
        v.field("att", attackMs); v.field("rel", releaseMs); v.field("q", q);
    }
};
class DeEsser {
public:
    void prepare(double fs) { fs_ = fs; reset(); setParams(p_); }
    void reset() { env_ = 0; grMeter = 0; det_ = {}; sp_[0].reset(); sp_[1].reset(); }
    void setParams(const DeEssParams& p) {
        p_ = p; c_ = BiquadCoeffs::make(FilterType::BandPass, fs_, p.freqHz, 0, p.q);
        a_ = timeCoef(p.attackMs, fs_); r_ = timeCoef(p.releaseMs, fs_);
    }
    void process(float* const* x, int nCh, int n) {
        float minGr = 0;
        for (int i = 0; i < n; ++i) {
            float m = 0; for (int c = 0; c < nCh; ++c) m += x[c][i]; m /= (float)nCh;
            const float d = std::fabs(det_.process(c_, m));
            env_ = d > env_ ? d + a_ * (env_ - d) : d + r_ * (env_ - d);
            const float over = 20.f * std::log10(env_ * 1.5708f + 1e-9f) - p_.thresholdDb;
            const float gr = over > 0 ? -std::min(p_.rangeDb, over) : 0.f;
            const float g = std::exp(gr * 0.115129255f); minGr = std::min(minGr, gr);
            for (int c = 0; c < nCh; ++c) { const float band = sp_[c].process(c_, x[c][i]); x[c][i] -= band * (1.f - g); }
        }
        grMeter = minGr;
    }
    float grMeter = 0;
private:
    double fs_ = 48000; DeEssParams p_; BiquadCoeffs c_; BiquadState det_, sp_[2]; float env_ = 0, a_ = 0, r_ = 0;
};

// ---------------- Transient Designer ----------------
struct TransientParams {
    float attack = 0.f, sustain = 0.f, punch = 0.f, body = 0.f; // each -1..+1 (max +/-12 dB)
    template <class V> void visit(V& v) { v.field("attack", attack); v.field("sustain", sustain); v.field("punch", punch); v.field("body", body); }
};
class TransientDesigner {
public:
    void prepare(double fs) { fs_ = fs; fa_ = timeCoef(1.f, fs); fr_ = timeCoef(30.f, fs); sa_ = timeCoef(25.f, fs); sr_ = timeCoef(200.f, fs);
        gs_ = timeCoef(1.f, fs); lp_.set(FilterType::LowPass, fs, 250.0, 0, 0.7071); reset(); }
    void reset() { f_ = s_ = 1e-6f; gl_ = gh_ = 0; lp_.reset(); }
    void setParams(const TransientParams& p) { p_ = p; }
    void process(float* const* x, int nCh, int n) {
        if (p_.attack == 0 && p_.sustain == 0 && p_.punch == 0 && p_.body == 0) return;
        for (int i = 0; i < n; ++i) {
            float d = 0; for (int c = 0; c < nCh; ++c) d = std::max(d, std::fabs(x[c][i]));
            f_ = d > f_ ? d + fa_ * (f_ - d) : d + fr_ * (f_ - d);
            s_ = d > s_ ? d + sa_ * (s_ - d) : d + sr_ * (s_ - d);
            const float diff = 20.f * std::log10((f_ + 1e-6f) / (s_ + 1e-6f));
            const float tr = clampf(diff, 0.f, 15.f) / 15.f, su = clampf(-diff, 0.f, 15.f) / 15.f;
            const float gH = 12.f * (p_.attack * tr + p_.sustain * su);
            const float gL = gH + 12.f * (p_.punch * tr + p_.body * su);
            gh_ = gH + gs_ * (gh_ - gH); gl_ = gL + gs_ * (gl_ - gL);
            const float lH = dbToLin(gh_), lL = dbToLin(gl_);
            for (int c = 0; c < nCh; ++c) { const float lo = lp_.process(c, x[c][i]); x[c][i] = lo * lL + (x[c][i] - lo) * lH; }
        }
    }
private:
    double fs_ = 48000; TransientParams p_; float fa_ = 0, fr_ = 0, sa_ = 0, sr_ = 0, gs_ = 0, f_ = 1e-6f, s_ = 1e-6f, gl_ = 0, gh_ = 0; Biquad2ch lp_;
};

// ---------------- Multiband compressor (4 bands, LR4 crossovers, phase-coherent) ----------------
struct MbParams {
    std::array<float, 3> xover{120.f, 1000.f, 5000.f};
    std::array<CompParams, 4> band; std::array<bool, 4> bandOn{true, true, true, true}; float outputDb = 0.f;
    template <class V> void visit(V& v) {
        v.field("x0", xover[0]); v.field("x1", xover[1]); v.field("x2", xover[2]); v.field("out", outputDb);
        for (size_t i = 0; i < 4; ++i) { v.push("m" + std::to_string(i)); v.field("on", bandOn[i]); band[i].visit(v); v.pop(); }
    }
};
class MultibandCompressor {
public:
    void prepare(double fs) { fs_ = fs; for (auto& c : comp_) c.prepare(fs); setParams(p_); reset(); }
    void reset() { for (int c = 0; c < 2; ++c) { x2_[c].reset(); x1_[c].reset(); x3_[c].reset(); for (auto& a : ap_[c]) a.reset(); } for (auto& c : comp_) c.reset(); }
    void setParams(const MbParams& p) {
        p_ = p; for (int c = 0; c < 2; ++c) {
            x1_[c].set(fs_, p.xover[0]); x2_[c].set(fs_, p.xover[1]); x3_[c].set(fs_, p.xover[2]);
            ap_[c][0].set(fs_, p.xover[2]); ap_[c][1].set(fs_, p.xover[2]); ap_[c][2].set(fs_, p.xover[0]); ap_[c][3].set(fs_, p.xover[0]);
        }
        for (int b = 0; b < 4; ++b) comp_[(size_t)b].setParams(p.band[(size_t)b]);
        outG_ = dbToLin(p.outputDb);
    }
    void process(float* const* x, int nCh, int n) {
        for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) {
            float lo2, hi2, b0, b1, b2, b3, l, h;
            x2_[c].split(0, x[c][i], lo2, hi2);   // NOTE: LR4 instance holds one state set; channel handled via separate instances
            x1_[c].split(0, lo2, b0, b1); x3_[c].split(0, hi2, b2, b3);
            ap_[c][0].split(0, b0, l, h); b0 = l + h; ap_[c][1].split(0, b1, l, h); b1 = l + h;
            ap_[c][2].split(0, b2, l, h); b2 = l + h; ap_[c][3].split(0, b3, l, h); b3 = l + h;
            buf_[0][(size_t)c][(size_t)i] = b0; buf_[1][(size_t)c][(size_t)i] = b1; buf_[2][(size_t)c][(size_t)i] = b2; buf_[3][(size_t)c][(size_t)i] = b3;
        }
        for (int c = 0; c < nCh; ++c) std::fill(x[c], x[c] + n, 0.f);
        for (int b = 0; b < 4; ++b) {
            float* ptr[2] = {buf_[b][0].data(), buf_[b][1].data()};
            if (p_.bandOn[(size_t)b]) comp_[(size_t)b].process(ptr, nCh, n);
            for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) x[c][i] += ptr[c][i];
        }
        for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) x[c][i] *= outG_;
    }
    float grDb(int band) const { return comp_[(size_t)band].grMeter; }
    MultibandCompressor() { for (auto& b : buf_) for (auto& c : b) c.assign(kMaxBlock, 0.f); }
private:
    double fs_ = 48000; MbParams p_; std::array<Compressor, 4> comp_; LR4 x1_[2], x2_[2], x3_[2], ap_[2][4]; float outG_ = 1.f;
    std::array<std::array<std::vector<float>, 2>, 4> buf_;
};

} // namespace slp
