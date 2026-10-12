// Studio Live Pro - single channel strip DSP chain
#pragma once
#include "../Core/State.h"

namespace slp {

class ChannelStrip {
public:
    void prepare(double fs, int maxBlock) {
        fs_ = fs; eq_.prepare(fs); dyn_.prepare(fs); gate_.prepare(fs); de_.prepare(fs); comp_.prepare(fs); sat_.prepare(fs, maxBlock); tr_.prepare(fs);
        hum_.prepare(fs); for (auto& n : nr_) n.prepare(fs); scratch_.assign((size_t)maxBlock, 0.f); setState(st_);
    }
    // Noise profile capture (thread-safe request, executed on the audio thread at the next block).
    void requestNoiseLearn(bool start) { learnReq_.store(start ? 1 : 2); }
    bool noiseProfileReady() const { return nr_[0].hasProfile() && !learning_; }
    void setState(const ChannelState& s) {
        st_ = s; eq_.setParams(s.eq); dyn_.setParams(s.dyn); gate_.setParams(s.gate); de_.setParams(s.deess); comp_.setParams(s.comp);
        sat_.setParams(s.sat); tr_.setParams(s.trans); hum_.setParams(s.hum); for (auto& n : nr_) n.setParams(s.noise);
        inG_ = dbToLin(s.inputGainDb) * (s.phaseInvert ? -1.f : 1.f);
        if (s.hpfHz > 10.f) { hp_[0].set(FilterType::HighPass, fs_, s.hpfHz, 0, 0.5412); hp_[1].set(FilterType::HighPass, fs_, s.hpfHz, 0, 1.3066); hpOn_ = true; } else hpOn_ = false; // 4th-order Butterworth
    }
    // safeLevel: 0 = full, 1 = skip heavy modules, 2 = gain/HPF only
    void process(float* const* x, int nCh, int n, int safeLevel) {
        for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) x[c][i] *= inG_;
        if (hpOn_) for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) x[c][i] = hp_[1].process(c, hp_[0].process(c, x[c][i]));
        { const int rq = learnReq_.exchange(0); if (rq == 1) { for (auto& nn : nr_) nn.startLearning(); learning_ = true; } else if (rq == 2) { for (auto& nn : nr_) nn.stopLearning(); learning_ = false; } }
        if (learning_) for (int c = 0; c < nCh; ++c) { std::copy(x[c], x[c] + n, scratch_.begin()); nr_[c].process(scratch_.data(), n); }
        if (safeLevel >= 2) return;
        if (st_.humOn && st_.hum.enabled) hum_.process(x, nCh, n);
        if (st_.noiseOn && st_.noise.enabled && !learning_ && nr_[0].hasProfile()) for (int c = 0; c < nCh; ++c) nr_[c].process(x[c], n);
        if (st_.gateOn) gate_.process(x, nCh, n);
        if (st_.eqOn) eq_.process(x, nCh, n);
        if (safeLevel == 0) {
            if (st_.dynEqOn) dyn_.process(x, nCh, n);
            if (st_.deessOn) de_.process(x, nCh, n);
        }
        if (st_.compOn) comp_.process(x, nCh, n);
        if (safeLevel == 0) {
            if (st_.satOn) sat_.process(x, nCh, n);
            if (st_.transOn) tr_.process(x, nCh, n);
        }
    }
    float compGrDb() const { return comp_.grMeter; } float gateGrDb() const { return gate_.grMeter; }
    const ChannelState& state() const { return st_; }
    int latencySamples() const { return (st_.compOn ? comp_.latencySamples() : 0) + (st_.satOn ? sat_.latencySamples() : 0) + (st_.noiseOn && st_.noise.enabled ? NoiseReducer::kN - NoiseReducer::kN / 4 : 0); }
private:
    double fs_ = 48000; ChannelState st_; ParametricEQ eq_; DynamicEQ dyn_; Gate gate_; DeEsser de_; Compressor comp_; Saturator sat_; TransientDesigner tr_;
    HumRemover hum_; NoiseReducer nr_[2]; std::vector<float> scratch_; std::atomic<int> learnReq_{0}; bool learning_ = false; Biquad2ch hp_[2]; bool hpOn_ = false; float inG_ = 1.f;
};

} // namespace slp
