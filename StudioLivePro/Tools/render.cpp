// slp_render: offline processing of a WAV file through a channel preset + master preset (for listening tests and the user's own material)
// usage: slp_render in.wav out.wav [channel-preset-name-substring] [master-preset-name-substring]
#include <cstdio>
#include <cstring>
#include <fstream>
#include "../Source/Presets/Presets.h"
using namespace slp;
struct Wav { int ch = 1, sr = 48000; std::vector<float> d; };
static bool readWav(const char* p, Wav& w) {
    std::ifstream f(p, std::ios::binary); if (!f) return false; char id[4]; uint32_t sz; f.read(id, 4); f.read((char*)&sz, 4); f.read(id, 4); if (std::memcmp(id, "WAVE", 4)) return false;
    uint16_t fmt = 0, ch = 0, bits = 0; uint32_t sr = 0; std::vector<char> data;
    while (f.read(id, 4) && f.read((char*)&sz, 4)) {
        if (!std::memcmp(id, "fmt ", 4)) { std::vector<char> b(sz); f.read(b.data(), sz); std::memcpy(&fmt, &b[0], 2); std::memcpy(&ch, &b[2], 2); std::memcpy(&sr, &b[4], 4); std::memcpy(&bits, &b[14], 2); if (fmt == 0xFFFE && sz >= 26) std::memcpy(&fmt, &b[24], 2); }
        else if (!std::memcmp(id, "data", 4)) { data.resize(sz); f.read(data.data(), sz); break; } else f.seekg(sz + (sz & 1), std::ios::cur);
    }
    if (!ch || data.empty()) return false; w.ch = ch; w.sr = (int)sr; const size_t n = data.size() / (bits / 8); w.d.resize(n);
    for (size_t i = 0; i < n; ++i) { const char* q = &data[i * (bits / 8)];
        if (fmt == 3 && bits == 32) { float v; std::memcpy(&v, q, 4); w.d[i] = v; } else if (bits == 16) { int16_t v; std::memcpy(&v, q, 2); w.d[i] = v / 32768.f; }
        else if (bits == 24) { int32_t v = (q[0] & 0xff) | ((q[1] & 0xff) << 8) | (q[2] << 16); w.d[i] = v / 8388608.f; } else if (bits == 32) { int32_t v; std::memcpy(&v, q, 4); w.d[i] = v / 2147483648.f; } else return false; }
    return true;
}
static bool writeWav(const char* p, const Wav& w) {
    std::ofstream f(p, std::ios::binary); if (!f) return false; uint32_t dsz = (uint32_t)(w.d.size() * 4), riff = 36 + dsz, fs = 16, sr = (uint32_t)w.sr, br = sr * w.ch * 4; uint16_t fmt = 3, ch = (uint16_t)w.ch, ba = (uint16_t)(w.ch * 4), bits = 32;
    f.write("RIFF", 4); f.write((char*)&riff, 4); f.write("WAVEfmt ", 8); f.write((char*)&fs, 4); f.write((char*)&fmt, 2); f.write((char*)&ch, 2); f.write((char*)&sr, 4); f.write((char*)&br, 4); f.write((char*)&ba, 2); f.write((char*)&bits, 2); f.write("data", 4); f.write((char*)&dsz, 4); f.write((const char*)w.d.data(), dsz); return (bool)f;
}
int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: slp_render in.wav out.wav [channel preset] [master preset]\n"); return 1; }
    Wav w; if (!readWav(argv[1], w)) { std::printf("cannot read %s (PCM16/24/32 or float WAV expected)\n", argv[1]); return 2; }
    const int nCh = std::min(w.ch, 2), frames = (int)(w.d.size() / w.ch); MixerEngine e; e.prepare(w.sr, 512, 16); MixerController c(&e, 16); c.ch[0].stereo = nCh == 2; c.ch[0].eqOn = false;
    if (argc > 3) for (auto& p : factoryChannelPresets()) if (p.name.find(argv[3]) != std::string::npos) { p.apply(c.ch[0]); std::printf("channel preset: %s\n", p.name.c_str()); break; }
    c.master.limOn = true; if (argc > 4) for (auto& p : factoryMasterPresets()) if (p.name.find(argv[4]) != std::string::npos) { p.apply(c.master); std::printf("master preset: %s\n", p.name.c_str()); break; }
    c.pushAll(); std::vector<std::vector<float>> in((size_t)nCh, std::vector<float>((size_t)frames)); for (int i = 0; i < frames; ++i) for (int k = 0; k < nCh; ++k) in[(size_t)k][(size_t)i] = w.d[(size_t)i * w.ch + k];
    std::vector<std::vector<float>> out(kNumOutputs, std::vector<float>((size_t)frames)); for (int s = 0; s < frames; s += 512) { const int len = std::min(512, frames - s); const float* ip[2]; float* op[kNumOutputs]; for (int k = 0; k < nCh; ++k) ip[k] = in[(size_t)k].data() + s; for (int o = 0; o < kNumOutputs; ++o) op[o] = out[(size_t)o].data() + s; e.process(ip, nCh, op, kNumOutputs, len); }
    Wav r; r.ch = 2; r.sr = w.sr; r.d.resize((size_t)frames * 2); for (int i = 0; i < frames; ++i) { r.d[(size_t)i * 2] = out[0][(size_t)i]; r.d[(size_t)i * 2 + 1] = out[1][(size_t)i]; }
    auto& m = e.meters; std::printf("done: %d frames @ %d Hz | LUFS I %.1f  S %.1f | true peak %.1f dBTP | LRA %.1f | latency %d samples\n", frames, w.sr, m.lufsI.load(), m.lufsS.load(), m.truePeakDb.load(), m.lra.load(), e.masterLatencySamples());
    return writeWav(argv[2], r) ? 0 : 3;
}
