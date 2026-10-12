// Studio Live Pro - FFT, Spectrum Analyzer, Noise Reduction (spectral, profile based), Hum Remover
#pragma once
#include "Biquad.h"

namespace slp {

class FFT {
public:
    explicit FFT(int order = 10) { init(order); }
    void init(int order) {
        order_ = order; n_ = 1 << order; cs_.resize((size_t)n_ / 2); sn_.resize((size_t)n_ / 2); rev_.resize((size_t)n_);
        for (int i = 0; i < n_ / 2; ++i) { cs_[(size_t)i] = (float)std::cos(2 * kPi * i / n_); sn_[(size_t)i] = (float)-std::sin(2 * kPi * i / n_); }
        for (int i = 0; i < n_; ++i) { int r = 0, v = i; for (int b = 0; b < order; ++b) { r = (r << 1) | (v & 1); v >>= 1; } rev_[(size_t)i] = r; }
    }
    int size() const { return n_; }
    // in-place complex transform; inverse=true applies 1/N scaling
    void transform(float* re, float* im, bool inverse) const {
        for (int i = 0; i < n_; ++i) { int j = rev_[(size_t)i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len / 2, step = n_ / len;
            for (int i = 0; i < n_; i += len) for (int k = 0; k < half; ++k) {
                const float wr = cs_[(size_t)(k * step)], wi = inverse ? -sn_[(size_t)(k * step)] : sn_[(size_t)(k * step)];
                const int a = i + k, b = a + half;
                const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
            }
        }
        if (inverse) { const float s = 1.f / (float)n_; for (int i = 0; i < n_; ++i) { re[i] *= s; im[i] *= s; } }
    }
private:
    int order_ = 10, n_ = 1024; std::vector<float> cs_, sn_; std::vector<int> rev_;
};

// Real-time spectrum analyzer (Hann, 4096-point, 75% overlap). Writer: audio thread. Reader: UI thread.
class SpectrumAnalyzer {
public:
    static constexpr int kOrder = 12, kSize = 1 << kOrder, kBins = kSize / 2, kHop = kSize / 4;
    SpectrumAnalyzer() : fft_(kOrder) { win_.resize(kSize); for (int i = 0; i < kSize; ++i) win_[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * i / kSize));
        ring_.assign(kSize, 0.f); re_.assign(kSize, 0.f); im_.assign(kSize, 0.f); for (auto& b : out_) b.assign(kBins, -120.f); avg_.assign(kBins, 1e-12f); peak_.assign(kBins, -120.f); }
    void prepare(double fs) { fs_ = fs; }
    void push(const float* x, int n) {
        for (int i = 0; i < n; ++i) { ring_[(size_t)pos_] = x[i]; pos_ = (pos_ + 1) % kSize; if (++cnt_ >= kHop) { cnt_ = 0; analyze(); } }
    }
    // Copies the latest magnitude spectrum (dBFS per bin, sine of amplitude 1 = 0 dB) into dst (kBins values).
    void getSpectrum(float* dst) const { const int r = ready_.load(std::memory_order_acquire); std::copy(out_[r].begin(), out_[r].end(), dst); }
    void getPeakHold(float* dst) const { std::copy(peak_.begin(), peak_.end(), dst); }
    double binHz(int k) const { return k * fs_ / kSize; }
    int binForHz(double f) const { return std::min(kBins - 1, std::max(0, (int)std::lround(f * kSize / fs_))); }
private:
    void analyze() {
        for (int i = 0; i < kSize; ++i) { re_[(size_t)i] = ring_[(size_t)((pos_ + i) % kSize)] * win_[(size_t)i]; im_[(size_t)i] = 0; }
        fft_.transform(re_.data(), im_.data(), false);
        const int w = 1 - ready_.load(); const float norm = 4.f / (float)kSize; // Hann coherent gain 0.5, one-sided
        for (int k = 0; k < kBins; ++k) {
            const float p = (re_[(size_t)k] * re_[(size_t)k] + im_[(size_t)k] * im_[(size_t)k]) * norm * norm;
            avg_[(size_t)k] = 0.6f * avg_[(size_t)k] + 0.4f * p;
            const float db = 10.f * std::log10(avg_[(size_t)k] + 1e-12f); out_[w][(size_t)k] = db;
            peak_[(size_t)k] = std::max(db, peak_[(size_t)k] - 0.5f);
        }
        ready_.store(w, std::memory_order_release);
    }
    FFT fft_; double fs_ = 48000; std::vector<float> win_, ring_, re_, im_, avg_, peak_; std::vector<float> out_[2]; std::atomic<int> ready_{0}; int pos_ = 0, cnt_ = 0;
};

// ---------------- Noise reduction ----------------
struct NoiseParams {
    float reductionDb = 12.f, sensitivity = 1.f, smoothing = 0.6f; bool enabled = false;
    template <class V> void visit(V& v) { v.field("red", reductionDb); v.field("sens", sensitivity); v.field("smooth", smoothing); v.field("on", enabled); }
};
class NoiseReducer {
public:
    static constexpr int kOrder = 10, kN = 1 << kOrder, kHop = kN / 4, kBins = kN / 2 + 1;
    NoiseReducer() : fft_(kOrder) {
        win_.resize(kN); for (int i = 0; i < kN; ++i) win_[(size_t)i] = (float)std::sqrt(0.5 - 0.5 * std::cos(2 * kPi * i / kN)); // sqrt-Hann
        inR_.assign(kN, 0.f); outA_.assign(kN, 0.f); re_.assign(kN, 0.f); im_.assign(kN, 0.f);
        noise_.assign(kBins, 0.f); gain_.assign(kBins, 1.f); tmpG_.assign(kBins, 1.f);
    }
    void prepare(double) { reset(); }
    void reset() { std::fill(inR_.begin(), inR_.end(), 0.f); std::fill(outA_.begin(), outA_.end(), 0.f); std::fill(gain_.begin(), gain_.end(), 1.f); pos_ = 0; cnt_ = 0; }
    int latencySamples() const { return kN - kHop; }
    void setParams(const NoiseParams& p) { p_ = p; }
    // Noise profile capture: while enabled, frames update the average noise power spectrum.
    void startLearning() { std::fill(noise_.begin(), noise_.end(), 0.f); learnFrames_ = 0; learning_ = true; }
    void stopLearning() { learning_ = false; }
    bool hasProfile() const { return learnFrames_ > 0; }
    bool learning() const { return learning_; }
    const std::vector<float>& profile() const { return noise_; }

    void process(float* x, int n) {
        for (int i = 0; i < n; ++i) {
            inR_[(size_t)pos_] = x[i]; pos_ = (pos_ + 1) % kN;
            // output: finished samples at the head of the accumulator
            const float o = outA_[(size_t)outPos_]; outA_[(size_t)outPos_] = 0.f; outPos_ = (outPos_ + 1) % kN;
            if (++cnt_ >= kHop) { cnt_ = 0; frame(); }
            x[i] = (p_.enabled || learning_) ? o : o;
        }
    }
private:
    void frame() {
        for (int i = 0; i < kN; ++i) { re_[(size_t)i] = inR_[(size_t)((pos_ + i) % kN)] * win_[(size_t)i]; im_[(size_t)i] = 0; }
        fft_.transform(re_.data(), im_.data(), false);
        const float gmin = dbToLin(-std::fabs(p_.reductionDb)), alpha = 4.0f * p_.sensitivity;
        if (learning_) {
            ++learnFrames_;
            for (int k = 0; k < kBins; ++k) { const float p = re_[(size_t)k] * re_[(size_t)k] + im_[(size_t)k] * im_[(size_t)k]; noise_[(size_t)k] += (p - noise_[(size_t)k]) / (float)learnFrames_; }
        }
        const bool active = p_.enabled && learnFrames_ > 0 && !learning_;
        for (int k = 0; k < kBins; ++k) {
            float g = 1.f;
            if (active) {
                const float p = re_[(size_t)k] * re_[(size_t)k] + im_[(size_t)k] * im_[(size_t)k];
                g = std::sqrt(std::max(gmin * gmin, 1.f - alpha * noise_[(size_t)k] / (p + 1e-20f)));   // power-domain subtraction, floor = reduction amount
            }
            tmpG_[(size_t)k] = g;
        }
        for (int k = 0; k < kBins; ++k) {  // frequency smoothing (3-bin) + temporal smoothing against musical noise
            const float a = tmpG_[(size_t)std::max(0, k - 1)], b = tmpG_[(size_t)k], c = tmpG_[(size_t)std::min(kBins - 1, k + 1)];
            const float gf = 0.25f * a + 0.5f * b + 0.25f * c, s = p_.smoothing;
            gain_[(size_t)k] = gf < gain_[(size_t)k] ? gf : gf + s * (gain_[(size_t)k] - gf);
        }
        for (int k = 0; k < kBins; ++k) { re_[(size_t)k] *= gain_[(size_t)k]; im_[(size_t)k] *= gain_[(size_t)k]; if (k > 0 && k < kN / 2) { re_[(size_t)(kN - k)] = re_[(size_t)k]; im_[(size_t)(kN - k)] = -im_[(size_t)k]; } }
        fft_.transform(re_.data(), im_.data(), true);
        const float norm = 2.f / 3.f; // sqrt-Hann^2 (=Hann) at 75% overlap sums to 2... windows applied twice => sum = 2 (scaled by 0.5 below)
        for (int i = 0; i < kN; ++i) outA_[(size_t)((outPos_ + i) % kN)] += re_[(size_t)i] * win_[(size_t)i] * 0.5f;
        (void)norm;
    }
    FFT fft_; NoiseParams p_; std::vector<float> win_, inR_, outA_, re_, im_, noise_, gain_, tmpG_; int pos_ = 0, outPos_ = 0, cnt_ = 0, learnFrames_ = 0; bool learning_ = false;
};

// ---------------- Hum remover ----------------
struct HumParams {
    bool enabled = false; int mainsHz = 60; float intensity = 1.f; int harmonics = 4;
    template <class V> void visit(V& v) { v.field("on", enabled); v.field("hz", mainsHz); v.field("int", intensity); v.field("harm", harmonics); }
};
class HumRemover {
public:
    static constexpr int kMax = 8;
    void prepare(double fs) { fs_ = fs; reset(); setParams(p_); }
    void reset() { for (auto& n : f_) n.reset(); }
    void setParams(const HumParams& p) {
        p_ = p; used_ = std::min(std::max(p.harmonics, 1), kMax);
        for (int k = 0; k < used_; ++k) f_[(size_t)k].set(FilterType::Notch, fs_, p.mainsHz * (k + 1), 0, 8.0 + 40.0 * clampf(p.intensity, 0.f, 1.f));
    }
    void process(float* const* x, int nCh, int n) {
        if (!p_.enabled) return;
        for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) {
            float y = x[c][i]; for (int k = 0; k < used_; ++k) { if (p_.mainsHz * (k + 1) >= fs_ * 0.45) break; y = f_[(size_t)k].process(c, y); }
            x[c][i] = y;
        }
    }
private:
    double fs_ = 48000; HumParams p_; int used_ = 4; std::array<Biquad2ch, kMax> f_;
};

} // namespace slp
