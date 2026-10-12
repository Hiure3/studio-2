// Studio Live Pro - Delay (6 types, ducking) and Reverb (FDN, 7 types, ducking)
#pragma once
#include "Biquad.h"

namespace slp {

enum class DelayType { Digital, Analog, Tape, Stereo, PingPong, Slapback };
struct DelayParams {
    DelayType type = DelayType::Digital; float timeMs = 350.f, timeRightMs = 450.f, feedback = 0.35f, mix = 0.2f, filterHz = 6000.f, width = 1.f, duck = 0.f;
    template <class V> void visit(V& v) {
        v.enumField("type", type); v.field("time", timeMs); v.field("timeR", timeRightMs); v.field("fb", feedback);
        v.field("mix", mix); v.field("filt", filterHz); v.field("width", width); v.field("duck", duck);
    }
};
class DelayFx {
public:
    void prepare(double fs) { fs_ = fs; for (auto& d : line_) d.prepare((int)(fs * 2.2)); reset(); setParams(p_); }
    void reset() { for (auto& d : line_) d.clear(); for (int c = 0; c < 2; ++c) { lp_[c] = 0; curT_[c] = tgtT_[c]; } env_ = 0; ph_ = 0; duckG_ = 1; }
    void setParams(const DelayParams& p) {
        p_ = p; float tl = p.timeMs, tr = p.timeRightMs;
        switch (p.type) { case DelayType::Slapback: tl = std::min(std::max(tl, 60.f), 140.f); tr = tl; break;
            case DelayType::Stereo: break; case DelayType::PingPong: tr = tl; break; default: tr = tl; break; }
        tgtT_[0] = tl * 0.001f * (float)fs_; tgtT_[1] = tr * 0.001f * (float)fs_;
        const float maxS = (float)(fs_ * 2.0); for (auto& t : tgtT_) t = std::min(std::max(t, 8.f), maxS);
        float f = p.filterHz; if (p.type == DelayType::Analog) f = std::min(f, 3500.f); if (p.type == DelayType::Tape) f = std::min(f, 5000.f);
        lpA_ = 1.f - std::exp(-2.f * (float)kPi * std::min(f, (float)(fs_ * 0.45)) / (float)fs_);
        fb_ = std::min(p.feedback, 0.95f); slew_ = 1.f - std::exp(-1.f / (0.05f * (float)fs_)); duckA_ = timeCoef(5.f, fs_); duckR_ = timeCoef(250.f, fs_);
    }
    void process(float* const* x, int nCh, int n) {
        const bool stereo = nCh == 2;
        for (int i = 0; i < n; ++i) {
            const float inL = x[0][i], inR = stereo ? x[1][i] : x[0][i];
            const float inLvl = std::max(std::fabs(inL), std::fabs(inR));
            env_ = inLvl > env_ ? inLvl + duckA_ * (env_ - inLvl) : inLvl + duckR_ * (env_ - inLvl);
            const float duckT = 1.f - p_.duck * clampf(env_ * 6.f, 0.f, 1.f); duckG_ += 0.002f * (duckT - duckG_);
            ph_ += 2.f * (float)kPi * 0.8f / (float)fs_; if (ph_ > 6.2831853f) ph_ -= 6.2831853f;
            float wet[2];
            for (int c = 0; c < 2; ++c) {
                curT_[c] += slew_ * (tgtT_[c] - curT_[c]);
                float t = curT_[c];
                if (p_.type == DelayType::Tape) t += 0.0006f * (float)fs_ * 0.3f * std::sin(ph_ * (c ? 1.1f : 1.f)) + 0.00004f * (float)fs_ * std::sin(ph_ * 11.f);
                wet[c] = line_[c].read(std::max(t, 2.f));
            }
            for (int c = 0; c < 2; ++c) {
                float fbSig = wet[c]; if (p_.type == DelayType::PingPong) fbSig = wet[1 - c];
                lp_[c] += lpA_ * (fbSig - lp_[c]); float f = lp_[c];
                if (p_.type == DelayType::Analog || p_.type == DelayType::Tape) f = std::tanh(f * 1.3f) / 1.3f;
                float in = (c == 0) ? inL : inR;
                if (p_.type == DelayType::PingPong) in = (c == 0) ? 0.5f * (inL + inR) : 0.f;   // enters left, bounces right
                line_[c].push(in + f * fb_);
            }
            const float m = 0.5f * (wet[0] + wet[1]), s = 0.5f * (wet[0] - wet[1]) * p_.width;
            const float oL = (m + s) * duckG_ * p_.mix, oR = (m - s) * duckG_ * p_.mix;
            x[0][i] = inL * (1.f - p_.mix * 0.0f) + oL; if (stereo) x[1][i] = inR + oR;
        }
    }
private:
    double fs_ = 48000; DelayParams p_; DelayLine line_[2]; float lp_[2]{}, lpA_ = 1, fb_ = 0, tgtT_[2]{1000, 1000}, curT_[2]{1000, 1000}, slew_ = 0.001f, env_ = 0, ph_ = 0, duckG_ = 1, duckA_ = 0, duckR_ = 0;
};

enum class ReverbType { Room, Studio, Hall, Plate, Chamber, Cathedral, Live };
struct ReverbParams {
    ReverbType type = ReverbType::Plate; float preDelayMs = 15.f, decaySec = 1.6f, diffusion = 0.7f, damping = 0.5f, early = 0.4f, width = 1.f, lowCutHz = 150.f, highCutHz = 9000.f, mix = 0.2f, size = 1.f, duck = 0.f;
    template <class V> void visit(V& v) {
        v.enumField("type", type); v.field("pre", preDelayMs); v.field("decay", decaySec); v.field("diff", diffusion); v.field("damp", damping);
        v.field("early", early); v.field("width", width); v.field("lc", lowCutHz); v.field("hc", highCutHz); v.field("mix", mix); v.field("size", size); v.field("duck", duck);
    }
    static ReverbParams preset(ReverbType t) {
        ReverbParams p; p.type = t;
        switch (t) {
        case ReverbType::Room: p.size = 0.55f; p.decaySec = 0.6f; p.preDelayMs = 4; p.damping = 0.6f; p.early = 0.6f; break;
        case ReverbType::Studio: p.size = 0.75f; p.decaySec = 0.9f; p.preDelayMs = 8; p.damping = 0.5f; p.early = 0.5f; break;
        case ReverbType::Hall: p.size = 1.5f; p.decaySec = 2.4f; p.preDelayMs = 25; p.damping = 0.45f; p.early = 0.3f; break;
        case ReverbType::Plate: p.size = 1.0f; p.decaySec = 1.6f; p.preDelayMs = 12; p.damping = 0.35f; p.early = 0.1f; p.diffusion = 0.9f; break;
        case ReverbType::Chamber: p.size = 1.1f; p.decaySec = 1.3f; p.preDelayMs = 10; p.damping = 0.55f; p.early = 0.45f; break;
        case ReverbType::Cathedral: p.size = 2.2f; p.decaySec = 5.5f; p.preDelayMs = 40; p.damping = 0.4f; p.early = 0.2f; break;
        case ReverbType::Live: p.size = 1.2f; p.decaySec = 1.8f; p.preDelayMs = 20; p.damping = 0.6f; p.early = 0.5f; p.highCutHz = 7000; break;
        }
        return p;
    }
};
class Reverb {
public:
    static constexpr int kN = 8;
    void prepare(double fs) {
        fs_ = fs; for (auto& d : line_) d.prepare(32768); pre_[0].prepare((int)(fs * 0.25)); pre_[1].prepare((int)(fs * 0.25));
        for (auto& a : ap_) for (auto& d : a) d.prepare(2048); er_[0].prepare((int)(fs * 0.12)); er_[1].prepare((int)(fs * 0.12)); reset(); setParams(p_);
    }
    void reset() { for (auto& d : line_) d.clear(); for (auto& a : ap_) for (auto& d : a) d.clear(); for (auto& d : pre_) d.clear(); for (auto& d : er_) d.clear();
        for (auto& v : damp_) v = 0; hp_[0].reset(); hp_[1].reset(); lpo_[0].reset(); lpo_[1].reset(); env_ = 0; duckG_ = 1; }
    void setParams(const ReverbParams& p) {
        p_ = p; static const int base[kN] = {1117, 1327, 1559, 1787, 2053, 2311, 2557, 2833};
        const float sc = (float)(fs_ / 44100.0) * std::min(std::max(p.size, 0.3f), 2.4f);
        for (int i = 0; i < kN; ++i) {
            len_[i] = std::min(std::max((int)(base[i] * sc), 16), 32000);
            fb_[i] = std::pow(10.f, -3.f * len_[i] / ((float)fs_ * std::max(p.decaySec, 0.1f)));
        }
        dampA_ = 1.f - clampf(p.damping, 0.f, 0.98f) * 0.9f; preS_ = p.preDelayMs * 0.001f * (float)fs_;
        apG_ = 0.35f + 0.3f * clampf(p.diffusion, 0.f, 1.f);
        hp_[0].set(FilterType::HighPass, fs_, std::max(p.lowCutHz, 20.f), 0, 0.707); hp_[1].set(FilterType::HighPass, fs_, std::max(p.lowCutHz, 20.f), 0, 0.707);
        lpo_[0].set(FilterType::LowPass, fs_, std::min(p.highCutHz, (float)(fs_ * 0.45)), 0, 0.707); lpo_[1].set(FilterType::LowPass, fs_, std::min(p.highCutHz, (float)(fs_ * 0.45)), 0, 0.707);
        static const float et[6] = {0.0113f, 0.0197f, 0.0271f, 0.0359f, 0.0443f, 0.0531f}; static const float eg[6] = {0.8f, 0.65f, 0.55f, 0.45f, 0.35f, 0.28f};
        for (int i = 0; i < 6; ++i) { erT_[i] = et[i] * (float)fs_ * std::min(std::max(p.size, 0.5f), 1.6f); erG_[i] = eg[i]; }
        duckA_ = timeCoef(8.f, fs_); duckR_ = timeCoef(300.f, fs_);
    }
    // In-place stereo (or mono duplicated) processing; adds wet to the dry signal.
    void process(float* const* x, int nCh, int n) {
        static const int apLen[4] = {142, 107, 379, 277}; const float fsr = (float)(fs_ / 44100.0);
        for (int i = 0; i < n; ++i) {
            const float inL = x[0][i], inR = nCh == 2 ? x[1][i] : x[0][i];
            const float lvl = std::max(std::fabs(inL), std::fabs(inR)); env_ = lvl > env_ ? lvl + duckA_ * (env_ - lvl) : lvl + duckR_ * (env_ - lvl);
            const float dT = 1.f - p_.duck * clampf(env_ * 6.f, 0.f, 1.f); duckG_ += 0.003f * (dT - duckG_);
            pre_[0].push(inL); pre_[1].push(inR);
            float a = pre_[0].read(preS_), b = pre_[1].read(preS_); const float mono = 0.5f * (a + b);
            er_[0].push(a); er_[1].push(b);
            float erL = 0, erR = 0; for (int k = 0; k < 6; ++k) { erL += erG_[k] * er_[k & 1].read(erT_[k]); erR += erG_[k] * er_[(k + 1) & 1].read(erT_[k] * 1.07f); }
            erL *= 0.25f * p_.early; erR *= 0.25f * p_.early;
            float s = mono;                       // series Schroeder allpass diffusers
            for (int k = 0; k < 4; ++k) {
                const int L = std::max(2, (int)(apLen[k] * fsr)); const float d = ap_[0][(size_t)k].readInt(L - 1);
                const float v = s - apG_ * d; ap_[0][(size_t)k].push(v); s = d + apG_ * v;
            }
            float y[kN], t[kN];
            for (int k = 0; k < kN; ++k) y[k] = line_[k].readInt(len_[k] - 1);
            // 8x8 Hadamard feedback matrix (orthogonal, scaled)
            float h[kN] = {y[0] + y[1], y[0] - y[1], y[2] + y[3], y[2] - y[3], y[4] + y[5], y[4] - y[5], y[6] + y[7], y[6] - y[7]};
            t[0] = h[0] + h[2]; t[1] = h[1] + h[3]; t[2] = h[0] - h[2]; t[3] = h[1] - h[3]; t[4] = h[4] + h[6]; t[5] = h[5] + h[7]; t[6] = h[4] - h[6]; t[7] = h[5] - h[7];
            float m[kN]; m[0] = t[0] + t[4]; m[1] = t[1] + t[5]; m[2] = t[2] + t[6]; m[3] = t[3] + t[7]; m[4] = t[0] - t[4]; m[5] = t[1] - t[5]; m[6] = t[2] - t[6]; m[7] = t[3] - t[7];
            for (int k = 0; k < kN; ++k) {
                float v = m[k] * 0.35355339f * fb_[k];
                damp_[k] += dampA_ * (v - damp_[k]); v = damp_[k];
                line_[k].push(s * 0.5f + v);
            }
            float wl = 0, wr = 0; for (int k = 0; k < kN; ++k) { if (k & 1) wr += (k & 2 ? -y[k] : y[k]); else wl += (k & 2 ? -y[k] : y[k]); }
            wl = wl * 0.5f + erL; wr = wr * 0.5f + erR;
            const float mid = 0.5f * (wl + wr), sid = 0.5f * (wl - wr) * p_.width;
            wl = lpo_[0].process(0, hp_[0].process(0, mid + sid)); wr = lpo_[1].process(1, hp_[1].process(1, mid - sid));
            x[0][i] = inL + wl * p_.mix * duckG_; if (nCh == 2) x[1][i] = inR + wr * p_.mix * duckG_;
        }
    }
private:
    double fs_ = 48000; ReverbParams p_; DelayLine line_[kN], ap_[1][4], pre_[2], er_[2]; Biquad2ch hp_[2], lpo_[2];
    int len_[kN]{}; float fb_[kN]{}, damp_[kN]{}, dampA_ = 0.5f, preS_ = 0, apG_ = 0.5f, erT_[6]{}, erG_[6]{}, env_ = 0, duckG_ = 1, duckA_ = 0, duckR_ = 0;
};

} // namespace slp
