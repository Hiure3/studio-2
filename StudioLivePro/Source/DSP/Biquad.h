// Studio Live Pro - Biquad filters (RBJ cookbook), double precision state.
#pragma once
#include "Common.h"
#include <complex>

namespace slp {

enum class FilterType { Bell, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass, Tilt, AllPass };

struct BiquadCoeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs make(FilterType t, double fs, double f, double gainDb, double q) {
        f = std::min(std::max(f, 10.0), fs * 0.49);
        q = std::max(q, 0.05);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * f / fs, cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        switch (t) {
        case FilterType::Bell:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case FilterType::LowShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - s; break; }
        case FilterType::HighShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - s; break; }
        case FilterType::HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::Notch:
            b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::BandPass:
            b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::AllPass:
            b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        default: // Tilt is built from two shelves by the caller
            b0 = 1; b1 = 0; b2 = 0; a0 = 1; a1 = 0; a2 = 0; break;
        }
        BiquadCoeffs c;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    double magnitude(double fs, double f) const {
        const double w = 2.0 * kPi * f / fs;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
        return std::abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }
};

struct BiquadState {
    double z1 = 0, z2 = 0;
    inline float process(const BiquadCoeffs& c, float x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float)y;
    }
    void reset() { z1 = z2 = 0; }
};

// Convenience: a single biquad with its coefficients, one state per channel (max 2).
struct Biquad2ch {
    BiquadCoeffs c; BiquadState s[2];
    void set(FilterType t, double fs, double f, double g, double q) { c = BiquadCoeffs::make(t, fs, f, g, q); }
    inline float process(int ch, float x) { return s[ch].process(c, x); }
    void reset() { s[0].reset(); s[1].reset(); }
};

// Linkwitz-Riley 4th order split (two cascaded Butterworth 2nd order sections).
struct LR4 {
    BiquadCoeffs lp, hp; BiquadState zl[2][2], zh[2][2];
    void set(double fs, double f) {
        lp = BiquadCoeffs::make(FilterType::LowPass, fs, f, 0, 0.70710678);
        hp = BiquadCoeffs::make(FilterType::HighPass, fs, f, 0, 0.70710678);
    }
    inline void split(int ch, float x, float& lo, float& hi) {
        lo = zl[ch][1].process(lp, zl[ch][0].process(lp, x));
        hi = zh[ch][1].process(hp, zh[ch][0].process(hp, x));
    }
    void reset() { for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) { zl[c][k].reset(); zh[c][k].reset(); } }
};

} // namespace slp
