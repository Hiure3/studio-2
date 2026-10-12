// Studio Live Pro - audio host: JUCE device callback <-> MixerEngine, virtual output (OBS), MIDI, logging
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../MIDI/MidiMap.h"
#include "../Presets/Presets.h"
#include "../AI/Analyzer.h"

namespace slp {

inline juce::File appDataDir() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("StudioLivePro"); d.createDirectory(); return d; }
inline juce::File logsDir() { auto d = appDataDir().getChildFile("Logs"); d.createDirectory(); return d; }
inline void logLine(const juce::String& s) { if (auto* l = juce::Logger::getCurrentLogger()) l->writeToLog(s); }

// Secondary output (e.g. VB-Cable / virtual audio device used as OBS audio source) fed through a lock-free FIFO.
class VirtualOutput : public juce::AudioIODeviceCallback {
public:
    VirtualOutput() : fifo(1 << 16), buf(2, 1 << 16) {}
    bool open(const juce::String& typeName, const juce::String& deviceName, double sampleRate) {
        close(); mgr = std::make_unique<juce::AudioDeviceManager>(); mgr->setCurrentAudioDeviceType(typeName, true);
        juce::AudioDeviceManager::AudioDeviceSetup s; mgr->getAudioDeviceSetup(s); s.outputDeviceName = deviceName; s.inputDeviceName = ""; s.sampleRate = sampleRate; s.useDefaultOutputChannels = true;
        auto err = mgr->initialise(0, 2, nullptr, true, {}, &s); if (err.isNotEmpty()) { logLine("Virtual output error: " + err); mgr.reset(); return false; }
        mgr->addAudioCallback(this); active = true; logLine("Virtual output opened: " + deviceName); return true;
    }
    void close() { if (mgr) { mgr->removeAudioCallback(this); mgr.reset(); } active = false; fifo.reset(); }
    void push(const float* l, const float* r, int n) {
        if (!active.load()) return; int s1, z1, s2, z2; fifo.prepareToWrite(n, s1, z1, s2, z2); if (z1 + z2 < n) { dropped++; return; }
        if (z1) { std::copy(l, l + z1, buf.getWritePointer(0, s1)); std::copy(r, r + z1, buf.getWritePointer(1, s1)); }
        if (z2) { std::copy(l + z1, l + z1 + z2, buf.getWritePointer(0, s2)); std::copy(r + z1, r + z1 + z2, buf.getWritePointer(1, s2)); } fifo.finishedWrite(z1 + z2);
    }
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int nOut, int n, const juce::AudioIODeviceCallbackContext&) override {
        for (int c = 0; c < nOut; ++c) std::fill(out[c], out[c] + n, 0.f);
        const int avail = fifo.getNumReady(); if (avail < n) { underruns++; return; }
        if (avail > n * 6) fifo.finishedRead(avail - n * 3);   // drift control: keep ~3 blocks of margin
        int s1, z1, s2, z2; fifo.prepareToRead(n, s1, z1, s2, z2);
        for (int c = 0; c < std::min(nOut, 2); ++c) { if (z1) std::copy(buf.getReadPointer(c, s1), buf.getReadPointer(c, s1) + z1, out[c]); if (z2) std::copy(buf.getReadPointer(c, s2), buf.getReadPointer(c, s2) + z2, out[c] + z1); }
        fifo.finishedRead(z1 + z2);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {} void audioDeviceStopped() override {}
    std::atomic<int> underruns{0}, dropped{0}; std::atomic<bool> active{false};
private:
    juce::AbstractFifo fifo; juce::AudioBuffer<float> buf; std::unique_ptr<juce::AudioDeviceManager> mgr;
};

class Host : public juce::AudioIODeviceCallback, public juce::MidiInputCallback {
public:
    static constexpr int kChannels = 16, kCaptureSeconds = 8;
    Host() : controller(&engine, kChannels) {
        for (auto& o : outBuf) o.assign(8192, 0.f); capture.assign((size_t)(48000 * 2 * kCaptureSeconds), 0.f);
    }
    ~Host() override { shutdown(); }
    bool startup() {
        auto st = appDataDir().getChildFile("audio-settings.xml"); std::unique_ptr<juce::XmlElement> xml; if (st.existsAsFile()) xml = juce::XmlDocument::parse(st);
        firstRun = xml == nullptr; engine.prepare(48000.0, 1024, kChannels); controller.master.limOn = true; controller.pushAll();
        auto err = dm.initialise(32, 8, xml.get(), true); if (err.isNotEmpty()) logLine("Audio device init error: " + err);
        dm.addAudioCallback(this); enableAllMidi(); return err.isEmpty();
    }
    void shutdown() { saveSettings(); virtualOut.close(); dm.removeAudioCallback(this); dm.closeAudioDevice(); }
    void saveSettings() { if (auto xml = dm.createStateXml()) xml->writeTo(appDataDir().getChildFile("audio-settings.xml")); }
    void enableAllMidi() { for (auto& d : juce::MidiInput::getAvailableDevices()) { dm.setMidiInputDeviceEnabled(d.identifier, true); dm.addMidiInputDeviceCallback(d.identifier, this); logLine("MIDI input: " + d.name); } }

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
        fs = d->getCurrentSampleRate(); block = d->getCurrentBufferSizeSamples(); inLat = d->getInputLatencyInSamples(); outLat = d->getOutputLatencyInSamples();
        engine.prepare(fs, std::max(block, 64), kChannels); controller.pushAll(); capture.assign((size_t)(fs * kCaptureSeconds), 0.f); capPos = 0;
        logLine("Device started: " + d->getName() + " type=" + d->getTypeName() + " sr=" + juce::String(fs) + " buf=" + juce::String(block) + " inLat=" + juce::String(inLat) + " outLat=" + juce::String(outLat)); lastXr = 0; running = true;
    }
    void audioDeviceStopped() override { running = false; logLine("Device stopped"); }
    void audioDeviceError(const juce::String& m) override { logLine("Audio device error: " + m); }
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n, const juce::AudioIODeviceCallbackContext&) override {
        for (int c = 0; c < nOut; ++c) std::fill(out[c], out[c] + n, 0.f);
        if (n > 8192) return; float* op[kNumOutputs]; for (int i = 0; i < kNumOutputs; ++i) op[i] = outBuf[(size_t)i].data();
        if (testTone.load() > 0) { // test tone 440 Hz, -20 dBFS, to main outputs
            for (int i = 0; i < n; ++i) { const float v = 0.1f * std::sin(tonePh); tonePh += 2.f * 3.14159265f * 440.f / (float)fs; if (tonePh > 6.2831853f) tonePh -= 6.2831853f; for (int c = 0; c < std::min(nOut, 2); ++c) out[(mainPair * 2 + c) % nOut][i] = v; }
            testTone.fetch_sub(n); return; }
        engine.process(in, nIn, op, kNumOutputs, n);
        for (int c = 0; c < 2; ++c) if (mainPair * 2 + c < nOut) std::copy(op[c], op[c] + n, out[mainPair * 2 + c]);
        if (monitorPair >= 0) for (int c = 0; c < 2; ++c) if (monitorPair * 2 + c < nOut) std::copy(op[2 + c], op[2 + c] + n, out[monitorPair * 2 + c]);
        virtualOut.push(op[0], op[1], n);
        if (captureCh >= 0 && captureCh < nIn) for (int i = 0; i < n; ++i) { capture[(size_t)(capPos % (juce::int64)capture.size())] = in[captureCh][i]; ++capPos; }
    }
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if (m.getRawDataSize() < 3) return; const auto* d = m.getRawData(); int s = d[0], a = d[1], b = d[2];
        juce::MessageManager::callAsync([this, s, a, b] { if (midi.handle(s, a, b, controller, &engine) && onMidiActivity) onMidiActivity(); });
    }
    std::vector<float> captureRecent() const { std::vector<float> v; const int n = (int)capture.size(); const int have = (int)std::min<juce::int64>(capPos, n); v.resize((size_t)have); for (int i = 0; i < have; ++i) v[(size_t)i] = capture[(size_t)((capPos - have + i) % n)]; return v; }
    void playTestTone() { testTone = (int)(fs * 2.0); }

    juce::AudioDeviceManager dm; MixerEngine engine; MixerController controller; MidiMapper midi; VirtualOutput virtualOut;
    double fs = 48000; int block = 512, inLat = 0, outLat = 0, mainPair = 0, monitorPair = -1, captureCh = 0; bool firstRun = false, running = false; std::function<void()> onMidiActivity;
    juce::int64 lastXr = 0;
private:
    std::array<std::vector<float>, kNumOutputs> outBuf; std::vector<float> capture; std::atomic<int> testTone{0}; float tonePh = 0; juce::int64 capPos = 0;
};

} // namespace slp
