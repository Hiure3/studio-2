// Studio Live Pro - realtime mixer engine: channels -> subgroups/aux/FX -> master chain -> outputs
#pragma once
#include <chrono>
#include <memory>
#include <cstdio>
#include "ChannelStrip.h"
#include "../Metering/Meters.h"

namespace slp {

struct EngineMeters {
    std::atomic<float> chPeakDb[kMaxChannels], chRmsDb[kMaxChannels], chCompGr[kMaxChannels], chGateGr[kMaxChannels];
    std::atomic<float> masterPeakDb[2], masterRmsDb[2], lufsM{-120.f}, lufsS{-120.f}, lufsI{-120.f}, lra{0.f}, truePeakDb{-120.f}, crest{0.f};
    std::atomic<float> correlation{0.f}, width{0.f}, monoLossDb{0.f}, limGr{0.f}, glueGr{0.f};
    std::atomic<float> cpuLoad{0.f}, dspLoad{0.f}; std::atomic<int> safeLevel{0}; std::atomic<long> overloads{0};
    EngineMeters() { for (int i = 0; i < kMaxChannels; ++i) { chPeakDb[i] = chRmsDb[i] = -120.f; chCompGr[i] = chGateGr[i] = 0.f; } for (int i = 0; i < 2; ++i) masterPeakDb[i] = masterRmsDb[i] = -120.f; }
};

class MixerEngine {
public:
    MixerEngine() { for (int i = 0; i < kMaxChannels; ++i) { st_[(size_t)i].inputIndex = i; std::snprintf(st_[(size_t)i].name, sizeof st_[(size_t)i].name, "CH %d", i + 1); } }
    void prepare(double fs, int maxBlock, int numChannels) {
        fs_ = fs; maxBlock_ = std::min(maxBlock, kMaxBlock); nCh_ = std::min(numChannels, kMaxChannels);
        strips_.reset(new ChannelStrip[kMaxChannels]); for (int i = 0; i < nCh_; ++i) { strips_[i].prepare(fs, maxBlock_); strips_[i].setState(st_[(size_t)i]); }
        for (auto& b : buf_) for (auto& c : b) c.assign((size_t)maxBlock_, 0.f);
        for (auto& b : sub_) for (auto& c : b) c.assign((size_t)maxBlock_, 0.f);
        for (auto& b : aux_) for (auto& c : b) c.assign((size_t)maxBlock_, 0.f);
        for (auto& b : fx_) for (auto& c : b) c.assign((size_t)maxBlock_, 0.f);
        for (auto& c : mix_) c.assign((size_t)maxBlock_, 0.f); for (auto& c : tmp_) c.assign((size_t)maxBlock_, 0.f);
        for (auto& c : dry_) c.assign((size_t)maxBlock_, 0.f); gStep_.assign((size_t)maxBlock_, 0.f);
        for (auto& l : chLevel_) l.prepare(fs);
        for (int i = 0; i < kMaxChannels; ++i) { prevMute_[i] = 1.f; prevFader_[i] = 0.f; }
        mEq_.prepare(fs); mDyn_.prepare(fs); mGlue_.prepare(fs); mMb_.prepare(fs); mSat_.prepare(fs, maxBlock_); mClip_.prepare(fs, maxBlock_); mLim_.prepare(fs);
        rev_.prepare(fs); dly_.prepare(fs); lufs_.prepare(fs, 2); lvl_[0].prepare(fs); lvl_[1].prepare(fs); corr_.prepare(fs); spec_.prepare(fs);
        bassLr_[0].reset(); bassLr_[1].reset(); applyMaster(ms_);
    }
    // ---- UI thread API (lock-free) ----
    bool pushChannel(int ch, const ChannelState& s) { if (ch < 0 || ch >= kMaxChannels) return false; ChCmd c; c.idx = ch; c.state = s; return chQ_.push(c); }
    bool pushMaster(const MasterState& m) { return mQ_.push(m); }
    void setEmergencyBypass(bool b) { bypass_.store(b); }
    bool emergencyBypass() const { return bypass_.load(); }
    void setSafeMode(bool enabled) { safeEnabled_.store(enabled); if (!enabled) meters.safeLevel = 0; }
    void resetSafe() { meters.safeLevel = 0; bypass_.store(false); calm_ = 0; }
    EngineMeters meters; SpectrumAnalyzer& spectrum() { return spec_; }
    int numChannels() const { return nCh_; } double sampleRate() const { return fs_; }
    int masterLatencySamples() const { return (ms_.clipOn ? mClip_.latencySamples() : 0) + (ms_.satOn ? mSat_.latencySamples() : 0) + (ms_.limOn ? mLim_.latencySamples() : 0); }
    void resetLoudness() { lufs_.reset(); }
    void requestNoiseLearn(int ch, bool start) { if (ch >= 0 && ch < nCh_) strips_[ch].requestNoiseLearn(start); }
    bool noiseProfileReady(int ch) const { return ch >= 0 && ch < nCh_ && strips_[ch].noiseProfileReady(); }
    ChannelStrip& strip(int i) { return strips_[i]; }   // for non-realtime tools (offline render, noise learn)

    // in: nIn input channels; out: nOut channels (see kNumOutputs layout); any n.
    void process(const float* const* in, int nIn, float* const* out, int nOut, int n) {
        ScopedNoDenormals nd;
        const float* ip[kMaxChannels]; float* op[kNumOutputs];
        for (int off = 0; off < n; off += maxBlock_) {
            const int len = std::min(maxBlock_, n - off);
            for (int i = 0; i < std::min(nIn, kMaxChannels); ++i) ip[i] = in[i] + off;
            for (int i = 0; i < std::min(nOut, kNumOutputs); ++i) op[i] = out[i] + off;
            chunk(ip, std::min(nIn, kMaxChannels), op, std::min(nOut, kNumOutputs), len);
        }
    }
private:
    static constexpr int kMaxCmdsPerBlock = 4;   // spread scene recalls over several blocks to avoid CPU spikes on the audio thread
    struct ChCmd { int idx = 0; ChannelState state; };
    static inline float ramped(float a, float b, int i, int n) { return a + (b - a) * ((float)(i + 1) / (float)n); }

    void applyMaster(const MasterState& m) {
        ms_ = m; mEq_.setParams(m.eq); mDyn_.setParams(m.dyn); mGlue_.setParams(m.glue); mMb_.setParams(m.mb); mSat_.setParams(m.sat); mClip_.setParams(m.clip); mLim_.setParams(m.lim);
        auto rp = m.reverb; rp.mix = 1.f; rev_.setParams(rp); auto dp = m.delay; dp.mix = 1.f; dly_.setParams(dp);
        if (m.stereo.bassMonoHz > 20.f) { bassLr_[0].set(fs_, m.stereo.bassMonoHz); bassLr_[1].set(fs_, m.stereo.bassMonoHz); }
    }

    void chunk(const float* const* in, int nIn, float* const* out, int nOut, int n) {
        const auto t0 = std::chrono::steady_clock::now();
        ChCmd c; int applied = 0; while (applied < kMaxCmdsPerBlock && chQ_.pop(c)) { ++applied; if (c.idx < nCh_) { st_[(size_t)c.idx] = c.state; strips_[c.idx].setState(c.state); } else st_[(size_t)c.idx] = c.state; }
        MasterState m; bool newM = false; while (mQ_.pop(m)) newM = true; if (newM) applyMaster(m);
        const int safe = bypass_.load() ? 2 : meters.safeLevel.load();
        for (auto& b : sub_) for (auto& ch : b) std::fill(ch.begin(), ch.begin() + n, 0.f);
        for (auto& b : aux_) for (auto& ch : b) std::fill(ch.begin(), ch.begin() + n, 0.f);
        for (auto& b : fx_) for (auto& ch : b) std::fill(ch.begin(), ch.begin() + n, 0.f);
        for (auto& ch : mix_) std::fill(ch.begin(), ch.begin() + n, 0.f);
        bool anySolo = false; for (int i = 0; i < nCh_; ++i) if (st_[(size_t)i].active && st_[(size_t)i].solo) anySolo = true;

        for (int i = 0; i < nCh_; ++i) {
            const ChannelState& s = st_[(size_t)i]; if (!s.active) { meterCh(i, nullptr, nullptr, 0); continue; }
            const int nC = s.stereo ? 2 : 1;
            for (int k = 0; k < nC; ++k) { const int idx = s.inputIndex + k; if (idx >= 0 && idx < nIn) std::copy(in[idx], in[idx] + n, buf_[(size_t)i % 1][(size_t)k].begin()); else std::fill(buf_[0][(size_t)k].begin(), buf_[0][(size_t)k].begin() + n, 0.f); }
            float* p[2] = {buf_[0][0].data(), buf_[0][1].data()};
            strips_[i].process(p, nC, n, safe);
            // pan / balance / width -> L,R
            float* L = tmp_[0].data(); float* R = tmp_[1].data();
            if (nC == 1) { const float a = (s.pan + 1.f) * 0.78539816f; const float gl = std::cos(a), gr = std::sin(a); for (int k = 0; k < n; ++k) { L[k] = p[0][k] * gl; R[k] = p[0][k] * gr; } }
            else {
                const float gl = std::min(1.f, 1.f - s.pan), gr = std::min(1.f, 1.f + s.pan);
                for (int k = 0; k < n; ++k) { const float mid = 0.5f * (p[0][k] + p[1][k]), sd = 0.5f * (p[0][k] - p[1][k]) * s.width; L[k] = (mid + sd) * gl; R[k] = (mid - sd) * gr; }
            }
            const bool audible = !s.mute && (!anySolo || s.solo);
            const float gM = audible ? 1.f : 0.f, gF = audible ? dbToLin(s.faderDb) : 0.f; const float pM = prevMute_[i], pF = prevFader_[i];
            prevMute_[i] = gM; prevFader_[i] = gF;
            float* oL = mix_[0].data(); float* oR = mix_[1].data();
            if (s.busAssign >= 0 && s.busAssign < kNumSubs) { oL = sub_[(size_t)s.busAssign][0].data(); oR = sub_[(size_t)s.busAssign][1].data(); }
            // post-fader signal into destination; pre-fader aux uses mute gate only
            float* pL = tmp_[2].data(); float* pR = tmp_[3].data();
            for (int k = 0; k < n; ++k) { const float g = ramped(pF, gF, k, n); pL[k] = L[k] * g; pR[k] = R[k] * g; oL[k] += pL[k]; oR[k] += pR[k]; }
            for (int a = 0; a < kNumAux; ++a) if (s.auxDb[a] > -99.f) {
                const float sg = dbToLin(s.auxDb[a]); float* aL = aux_[(size_t)a][0].data(); float* aR = aux_[(size_t)a][1].data();
                if (s.auxPre) for (int k = 0; k < n; ++k) { const float g = ramped(pM, gM, k, n) * sg; aL[k] += L[k] * g; aR[k] += R[k] * g; }
                else for (int k = 0; k < n; ++k) { aL[k] += pL[k] * sg; aR[k] += pR[k] * sg; }
            }
            for (int f = 0; f < kNumFx; ++f) if (s.fxDb[f] > -99.f) { const float sg = dbToLin(s.fxDb[f]) * 0.5f; float* fl = fx_[(size_t)f][0].data(); float* fr = fx_[(size_t)f][1].data(); for (int k = 0; k < n; ++k) { fl[k] += pL[k] * sg; fr[k] += pR[k] * sg; } }
            meterCh(i, pL, pR, n);
            chGr_[i] = strips_[i].compGrDb(); meters.chCompGr[i] = strips_[i].compGrDb(); meters.chGateGr[i] = strips_[i].gateGrDb();
        }
        // subgroups -> master
        for (int b = 0; b < kNumSubs; ++b) { if (ms_.subMute[b]) continue; const float g = dbToLin(ms_.subFaderDb[b]); for (int k = 0; k < n; ++k) { mix_[0][(size_t)k] += sub_[(size_t)b][0][(size_t)k] * g; mix_[1][(size_t)k] += sub_[(size_t)b][1][(size_t)k] * g; } }
        // FX returns (reverb / delay) - skipped in safe mode
        if (safe == 0) {
            for (int f = 0; f < kNumFx; ++f) {
                if (!ms_.fxOn[f]) continue; float* w[2] = {fx_[(size_t)f][0].data(), fx_[(size_t)f][1].data()};
                std::copy(w[0], w[0] + n, dry_[0].begin()); std::copy(w[1], w[1] + n, dry_[1].begin());
                if (f == 0) rev_.process(w, 2, n); else dly_.process(w, 2, n);
                const float g = dbToLin(ms_.fxReturnDb[f]);
                for (int k = 0; k < n; ++k) { mix_[0][(size_t)k] += (w[0][k] - dry_[0][(size_t)k]) * g; mix_[1][(size_t)k] += (w[1][k] - dry_[1][(size_t)k]) * g; }
            }
        }
        // master chain
        float* M[2] = {mix_[0].data(), mix_[1].data()};
        if (safe < 2) {
            if (ms_.eqOn) mEq_.process(M, 2, n);
            if (safe == 0 && ms_.dynEqOn) mDyn_.process(M, 2, n);
            if (ms_.glueOn) { mGlue_.process(M, 2, n); meters.glueGr = mGlue_.grMeter; }
            if (safe == 0 && ms_.mbOn) mMb_.process(M, 2, n);
            if (safe == 0 && ms_.satOn) mSat_.process(M, 2, n);
            if (ms_.stereoOn) stereoStage(M, n);
            if (safe == 0 && ms_.clipOn) mClip_.process(M, 2, n);
        }
        const float mg = ms_.mute ? 0.f : dbToLin(ms_.faderDb + ms_.matchTrimDb); const float pmg = prevMaster_; prevMaster_ = mg;
        for (int k = 0; k < n; ++k) { const float g = ramped(pmg, mg, k, n); M[0][k] *= g; M[1][k] *= g; }
        if (ms_.limOn || safe >= 2) { if (ms_.limOn) { mLim_.process(M, 2, n); meters.limGr = mLim_.grMeter; } else for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < n; ++k) M[ch][k] = clampf(M[ch][k], -0.89f, 0.89f); }
        for (int k = 0; k < n; ++k) for (int ch = 0; ch < 2; ++ch) if (!std::isfinite(M[ch][k])) M[ch][k] = 0.f;   // never emit NaN/Inf
        // meters
        lvl_[0].process(M[0], n); lvl_[1].process(M[1], n); corr_.process(M[0], M[1], n); const float* lp[2] = {M[0], M[1]}; lufs_.process(lp, n);
        spec_.push(M[0], n);
        meters.masterPeakDb[0] = lvl_[0].peakDb(); meters.masterPeakDb[1] = lvl_[1].peakDb(); meters.masterRmsDb[0] = lvl_[0].rmsDb(); meters.masterRmsDb[1] = lvl_[1].rmsDb();
        meters.lufsM = lufs_.momentary(); meters.lufsS = lufs_.shortTerm(); meters.lufsI = lufs_.integrated(); meters.truePeakDb = lufs_.truePeakDb(); meters.lra = lufs_.loudnessRange();
        meters.correlation = corr_.correlation(); meters.width = corr_.width(); meters.monoLossDb = corr_.monoLossDb(); meters.crest = std::max(lvl_[0].crestDb(), lvl_[1].crestDb());
        // outputs
        if (nOut > 0) std::copy(M[0], M[0] + n, out[0]); if (nOut > 1) std::copy(M[1], M[1] + n, out[1]);
        if (nOut > 3) {
            const bool fromMaster = ms_.monitor.source == 0; const float lv = (ms_.monitor.mute ? 0.f : dbToLin(ms_.monitor.levelDb + (ms_.monitor.dim ? ms_.monitor.dimDb : 0.f)));
            for (int k = 0; k < n; ++k) {
                float l = fromMaster ? M[0][k] : aux_[0][0][(size_t)k] * dbToLin(ms_.auxMasterDb[0]), r = fromMaster ? M[1][k] : aux_[0][1][(size_t)k] * dbToLin(ms_.auxMasterDb[0]);
                if (ms_.monitor.mono) l = r = 0.5f * (l + r); out[2][k] = l * lv; out[3][k] = r * lv;
            }
        }
        for (int a = 1; a < kNumAux; ++a) if (nOut > 3 + 2 * a) { const float g = dbToLin(ms_.auxMasterDb[a]); for (int k = 0; k < n; ++k) { out[2 + 2 * a][k] = aux_[(size_t)a][0][(size_t)k] * g; out[3 + 2 * a][k] = aux_[(size_t)a][1][(size_t)k] * g; } }
        // CPU / safe mode supervision
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), budget = n / fs_;
        load_ = 0.9f * load_ + 0.1f * (float)(el / budget); meters.cpuLoad = load_; meters.dspLoad = (float)(el / budget);
        if (el > budget) meters.overloads++;
        if (safeEnabled_.load()) {
            if (load_ > 0.85f) { if (++hot_ > 20) { hot_ = 0; calm_ = 0; if (meters.safeLevel < 2) meters.safeLevel++; } } else hot_ = 0;
            if (load_ < 0.4f && meters.safeLevel > 0) { if (++calm_ > (int)(fs_ / n * 5)) { calm_ = 0; meters.safeLevel--; } } else calm_ = 0;
        }
    }

    void stereoStage(float* const* M, int n) {
        const auto& s = ms_.stereo; const float gl = std::min(1.f, 1.f - s.balance), gr = std::min(1.f, 1.f + s.balance);
        for (int k = 0; k < n; ++k) {
            float l = M[0][k], r = M[1][k];
            if (s.bassMonoHz > 20.f) { float ll, lh, rl, rh; bassLr_[0].split(0, l, ll, lh); bassLr_[1].split(0, r, rl, rh); const float bm = 0.5f * (ll + rl); l = lh + bm; r = rh + bm; }
            const float mid = 0.5f * (l + r), sd = 0.5f * (l - r) * s.width; l = mid + sd; r = mid - sd;
            if (s.mono) l = r = mid;
            M[0][k] = l * gl; M[1][k] = r * gr;
        }
    }
    void meterCh(int i, const float* L, const float* R, int n) {
        if (!L) { meters.chPeakDb[i] = -120.f; meters.chRmsDb[i] = -120.f; return; }
        chLevel_[i].process(L, n); chLevel_[i].process(R, n);
        meters.chPeakDb[i] = chLevel_[i].peakDb(); meters.chRmsDb[i] = chLevel_[i].rmsDb();
    }

    double fs_ = 48000; int maxBlock_ = 1024, nCh_ = 16; std::unique_ptr<ChannelStrip[]> strips_;
    std::array<ChannelState, kMaxChannels> st_{}; MasterState ms_;
    SpscQueue<ChCmd, 256> chQ_; SpscQueue<MasterState, 16> mQ_;
    std::array<std::array<std::vector<float>, 2>, 1> buf_; std::array<std::array<std::vector<float>, 2>, kNumSubs> sub_; std::array<std::array<std::vector<float>, 2>, kNumAux> aux_;
    std::array<std::array<std::vector<float>, 2>, kNumFx> fx_; std::array<std::vector<float>, 2> mix_, dry_; std::array<std::vector<float>, 4> tmp_; std::vector<float> gStep_;
    float prevMute_[kMaxChannels], prevFader_[kMaxChannels], prevMaster_ = 1.f, chGr_[kMaxChannels]{}, load_ = 0.f; int hot_ = 0, calm_ = 0;
    std::array<LevelMeter, kMaxChannels> chLevel_;
    ParametricEQ mEq_; DynamicEQ mDyn_; Compressor mGlue_; MultibandCompressor mMb_; Saturator mSat_; SoftClipper mClip_; TruePeakLimiter mLim_; Reverb rev_; DelayFx dly_; LR4 bassLr_[2];
    LoudnessMeter lufs_; LevelMeter lvl_[2]; CorrelationMeter corr_; SpectrumAnalyzer spec_;
    std::atomic<bool> bypass_{false}, safeEnabled_{true};
};

} // namespace slp
