// Studio Live Pro - signal analysis: AI Studio Magic (rule-based, explainable), reference comparison, masking detection
#pragma once
#include <string>
#include "../DSP/Spectral.h"
#include "../Metering/Meters.h"
#include "../Mixer/ChannelStrip.h"

namespace slp {

enum class SourceType { Auto, Vocal, Speech, Bass, Drums, Guitar, Keys, Other };
inline const char* sourceName(SourceType t) { static const char* n[] = {"Auto", "Vocal", "Speech", "Bass", "Drums", "Guitar", "Keys", "Other"}; return n[(int)t]; }

struct Analysis {
    static constexpr int kBands = 30;                 // 1/3-octave, 25 Hz .. 20 kHz
    float band[kBands]{};                              // dB (integrated power per band; flat = pink noise)
    float rmsDb = -120, peakDb = -120, crestDb = 0, noiseFloorDb = -120, centroidHz = 0, lufs = -120; SourceType guess = SourceType::Other;
    static float centerHz(int k) { return 25.f * std::pow(2.f, k / 3.f); }
    static int bandFor(float hz) { return std::min(kBands - 1, std::max(0, (int)std::lround(3.f * std::log2(hz / 25.f)))); }
    float at(float hz) const { const int k = bandFor(hz); return (band[std::max(0, k - 0)] * 2.f + band[std::max(0, k - 1)] + band[std::min(kBands - 1, k + 1)]) * 0.25f; }
};

inline Analysis analyzeSignal(const float* x, int n, double fs) {
    Analysis a; if (n < 4096) return a;
    FFT fft(12); const int N = 4096; std::vector<float> re((size_t)N), im((size_t)N), win((size_t)N); std::vector<double> pw((size_t)N / 2, 0.0);
    for (int i = 0; i < N; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * i / N));
    int frames = 0; for (int s = 0; s + N <= n; s += N / 2) { for (int i = 0; i < N; ++i) { re[(size_t)i] = x[s + i] * win[(size_t)i]; im[(size_t)i] = 0; } fft.transform(re.data(), im.data(), false);
        for (int k = 0; k < N / 2; ++k) pw[(size_t)k] += (double)re[(size_t)k] * re[(size_t)k] + (double)im[(size_t)k] * im[(size_t)k]; ++frames; }
    double totalP = 0, cent = 0; const double norm = 1.0 / ((double)frames * N * N * 0.375);
    for (int b = 0; b < Analysis::kBands; ++b) { const double fc = Analysis::centerHz(b), lo = fc / 1.1225, hi = fc * 1.1225; double sum = 0;
        for (int k = 1; k < N / 2; ++k) { const double f = k * fs / N; if (f >= lo && f < hi) sum += pw[(size_t)k] * norm; }
        a.band[b] = (float)(10 * std::log10(sum + 1e-14)); }
    for (int k = 1; k < N / 2; ++k) { const double p = pw[(size_t)k]; totalP += p; cent += p * k * fs / N; } a.centroidHz = (float)(cent / std::max(totalP, 1e-20));
    double s2 = 0, pk = 0; for (int i = 0; i < n; ++i) { s2 += (double)x[i] * x[i]; pk = std::max(pk, (double)std::fabs(x[i])); }
    a.rmsDb = (float)(10 * std::log10(s2 / n + 1e-14)); a.peakDb = (float)(20 * std::log10(pk + 1e-9)); a.crestDb = a.peakDb - a.rmsDb;
    const int fl = (int)(fs * 0.05); std::vector<float> fr; for (int s = 0; s + fl <= n; s += fl) { double e = 0; for (int i = 0; i < fl; ++i) e += (double)x[s + i] * x[s + i]; fr.push_back((float)(10 * std::log10(e / fl + 1e-14))); }
    if (!fr.empty()) { std::sort(fr.begin(), fr.end()); a.noiseFloorDb = fr[fr.size() / 10]; }
    LoudnessMeter lm; lm.prepare(fs, 1); const float* ch[1] = {x}; lm.process(ch, n); a.lufs = lm.integrated();
    double low = 0, tot = 0; for (int b = 0; b < Analysis::kBands; ++b) { const double p = std::pow(10.0, a.band[b] / 10.0); tot += p; if (Analysis::centerHz(b) < 250) low += p; }
    const double lowFrac = low / std::max(tot, 1e-20);
    a.guess = lowFrac > 0.6 ? SourceType::Bass : (a.crestDb > 20 && a.centroidHz > 1500) ? SourceType::Drums : (a.centroidHz > 300 && a.centroidHz < 2800 && a.crestDb < 20) ? SourceType::Vocal : SourceType::Other;
    return a;
}

struct Change { std::string what, why; float amountDb = 0; };
struct MagicResult { ChannelState state; std::vector<Change> changes; Analysis before, after; SourceType source = SourceType::Other; };

// AI STUDIO MAGIC: moderate, explainable processing suggestions (no change > 3 dB EQ).
inline MagicResult studioMagic(const float* x, int n, double fs, ChannelState base, SourceType hint = SourceType::Auto) {
    MagicResult r; r.before = analyzeSignal(x, n, fs); r.state = base; const Analysis& a = r.before; ChannelState& s = r.state;
    SourceType t = hint == SourceType::Auto ? a.guess : hint; r.source = t; int nb = 0; for (auto& b : s.eq.band) if (b.enabled) ++nb;
    auto addBand = [&](FilterType ty, float f, float g, float q) { if (nb < 24) { s.eq.band[(size_t)nb++] = {true, ty, f, g, q, 12}; s.eqOn = true; } };
    char buf[200];
    const bool voiceLike = t == SourceType::Vocal || t == SourceType::Speech;
    // 1. cleanliness: high-pass
    float hpf = voiceLike ? 90.f : (t == SourceType::Bass ? 30.f : (t == SourceType::Drums ? 40.f : 60.f));
    if (a.at(40) > a.at(160) + 2.f || voiceLike) { s.hpfHz = hpf; std::snprintf(buf, sizeof buf, "High-pass em %.0f Hz.", hpf); r.changes.push_back({buf, "Remove energia subsônica/rumble que consome headroom sem contribuir para o timbre."}); }
    // 2. noise gate when there's a measurable noise floor
    if (a.noiseFloorDb > -75.f && a.noiseFloorDb < a.rmsDb - 15.f && t != SourceType::Bass) { s.gateOn = true; s.gate.thresholdDb = a.noiseFloorDb + 9.f; s.gate.rangeDb = -18.f; s.gate.ratio = 3.f;
        std::snprintf(buf, sizeof buf, "Gate/expansor com threshold %.0f dB.", s.gate.thresholdDb); r.changes.push_back({buf, "O piso de ruído medido (" + std::to_string((int)a.noiseFloorDb) + " dBFS) está próximo do sinal; expansão suave reduz ruído entre frases."}); }
    // 3. low-mid buildup (200-500 Hz relative to neighbours)
    { const float excess = a.at(320) - 0.5f * (a.at(125) + a.at(800)); if (excess > 2.5f) { const float g = -std::min(3.f, 0.6f * excess); addBand(FilterType::Bell, 320, g, 1.2f);
        std::snprintf(buf, sizeof buf, "Redução de %.1f dB em 320 Hz.", -g); r.changes.push_back({buf, "Reduz acúmulo de low-mid (a região de 320 Hz está " + std::to_string((int)excess) + " dB acima dos vizinhos), que deixa o som embolado."}); } }
    // 4. harshness 2.5-5 kHz
    { const float excess = a.at(3500) - 0.5f * (a.at(1600) + a.at(7000)); if (excess > 3.f) { const float g = -std::min(3.f, 0.5f * excess); addBand(FilterType::Bell, 3500, g, 1.5f);
        std::snprintf(buf, sizeof buf, "Redução de %.1f dB em 3,5 kHz.", -g); r.changes.push_back({buf, "Controla aspereza (harshness): a banda de presença sobressai " + std::to_string((int)excess) + " dB acima do entorno."}); }
      else if (voiceLike && excess < -4.f) { addBand(FilterType::Bell, 3500, 1.5f, 1.f); r.changes.push_back({"Realce de 1,5 dB em 3,5 kHz.", "Falta presença na região de inteligibilidade da voz."}); } }
    // 5. sibilance
    if (voiceLike) { const float sib = a.at(7000) - a.at(3000); if (sib > -2.f) { s.deessOn = true; s.deess.freqHz = 7000; s.deess.thresholdDb = a.rmsDb - 8.f; s.deess.rangeDb = 8.f; r.changes.push_back({"De-esser ativado em 7 kHz.", "A energia em 7 kHz está alta em relação a 3 kHz (sibilância provável)."}); } }
    // 6. dynamics
    if (a.crestDb > 14.f && t != SourceType::Drums) { s.compOn = true; s.comp.model = voiceLike ? CompModel::Optical : CompModel::VCA; s.comp.thresholdDb = a.rmsDb + 3.f; s.comp.ratio = a.crestDb > 20.f ? 4.f : 3.f; s.comp.attackMs = 12; s.comp.releaseMs = 150; s.comp.kneeDb = 8; s.comp.autoGain = true;
        r.changes.push_back({"Compressor " + std::string(voiceLike ? "óptico" : "VCA") + ", razão " + std::to_string((int)s.comp.ratio) + ":1.", "Fator de crista de " + std::to_string((int)a.crestDb) + " dB indica dinâmica ampla; compressão moderada melhora consistência sem esmagar o sinal."}); }
    if (t == SourceType::Drums) { s.transOn = true; s.trans.attack = 0.2f; r.changes.push_back({"Transient designer: attack +20%.", "Fonte percussiva: realça o ataque preservando o corpo."}); }
    // render "after"
    ChannelStrip strip; strip.prepare(fs, 512); strip.setState(s); std::vector<float> y(x, x + n); for (int p = 0; p < n; p += 512) { float* ptr[1] = {y.data() + p}; strip.process(ptr, 1, std::min(512, n - p), 0); }
    r.after = analyzeSignal(y.data(), n, fs); return r;
}

// Reference comparison: suggests (never applies) tonal corrections to match the reference's spectral balance.
inline std::vector<Change> compareToReference(const Analysis& mix, const Analysis& ref) {
    std::vector<Change> out; struct R { const char* name; float lo, hi; } regs[] = {{"graves (40-120 Hz)", 40, 120}, {"médios-graves (160-500 Hz)", 160, 500}, {"médios (630-2k Hz)", 630, 2000}, {"presença (2.5-6 kHz)", 2500, 6000}, {"agudos (8-16 kHz)", 8000, 16000}};
    auto avg = [](const Analysis& a, float lo, float hi) { double s = 0; int c = 0; for (int b = Analysis::bandFor(lo); b <= Analysis::bandFor(hi); ++b, ++c) s += a.band[b]; return (float)(s / std::max(c, 1)); };
    float mean = 0, refMean = 0; for (auto& r : regs) { mean += avg(mix, r.lo, r.hi); refMean += avg(ref, r.lo, r.hi); } mean /= 5; refMean /= 5;
    for (auto& r : regs) { const float d = (avg(mix, r.lo, r.hi) - mean) - (avg(ref, r.lo, r.hi) - refMean);
        if (std::fabs(d) > 2.f) { char b[160]; std::snprintf(b, sizeof b, "%s: %s %.1f dB", r.name, d > 0 ? "reduzir" : "aumentar", std::min(3.f, std::fabs(d))); out.push_back({b, "Em relação à referência, esta região está " + std::string(d > 0 ? "mais forte" : "mais fraca") + " em " + std::to_string((int)std::fabs(d)) + " dB (após normalizar o nível geral).", d}); } }
    const float dl = ref.lufs - mix.lufs; if (std::fabs(dl) > 1.5f && ref.lufs > -100 && mix.lufs > -100) { char b[120]; std::snprintf(b, sizeof b, "Loudness: referência %.1f LUFS vs. mix %.1f LUFS", ref.lufs, mix.lufs); out.push_back({b, "Diferença de loudness; ajuste o ganho do master antes de comparar timbres.", dl}); }
    const float dc = ref.crestDb - mix.crestDb; if (std::fabs(dc) > 3.f) out.push_back({dc > 0 ? "Dinâmica: a referência é mais dinâmica." : "Dinâmica: a referência é mais comprimida.", "Fator de crista " + std::to_string((int)ref.crestDb) + " dB (ref) vs " + std::to_string((int)mix.crestDb) + " dB (mix).", dc});
    return out;
}

struct MaskingConflict { float centerHz; float overlapDb; std::string suggestion; };
// Masking detection between two sources (e.g. vocal vs. guitar): bands where both are strong.
inline std::vector<MaskingConflict> detectMasking(const Analysis& primary, const Analysis& secondary, const char* primaryName = "A", const char* secondaryName = "B") {
    std::vector<MaskingConflict> out; float pMax = -200, sMax = -200; for (int b = 0; b < Analysis::kBands; ++b) { pMax = std::max(pMax, primary.band[b]); sMax = std::max(sMax, secondary.band[b]); }
    for (int b = 2; b < Analysis::kBands - 1; ++b) { const float p = primary.band[b] - pMax, s = secondary.band[b] - sMax;
        if (p > -9.f && s > -9.f) { const float ov = std::min(p, s) + 9.f; if (ov > 3.f && Analysis::centerHz(b) > 100.f && out.size() < 8) { char buf[200]; std::snprintf(buf, sizeof buf, "Reduzir %.1f dB de %s em %.0f Hz (Q 1.4) para abrir espaço para %s.", std::min(2.5f, 0.4f * ov), secondaryName, Analysis::centerHz(b), primaryName); out.push_back({Analysis::centerHz(b), ov, buf}); } } }
    return out;
}

} // namespace slp
