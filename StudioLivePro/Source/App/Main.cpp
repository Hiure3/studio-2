// Studio Live Pro - JUCE application (UI): mixer, master/metering, DSP editor, device manager, wizard, scenes, MIDI, AI Studio Magic
#include "Host.h"
using namespace slp;

namespace {
const juce::Colour kBg(0xff14161a), kPanel(0xff1d2026), kPanel2(0xff252a32), kAccent(0xff3fb6ff), kGood(0xff4cd964), kWarn(0xffffcc00), kBad(0xffff453a), kText(0xffd8dde6);

struct LevelBar : juce::Component {
    float db = -120.f, hold = -120.f; float minDb = -60.f;
    void set(float v) { db = v; hold = std::max(v, hold - 0.6f); repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); g.setColour(juce::Colours::black); g.fillRect(r);
        auto pos = [&](float d) { return juce::jlimit(0.f, 1.f, (d - minDb) / (0.f - minDb)); };
        const float h = r.getHeight() * pos(db); g.setGradientFill(juce::ColourGradient(kGood, 0, r.getBottom(), kBad, 0, r.getY(), false)); g.fillRect(r.getX(), r.getBottom() - h, r.getWidth(), h);
        g.setColour(juce::Colours::white); g.fillRect(r.getX(), r.getBottom() - r.getHeight() * pos(hold), r.getWidth(), 1.5f);
    }
};

// Labeled parameter slider bound to a float reference inside the controller model.
struct Param : juce::Component {
    juce::Slider s; juce::Label l; std::function<float()> get; std::function<void(float)> set;
    Param(const juce::String& name, double lo, double hi, double step, std::function<float()> g, std::function<void(float)> st, const juce::String& suffix = {}) : get(std::move(g)), set(std::move(st)) {
        l.setText(name, juce::dontSendNotification); l.setFont(juce::FontOptions(11.f)); l.setJustificationType(juce::Justification::centred); addAndMakeVisible(l);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14); s.setRange(lo, hi, step); s.setTextValueSuffix(suffix); addAndMakeVisible(s);
        s.onValueChange = [this] { if (!refreshing) set((float)s.getValue()); };
    }
    void refresh() { refreshing = true; s.setValue(get(), juce::dontSendNotification); refreshing = false; }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromTop(14)); s.setBounds(r); }
    bool refreshing = false;
};

struct SpectrumView : juce::Component {
    SpectrumAnalyzer* an = nullptr; std::vector<float> sp, pk; SpectrumView() { sp.assign(SpectrumAnalyzer::kBins, -120.f); pk = sp; }
    void update() { if (!an) return; an->getSpectrum(sp.data()); an->getPeakHold(pk.data()); repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); g.setColour(juce::Colours::black); g.fillRect(r); g.setColour(juce::Colour(0xff2a2f38));
        for (float f : {100.f, 1000.f, 10000.f}) { const float x = r.getX() + r.getWidth() * std::log10(f / 20.f) / 3.f; g.drawVerticalLine((int)x, r.getY(), r.getBottom()); }
        auto X = [&](int k) { const float f = (float)(an ? an->binHz(k) : k * 11.7); return r.getX() + r.getWidth() * juce::jlimit(0.f, 1.f, std::log10(std::max(f, 20.f) / 20.f) / 3.f); };
        auto Y = [&](float d) { return r.getBottom() - r.getHeight() * juce::jlimit(0.f, 1.f, (d + 100.f) / 100.f); };
        juce::Path p, q; bool first = true; for (int k = 2; k < SpectrumAnalyzer::kBins; ++k) { if (first) { p.startNewSubPath(X(k), Y(sp[(size_t)k])); q.startNewSubPath(X(k), Y(pk[(size_t)k])); first = false; } else { p.lineTo(X(k), Y(sp[(size_t)k])); q.lineTo(X(k), Y(pk[(size_t)k])); } }
        g.setColour(kAccent.withAlpha(0.9f)); g.strokePath(p, juce::PathStrokeType(1.2f)); g.setColour(kWarn.withAlpha(0.5f)); g.strokePath(q, juce::PathStrokeType(0.8f));
        g.setColour(kText.withAlpha(0.6f)); g.setFont(10.f); g.drawText("100 Hz", (int)(r.getX() + r.getWidth() * 0.233f) + 2, 2, 50, 12, juce::Justification::left); g.drawText("1 kHz", (int)(r.getX() + r.getWidth() * 0.566f) + 2, 2, 50, 12, juce::Justification::left); g.drawText("10 kHz", (int)(r.getX() + r.getWidth() * 0.9f) - 40, 2, 50, 12, juce::Justification::left);
    }
};

struct StripUI : juce::Component {
    Host& host; int idx; juce::Label name; juce::Slider fader, pan; juce::TextButton mute{"M"}, solo{"S"}, eq{"EQ"}, comp{"CMP"}, sel{"edit"}; LevelBar meter; std::function<void(int)> onSelect; bool selected = false;
    StripUI(Host& h, int i) : host(h), idx(i) {
        name.setText(h.controller.ch[(size_t)i].name, juce::dontSendNotification); name.setEditable(false, true); name.setJustificationType(juce::Justification::centred); addAndMakeVisible(name);
        name.onTextChange = [this] { host.controller.beginEdit(); std::snprintf(host.controller.ch[(size_t)idx].name, 24, "%s", name.getText().toRawUTF8()); host.controller.commitChannel(idx); };
        fader.setSliderStyle(juce::Slider::LinearVertical); fader.setRange(-100.0, 6.0, 0.1); fader.setSkewFactorFromMidPoint(-12.0); fader.setValue(0, juce::dontSendNotification); fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14); fader.setDoubleClickReturnValue(true, 0.0);
        fader.onValueChange = [this] { auto& c = host.controller.ch[(size_t)idx]; c.faderDb = (float)fader.getValue(); host.controller.commitChannel(idx); };
        fader.onDragStart = [this] { host.controller.beginEdit(); };
        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); pan.setRange(-1.0, 1.0, 0.01); pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); pan.setDoubleClickReturnValue(true, 0.0);
        pan.onValueChange = [this] { host.controller.ch[(size_t)idx].pan = (float)pan.getValue(); host.controller.commitChannel(idx); }; pan.onDragStart = [this] { host.controller.beginEdit(); };
        for (auto* b : {&mute, &solo, &eq, &comp, &sel}) { b->setClickingTogglesState(b != &sel); addAndMakeVisible(*b); }
        mute.setColour(juce::TextButton::buttonOnColourId, kBad); solo.setColour(juce::TextButton::buttonOnColourId, kWarn); eq.setColour(juce::TextButton::buttonOnColourId, kAccent); comp.setColour(juce::TextButton::buttonOnColourId, kAccent);
        mute.onClick = [this] { host.controller.beginEdit(); host.controller.ch[(size_t)idx].mute = mute.getToggleState(); host.controller.commitChannel(idx); };
        solo.onClick = [this] { host.controller.beginEdit(); host.controller.ch[(size_t)idx].solo = solo.getToggleState(); host.controller.commitChannel(idx); };
        eq.onClick = [this] { host.controller.beginEdit(); host.controller.ch[(size_t)idx].eqOn = eq.getToggleState(); host.controller.commitChannel(idx); };
        comp.onClick = [this] { host.controller.beginEdit(); host.controller.ch[(size_t)idx].compOn = comp.getToggleState(); host.controller.commitChannel(idx); };
        sel.onClick = [this] { if (onSelect) onSelect(idx); };
        addAndMakeVisible(fader); addAndMakeVisible(pan); addAndMakeVisible(meter);
    }
    void syncFromModel() { auto& c = host.controller.ch[(size_t)idx]; name.setText(c.name, juce::dontSendNotification); fader.setValue(c.faderDb, juce::dontSendNotification); pan.setValue(c.pan, juce::dontSendNotification); mute.setToggleState(c.mute, juce::dontSendNotification); solo.setToggleState(c.solo, juce::dontSendNotification); eq.setToggleState(c.eqOn, juce::dontSendNotification); comp.setToggleState(c.compOn, juce::dontSendNotification); }
    void tick() { meter.set(host.engine.meters.chPeakDb[idx].load()); }
    void paint(juce::Graphics& g) override { g.setColour(selected ? kPanel2.brighter(0.2f) : kPanel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.f); if (selected) { g.setColour(kAccent); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.f, 1.5f); } }
    void resized() override {
        auto r = getLocalBounds().reduced(4); name.setBounds(r.removeFromTop(18)); auto row = r.removeFromTop(22); mute.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(1)); solo.setBounds(row.reduced(1));
        row = r.removeFromTop(22); eq.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(1)); comp.setBounds(row.reduced(1)); sel.setBounds(r.removeFromTop(22).reduced(1)); pan.setBounds(r.removeFromTop(36).reduced(8, 0));
        meter.setBounds(r.removeFromRight(10).reduced(0, 6)); fader.setBounds(r);
    }
};

struct MasterUI : juce::Component {
    Host& host; LevelBar l, r; juce::Slider fader, ceiling, drive; juce::Label lufs, info; juce::TextButton lim{"LIMITER"}, glue{"GLUE"}, clip{"CLIP"}, mono{"MONO"}, dim{"DIM"}, bypass{"EMERGENCY BYPASS"}, safe{"SAFE MODE"};
    MasterUI(Host& h) : host(h) {
        addAndMakeVisible(l); addAndMakeVisible(r); fader.setSliderStyle(juce::Slider::LinearVertical); fader.setRange(-100, 6, 0.1); fader.setSkewFactorFromMidPoint(-12); fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14); fader.setDoubleClickReturnValue(true, 0.0);
        fader.onValueChange = [this] { host.controller.master.faderDb = (float)fader.getValue(); host.controller.commitMaster(); }; fader.onDragStart = [this] { host.controller.beginEdit(); }; addAndMakeVisible(fader);
        ceiling.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); ceiling.setRange(-6, 0, 0.1); ceiling.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14); ceiling.setTextValueSuffix(" dBTP"); ceiling.setValue(-1, juce::dontSendNotification);
        ceiling.onValueChange = [this] { host.controller.master.lim.ceilingDb = (float)ceiling.getValue(); host.controller.commitMaster(); }; addAndMakeVisible(ceiling);
        drive.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); drive.setRange(0, 18, 0.1); drive.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14); drive.setTextValueSuffix(" dB gain"); drive.setValue(2, juce::dontSendNotification);
        drive.onValueChange = [this] { host.controller.master.lim.thresholdDb = -(float)drive.getValue(); host.controller.commitMaster(); }; addAndMakeVisible(drive);
        for (auto* b : {&lim, &glue, &clip, &mono, &dim, &bypass, &safe}) { b->setClickingTogglesState(true); addAndMakeVisible(*b); }
        lim.setToggleState(true, juce::dontSendNotification); safe.setToggleState(true, juce::dontSendNotification); bypass.setColour(juce::TextButton::buttonOnColourId, kBad);
        lim.onClick = [this] { host.controller.master.limOn = lim.getToggleState(); host.controller.commitMaster(); }; glue.onClick = [this] { host.controller.master.glueOn = glue.getToggleState(); host.controller.commitMaster(); };
        clip.onClick = [this] { host.controller.master.clipOn = clip.getToggleState(); host.controller.commitMaster(); }; mono.onClick = [this] { host.controller.master.monitor.mono = mono.getToggleState(); host.controller.commitMaster(); };
        dim.onClick = [this] { host.controller.master.monitor.dim = dim.getToggleState(); host.controller.commitMaster(); };
        bypass.onClick = [this] { host.engine.setEmergencyBypass(bypass.getToggleState()); logLine(bypass.getToggleState() ? "EMERGENCY BYPASS ON" : "Emergency bypass off"); };
        safe.onClick = [this] { host.engine.setSafeMode(safe.getToggleState()); };
        lufs.setFont(juce::FontOptions(13.f)); lufs.setJustificationType(juce::Justification::topLeft); addAndMakeVisible(lufs);
    }
    void tick() { auto& m = host.engine.meters; l.set(m.masterPeakDb[0]); r.set(m.masterPeakDb[1]);
        lufs.setText(juce::String::formatted("LUFS  M %.1f   S %.1f   I %.1f\nTrue Peak %.1f dBTP   Peak L/R %.1f / %.1f\nLRA %.1f LU   Crest %.1f dB\nCorrelation %+.2f   Width %.0f%%   Mono %s\nLimiter GR %.1f dB   Glue GR %.1f dB\nSafe level %d   Overloads %ld",
            m.lufsM.load(), m.lufsS.load(), m.lufsI.load(), m.truePeakDb.load(), m.masterPeakDb[0].load(), m.masterPeakDb[1].load(), m.lra.load(), m.crest.load(), m.correlation.load(), m.width.load() * 100.f, m.monoLossDb.load() > -3.f ? "OK" : "WARNING", m.limGr.load(), m.glueGr.load(), m.safeLevel.load(), m.overloads.load()), juce::dontSendNotification); }
    void syncFromModel() { fader.setValue(host.controller.master.faderDb, juce::dontSendNotification); ceiling.setValue(host.controller.master.lim.ceilingDb, juce::dontSendNotification); drive.setValue(-host.controller.master.lim.thresholdDb, juce::dontSendNotification); lim.setToggleState(host.controller.master.limOn, juce::dontSendNotification); glue.setToggleState(host.controller.master.glueOn, juce::dontSendNotification); clip.setToggleState(host.controller.master.clipOn, juce::dontSendNotification); }
    void paint(juce::Graphics& g) override { g.setColour(kPanel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.f); g.setColour(kText); g.setFont(14.f); g.drawText("MASTER", 8, 4, 100, 18, juce::Justification::left); }
    void resized() override {
        auto a = getLocalBounds().reduced(6); a.removeFromTop(20); auto top = a.removeFromTop(120); auto meters = top.removeFromLeft(34); l.setBounds(meters.removeFromLeft(14)); meters.removeFromLeft(4); r.setBounds(meters.removeFromLeft(14)); fader.setBounds(top);
        lufs.setBounds(a.removeFromTop(110)); auto k = a.removeFromTop(80); ceiling.setBounds(k.removeFromLeft(k.getWidth() / 2)); drive.setBounds(k);
        auto b1 = a.removeFromTop(24); lim.setBounds(b1.removeFromLeft(b1.getWidth() / 3).reduced(1)); glue.setBounds(b1.removeFromLeft(b1.getWidth() / 2).reduced(1)); clip.setBounds(b1.reduced(1));
        auto b2 = a.removeFromTop(24); mono.setBounds(b2.removeFromLeft(b2.getWidth() / 2).reduced(1)); dim.setBounds(b2.reduced(1)); bypass.setBounds(a.removeFromTop(28).reduced(1)); safe.setBounds(a.removeFromTop(24).reduced(1));
    }
};

// Bottom DSP editor: Pro mode (all parameters of the selected channel) / Easy mode (6 musical controls)
struct EditorUI : juce::Component {
    Host& host; int ch = 0; bool easy = false; juce::OwnedArray<Param> params; juce::Label title;
    EditorUI(Host& h) : host(h) { addAndMakeVisible(title); title.setFont(juce::FontOptions(14.f)); }
    ChannelState& st() { return host.controller.ch[(size_t)ch]; }
    template <class G, class S> void add(const juce::String& n, double lo, double hi, double step, G g, S s, const juce::String& suf = {}) {
        auto* p = new Param(n, lo, hi, step, g, [this, s](float v) { host.controller.beginEdit(); s(v); host.controller.commitChannel(ch); }, suf); params.add(p); addAndMakeVisible(p); p->refresh(); }
    void rebuild(int channel, bool easyMode) {
        ch = channel; easy = easyMode; params.clear(); title.setText("Channel " + juce::String(ch + 1) + " - " + juce::String(st().name) + (easy ? "  (Easy Mode)" : "  (Pro Mode)"), juce::dontSendNotification);
        auto& s = st();
        if (easy) {   // musical macros (use reserved EQ bands 20..23 so they never clash with the user's bands)
            add("Clarity", 0, 100, 1, [&s] { return s.eq.band[20].enabled ? s.eq.band[20].gainDb * 25.f : 0.f; }, [this](float v) { auto& b = st().eq.band[20]; b = {v > 0, FilterType::Bell, 3500.f, v / 25.f, 1.f, 12}; auto& c = st().eq.band[21]; c = {v > 0, FilterType::Bell, 320.f, -v / 50.f, 1.2f, 12}; st().eqOn = true; }, " %");
            add("Warmth", 0, 100, 1, [&s] { return s.eq.band[22].enabled ? s.eq.band[22].gainDb * 25.f : 0.f; }, [this](float v) { st().eq.band[22] = {v > 0, FilterType::LowShelf, 180.f, v / 25.f, 0.7f, 12}; st().satOn = v > 0; st().sat.model = SatModel::Tube; st().sat.driveDb = v / 12.f; st().sat.mix = 0.4f; st().eqOn = true; }, " %");
            add("Punch", 0, 100, 1, [&s] { return s.trans.attack * 100.f; }, [this](float v) { st().transOn = v > 0; st().trans.attack = v / 100.f * 0.6f; st().compOn = v > 0 || st().compOn; if (v > 0) { st().comp.ratio = 3.f + v / 50.f; } }, " %");
            add("Presence", 0, 100, 1, [&s] { return s.eq.band[23].enabled ? s.eq.band[23].gainDb * 25.f : 0.f; }, [this](float v) { st().eq.band[23] = {v > 0, FilterType::HighShelf, 7000.f, v / 25.f, 0.7f, 12}; st().eqOn = true; }, " %");
            add("Space", 0, 100, 1, [&s] { return s.fxDb[0] > -99.f ? (s.fxDb[0] + 40.f) * 100.f / 40.f : 0.f; }, [this](float v) { st().fxDb[0] = v <= 0 ? -100.f : -40.f + v * 0.4f; }, " %");
            add("Loudness", 0, 100, 1, [this] { return -host.controller.master.lim.thresholdDb * 100.f / 12.f; }, [this](float v) { host.controller.master.lim.thresholdDb = -v * 12.f / 100.f; host.controller.commitMaster(); }, " %");
        } else {
            add("Input gain", -24, 24, 0.1, [&s] { return s.inputGainDb; }, [this](float v) { st().inputGainDb = v; }, " dB"); add("HPF", 0, 400, 1, [&s] { return s.hpfHz; }, [this](float v) { st().hpfHz = v; }, " Hz");
            for (int b = 0; b < 4; ++b) { add("EQ" + juce::String(b + 1) + " Hz", 20, 20000, 1, [&s, b] { return s.eq.band[(size_t)b].freqHz; }, [this, b](float v) { auto& x = st().eq.band[(size_t)b]; x.freqHz = v; x.enabled = true; st().eqOn = true; });
                add("EQ" + juce::String(b + 1) + " dB", -15, 15, 0.1, [&s, b] { return s.eq.band[(size_t)b].gainDb; }, [this, b](float v) { auto& x = st().eq.band[(size_t)b]; x.gainDb = v; x.enabled = true; st().eqOn = true; });
                add("EQ" + juce::String(b + 1) + " Q", 0.1, 10, 0.01, [&s, b] { return s.eq.band[(size_t)b].q; }, [this, b](float v) { st().eq.band[(size_t)b].q = v; }); }
            add("Gate thr", -80, 0, 0.5, [&s] { return s.gate.thresholdDb; }, [this](float v) { st().gate.thresholdDb = v; st().gateOn = v > -79.f; }, " dB");
            add("Comp thr", -60, 0, 0.5, [&s] { return s.comp.thresholdDb; }, [this](float v) { st().comp.thresholdDb = v; st().compOn = true; }, " dB"); add("Ratio", 1, 20, 0.1, [&s] { return s.comp.ratio; }, [this](float v) { st().comp.ratio = v; });
            add("Attack", 0.1, 200, 0.1, [&s] { return s.comp.attackMs; }, [this](float v) { st().comp.attackMs = v; }, " ms"); add("Release", 10, 2000, 1, [&s] { return s.comp.releaseMs; }, [this](float v) { st().comp.releaseMs = v; }, " ms");
            add("Makeup", -12, 24, 0.1, [&s] { return s.comp.makeupDb; }, [this](float v) { st().comp.makeupDb = v; }, " dB"); add("De-ess thr", -60, 0, 0.5, [&s] { return s.deess.thresholdDb; }, [this](float v) { st().deess.thresholdDb = v; st().deessOn = true; }, " dB");
            add("Sat drive", 0, 24, 0.1, [&s] { return s.sat.driveDb; }, [this](float v) { st().sat.driveDb = v; st().satOn = v > 0.f; }, " dB"); add("Attack+", -1, 1, 0.01, [&s] { return s.trans.attack; }, [this](float v) { st().trans.attack = v; st().transOn = true; });
            add("Sustain", -1, 1, 0.01, [&s] { return s.trans.sustain; }, [this](float v) { st().trans.sustain = v; st().transOn = true; }); add("Width", 0, 2, 0.01, [&s] { return s.width; }, [this](float v) { st().width = v; });
            for (int a = 0; a < 2; ++a) add(a ? "Aux1 send" : "Rev send", -60, 6, 0.5, [&s, a] { return a ? s.auxDb[0] : s.fxDb[0]; }, [this, a](float v) { (a ? st().auxDb[0] : st().fxDb[0]) = v <= -59.f ? -100.f : v; }, " dB");
            add("Delay send", -60, 6, 0.5, [&s] { return s.fxDb[1]; }, [this](float v) { st().fxDb[1] = v <= -59.f ? -100.f : v; }, " dB");
        }
        resized();
    }
    void resized() override { auto r = getLocalBounds().reduced(4); title.setBounds(r.removeFromTop(18)); const int w = 70, per = std::max(1, r.getWidth() / w); int i = 0; for (auto* p : params) { p->setBounds(r.getX() + (i % per) * w, r.getY() + (i / per) * 76, w - 2, 74); ++i; } }
    void paint(juce::Graphics& g) override { g.setColour(kPanel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4.f); }
};

struct LogoText { static juce::String slogan() { return "Som de estudio. Em tempo real."; } };

// First-run wizard (7 steps)
struct WizardComponent : juce::Component {
    Host& host; int step = 0; juce::Label title, body; juce::ComboBox c1, c2; juce::TextButton next{"Next"}, back{"Back"}, test{"Play test tone"}; std::function<void()> onDone; juce::ToggleButton stream{"Enable virtual output for OBS"};
    WizardComponent(Host& h) : host(h) {
        title.setFont(juce::FontOptions(22.f)); body.setJustificationType(juce::Justification::topLeft); for (auto* c : {(juce::Component*)&title, (juce::Component*)&body, (juce::Component*)&c1, (juce::Component*)&c2, (juce::Component*)&next, (juce::Component*)&back, (juce::Component*)&test, (juce::Component*)&stream}) addAndMakeVisible(c);
        next.onClick = [this] { applyStep(); if (step == 6) { host.firstRun = false; host.saveSettings(); if (onDone) onDone(); return; } ++step; show(); }; back.onClick = [this] { if (step > 0) { --step; show(); } }; test.onClick = [this] { host.playTestTone(); };
        c1.onChange = [this] { if (step == 0) { host.dm.setCurrentAudioDeviceType(c1.getText(), true); } }; show(); setSize(560, 340);
    }
    juce::AudioDeviceManager::AudioDeviceSetup setup() { juce::AudioDeviceManager::AudioDeviceSetup s; host.dm.getAudioDeviceSetup(s); return s; }
    void applyStep() {
        auto s = setup(); if (step == 1) { s.outputDeviceName = c1.getText(); s.inputDeviceName = c2.getText(); host.dm.setAudioDeviceSetup(s, true); }
        else if (step == 2) { s.sampleRate = c1.getText().getDoubleValue(); host.dm.setAudioDeviceSetup(s, true); } else if (step == 3) { s.bufferSize = c1.getText().getIntValue(); host.dm.setAudioDeviceSetup(s, true); }
        else if (step == 5) { host.mainPair = std::max(0, c1.getSelectedId() - 1); host.monitorPair = c2.getSelectedId() > 1 ? c2.getSelectedId() - 2 : -1; }
        else if (step == 6 && stream.getToggleState() && c1.getText().isNotEmpty()) host.virtualOut.open(c2.getText(), c1.getText(), host.fs);
    }
    void fill(juce::ComboBox& c, const juce::StringArray& items, const juce::String& sel) { c.clear(juce::dontSendNotification); c.addItemList(items, 1); c.setText(sel, juce::dontSendNotification); if (c.getSelectedId() == 0 && items.size()) c.setSelectedId(1, juce::dontSendNotification); }
    void show() {
        auto* dev = host.dm.getCurrentAudioDevice(); auto s = setup(); c2.setVisible(false); test.setVisible(false); stream.setVisible(false); c1.setVisible(true); back.setEnabled(step > 0); next.setButtonText(step == 6 ? "Finish" : "Next");
        const char* titles[] = {"1. Select driver", "2. Select audio interface", "3. Sample rate", "4. Buffer size", "5. Test audio", "6. Output configuration", "7. Streaming (optional)"}; title.setText(juce::String("Welcome to Studio Live Pro  -  ") + titles[step], juce::dontSendNotification);
        juce::StringArray types; for (auto* t : host.dm.getAvailableDeviceTypes()) types.add(t->getTypeName());
        switch (step) {
        case 0: body.setText("Choose the audio driver. ASIO gives the lowest latency with professional interfaces; WASAPI works with any Windows sound card.", juce::dontSendNotification); fill(c1, types, host.dm.getCurrentAudioDeviceType()); break;
        case 1: { body.setText("Select input (left) and output (right) devices.", juce::dontSendNotification); juce::StringArray ins, outs; if (auto* t = host.dm.getCurrentDeviceTypeObject()) { ins = t->getDeviceNames(true); outs = t->getDeviceNames(false); } fill(c2, ins, s.inputDeviceName); fill(c1, outs, s.outputDeviceName); c2.setVisible(true); break; }
        case 2: { body.setText("Pick the sample rate (48 kHz recommended for streaming).", juce::dontSendNotification); juce::StringArray a; if (dev) for (double r : dev->getAvailableSampleRates()) a.add(juce::String(r, 0)); fill(c1, a, juce::String(s.sampleRate, 0)); break; }
        case 3: { body.setText("Pick the buffer size. Smaller = lower latency but more CPU load (128-256 is a good start).", juce::dontSendNotification); juce::StringArray a; if (dev) for (int b : dev->getAvailableBufferSizes()) a.add(juce::String(b)); fill(c1, a, juce::String(s.bufferSize)); break; }
        case 4: body.setText("Press the button: a 440 Hz tone is played on the main outputs for 2 seconds. If you hear it, continue.", juce::dontSendNotification); c1.setVisible(false); test.setVisible(true); break;
        case 5: { body.setText("Main output pair (left) and monitor/headphone pair (right).", juce::dontSendNotification); juce::StringArray pairs, mons{"No monitor output"}; const int nOut = dev ? dev->getActiveOutputChannels().countNumberOfSetBits() : 2; for (int i = 0; i + 1 < std::max(nOut, 2); i += 2) { pairs.add("Outputs " + juce::String(i + 1) + "-" + juce::String(i + 2)); mons.add("Outputs " + juce::String(i + 1) + "-" + juce::String(i + 2)); } fill(c1, pairs, pairs[0]); fill(c2, mons, mons[0]); c2.setVisible(true); break; }
        case 6: { body.setText("Optional: send the master to a virtual audio cable (e.g. VB-Cable) and use it as an audio source in OBS Studio.", juce::dontSendNotification); juce::StringArray outs; if (auto* t = host.dm.getCurrentDeviceTypeObject()) outs = t->getDeviceNames(false); fill(c1, outs, ""); c2.clear(); c2.addItem("Windows Audio", 1); c2.setSelectedId(1); c2.setVisible(true); stream.setVisible(true); break; }
        }
        resized();
    }
    void resized() override { auto r = getLocalBounds().reduced(14); title.setBounds(r.removeFromTop(34)); body.setBounds(r.removeFromTop(70)); c1.setBounds(r.removeFromTop(28)); r.removeFromTop(6); c2.setBounds(r.removeFromTop(28)); r.removeFromTop(6); test.setBounds(r.removeFromTop(28).removeFromLeft(200)); stream.setBounds(r.removeFromTop(24)); auto b = getLocalBounds().reduced(14).removeFromBottom(30); next.setBounds(b.removeFromRight(100)); b.removeFromRight(8); back.setBounds(b.removeFromRight(100)); }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); g.setColour(kText); }
};

struct MainComponent : juce::Component, juce::Timer {
    Host host; juce::OwnedArray<StripUI> strips; MasterUI master{host}; EditorUI editor{host}; SpectrumView spectrum; juce::Viewport vp; juce::Component stripHolder;
    juce::TextButton setup{"Audio Setup"}, newP{"New"}, openP{"Open"}, saveP{"Save"}, saveAs{"Save As"}, undoB{"Undo"}, redoB{"Redo"}, modeB{"EASY / PRO"}, magic{"VOCAL MAGIC"}, learn{"Capture noise"}, snapS{"Snap +"}, snapL{"Snap <"}, abB{"A/B"}, beforeAfter{"Before/After"}, midiLearn{"MIDI learn"};
    juce::TextButton scene[5]; juce::ComboBox presetBox, masterPresetBox; juce::Label status, midiLbl; bool easyMode = false; int selected = 0; juce::File projectFile; std::shared_ptr<juce::FileChooser> chooser; std::vector<ChannelPreset> cpresets; std::vector<MasterPreset> mpresets;
    int lastSnap = -1; bool noiseCapturing = false, baOn = false;
    MainComponent() {
        host.startup(); cpresets = factoryChannelPresets(); mpresets = factoryMasterPresets();
        for (int i = 0; i < Host::kChannels; ++i) { auto* s = new StripUI(host, i); s->onSelect = [this](int c) { select(c); }; strips.add(s); stripHolder.addAndMakeVisible(s); }
        vp.setViewedComponent(&stripHolder, false); vp.setScrollBarsShown(false, true); addAndMakeVisible(vp); addAndMakeVisible(master); addAndMakeVisible(editor); spectrum.an = &host.engine.spectrum(); addAndMakeVisible(spectrum);
        for (auto* b : {&setup, &newP, &openP, &saveP, &saveAs, &undoB, &redoB, &modeB, &magic, &learn, &snapS, &snapL, &abB, &beforeAfter, &midiLearn}) addAndMakeVisible(*b);
        for (int i = 0; i < 5; ++i) { static const char* nm[] = {"1 Banda", "2 Pregacao", "3 Vocal", "4 Intervalo", "5 Encerr."}; scene[i].setButtonText(nm[i]); scene[i].onClick = [this, i] { if (juce::ModifierKeys::getCurrentModifiers().isShiftDown()) host.controller.saveScene(i, scene[i].getButtonText().toStdString()); else if (!host.controller.loadScene(i)) host.controller.saveScene(i, scene[i].getButtonText().toStdString()); syncAll(); }; addAndMakeVisible(scene[i]); }
        for (auto& p : cpresets) presetBox.addItem(p.category + ": " + p.name, presetBox.getNumItems() + 1); for (auto& p : mpresets) masterPresetBox.addItem(p.category + ": " + p.name, masterPresetBox.getNumItems() + 1);
        presetBox.setTextWhenNothingSelected("Channel preset..."); masterPresetBox.setTextWhenNothingSelected("Master / Live preset...");
        presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i < (int)cpresets.size()) { host.controller.beginEdit(); auto n = std::string(host.controller.ch[(size_t)selected].name); auto in = host.controller.ch[(size_t)selected].inputIndex; auto st = host.controller.ch[(size_t)selected].stereo; host.controller.ch[(size_t)selected] = ChannelState(); auto& c = host.controller.ch[(size_t)selected]; std::snprintf(c.name, 24, "%s", n.c_str()); c.inputIndex = in; c.stereo = st; cpresets[(size_t)i].apply(c); host.controller.commitChannel(selected); syncAll(); } };
        masterPresetBox.onChange = [this] { const int i = masterPresetBox.getSelectedId() - 1; if (i >= 0 && i < (int)mpresets.size()) { host.controller.beginEdit(); host.controller.master = MasterState(); mpresets[(size_t)i].apply(host.controller.master); host.controller.commitMaster(); syncAll(); } };
        setup.onClick = [this] { showSetup(); }; newP.onClick = [this] { resetProject(); };
        openP.onClick = [this] { chooser = std::make_shared<juce::FileChooser>("Open project", appDataDir(), "*.slpproj"); chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile() && host.controller.openProject(f.getFullPathName().toStdString())) { projectFile = f; syncAll(); logLine("Project opened: " + f.getFullPathName()); } }); };
        saveP.onClick = [this] { if (projectFile == juce::File()) saveAs.onClick(); else host.controller.saveProject(projectFile.getFullPathName().toStdString()); };
        saveAs.onClick = [this] { chooser = std::make_shared<juce::FileChooser>("Save project", appDataDir().getChildFile("project.slpproj"), "*.slpproj"); chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) { auto f = fc.getResult().withFileExtension("slpproj"); if (f != juce::File()) { host.controller.projectName = f.getFileNameWithoutExtension().toStdString(); if (host.controller.saveProject(f.getFullPathName().toStdString())) { projectFile = f; logLine("Project saved: " + f.getFullPathName()); } } }); };
        undoB.onClick = [this] { host.controller.undo(); syncAll(); }; redoB.onClick = [this] { host.controller.redo(); syncAll(); };
        modeB.onClick = [this] { easyMode = !easyMode; editor.rebuild(selected, easyMode); };
        snapS.onClick = [this] { lastSnap = host.controller.snapshotSave("Snapshot " + std::to_string(host.controller.snapshots.size() + 1)); }; snapL.onClick = [this] { if (lastSnap >= 0) { host.controller.snapshotLoad(lastSnap); syncAll(); } };
        abB.onClick = [this] { auto& c = host.controller; if (c.abActive() == 0 && !abInit) { c.abStore(0); c.abStore(1); abInit = true; c.abSwitch(0, host.engine.meters.lufsS, true); } const int to = 1 - c.abActive(); c.abSwitch(to, host.engine.meters.lufsS, true); syncAll(); };
        beforeAfter.onClick = [this] { baOn = !baOn; host.controller.setBypassAll(baOn); syncAll(); };
        learn.onClick = [this] { noiseCapturing = !noiseCapturing; host.engine.requestNoiseLearn(selected, noiseCapturing); if (!noiseCapturing) { auto& c = host.controller.ch[(size_t)selected]; c.noiseOn = true; c.noise.enabled = true; host.controller.commitChannel(selected); } learn.setButtonText(noiseCapturing ? "Stop capture" : "Capture noise"); };
        midiLearn.onClick = [this] { host.midi.startLearn({false, 0, 0, MidiTarget::ChannelFader, selected}); midiLbl.setText("Move a control on your MIDI device to bind it to the fader of channel " + juce::String(selected + 1), juce::dontSendNotification); };
        host.onMidiActivity = [this] { syncAll(); midiLbl.setText("MIDI activity", juce::dontSendNotification); };
        magic.onClick = [this] { runMagic(); };
        addAndMakeVisible(presetBox); addAndMakeVisible(masterPresetBox); addAndMakeVisible(status); addAndMakeVisible(midiLbl); status.setFont(juce::FontOptions(12.f));
        syncAll(); select(0); setSize(1280, 780); startTimerHz(30);
        if (host.firstRun) juce::MessageManager::callAsync([this] { showWizard(); });
    }
    ~MainComponent() override { stopTimer(); }
    bool abInit = false;
    void resetProject() { host.controller.ch.assign(Host::kChannels, ChannelState()); for (int i = 0; i < Host::kChannels; ++i) { host.controller.ch[(size_t)i].inputIndex = i; std::snprintf(host.controller.ch[(size_t)i].name, 24, "CH %d", i + 1); } host.controller.master = MasterState(); host.controller.projectName = "Untitled"; projectFile = juce::File(); host.controller.pushAll(); syncAll(); }
    void select(int c) { selected = c; for (auto* s : strips) { s->selected = s->idx == c; s->repaint(); } editor.rebuild(c, easyMode); }
    void syncAll() { for (auto* s : strips) s->syncFromModel(); master.syncFromModel(); editor.rebuild(selected, easyMode); }
    void showSetup() {
        auto* sel = new juce::AudioDeviceSelectorComponent(host.dm, 0, 32, 0, 8, true, true, true, false); sel->setSize(620, 460);
        juce::DialogWindow::LaunchOptions o; o.content.setOwned(sel); o.dialogTitle = "Device Manager - driver, interface, sample rate, buffer, channels"; o.dialogBackgroundColour = kBg; o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false; o.launchAsync();
    }
    void showWizard() { auto* w = new WizardComponent(host); juce::DialogWindow::LaunchOptions o; o.content.setOwned(w); o.dialogTitle = "Welcome to Studio Live Pro"; o.dialogBackgroundColour = kBg; o.useNativeTitleBar = true; auto* dw = o.launchAsync(); w->onDone = [this, dw] { if (dw) dw->exitModalState(0); juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Studio Live Pro", "Studio Live Pro is ready."); }; }
    void runMagic() {
        auto buf = host.captureRecent(); if ((int)buf.size() < (int)(host.fs * 3)) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "AI Studio Magic", "Need at least 3 seconds of signal. Play/sing into the selected channel and try again."); return; }
        host.captureCh = host.controller.ch[(size_t)selected].inputIndex; ChannelState base = host.controller.ch[(size_t)selected]; auto res = studioMagic(buf.data(), (int)buf.size(), host.fs, base, SourceType::Auto);
        juce::String t = "Source detected: " + juce::String(sourceName(res.source)) + "\n\nBEFORE: RMS " + juce::String(res.before.rmsDb, 1) + " dB, crest " + juce::String(res.before.crestDb, 1) + " dB, centroid " + juce::String(res.before.centroidHz, 0) + " Hz\nAFTER:  RMS " + juce::String(res.after.rmsDb, 1) + " dB, crest " + juce::String(res.after.crestDb, 1) + " dB, centroid " + juce::String(res.after.centroidHz, 0) + " Hz\n\n";
        if (res.changes.empty()) t += "No changes needed: the signal is already balanced.\n"; for (auto& c : res.changes) t += juce::String::fromUTF8(("- " + c.what + "\n    Por que? " + c.why + "\n").c_str());
        auto st = res.state; auto idx = selected;
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::InfoIcon, "AI STUDIO MAGIC", t, "Apply", "Cancel", this, juce::ModalCallbackFunction::create([this, st, idx](int r) { if (r == 1) { host.controller.beginEdit(); host.controller.ch[(size_t)idx] = st; host.controller.commitChannel(idx); syncAll(); } }));
    }
    void timerCallback() override {
        for (auto* s : strips) s->tick(); master.tick(); spectrum.update();
        if (auto* d = host.dm.getCurrentAudioDevice()) { const auto xr = d->getXRunCount(); if (xr > host.lastXr && xr > 0) { logLine("XRUN count=" + juce::String(xr)); host.lastXr = xr; }
            const double latMs = (host.inLat + host.outLat + (double)host.engine.masterLatencySamples() + host.block) * 1000.0 / host.fs; auto& m = host.engine.meters;
            status.setText(juce::String(d->getName()) + " [" + d->getTypeName() + "]  " + juce::String(host.fs, 0) + " Hz  buf " + juce::String(host.block) + "  latency in/out " + juce::String(host.inLat) + "/" + juce::String(host.outLat) + " smp, engine " + juce::String(host.engine.masterLatencySamples()) + " smp, ~" + juce::String(latMs, 1) + " ms total  |  CPU " + juce::String(m.cpuLoad.load() * 100.f, 0) + "%  DSP peak " + juce::String(m.dspLoad.load() * 100.f, 0) + "%  XRuns " + juce::String((int)xr) + "  Overloads " + juce::String((int)m.overloads.load()) + (host.virtualOut.active ? "  VirtOut underruns " + juce::String(host.virtualOut.underruns.load()) : juce::String()), juce::dontSendNotification);
        } else status.setText("No audio device - open Audio Setup", juce::dontSendNotification);
    }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); g.setColour(kAccent); g.setFont(juce::FontOptions(18.f, juce::Font::bold)); g.drawText("STUDIO LIVE PRO", 10, 4, 200, 22, juce::Justification::left); g.setColour(kText.withAlpha(0.6f)); g.setFont(11.f); g.drawText(LogoText::slogan(), 10, 24, 220, 14, juce::Justification::left); }
    void resized() override {
        auto r = getLocalBounds().reduced(6); auto top = r.removeFromTop(40); top.removeFromLeft(230); for (auto* b : {&setup, &newP, &openP, &saveP, &saveAs, &undoB, &redoB}) b->setBounds(top.removeFromLeft(72).reduced(2)); modeB.setBounds(top.removeFromLeft(90).reduced(2)); magic.setBounds(top.removeFromLeft(110).reduced(2)); learn.setBounds(top.removeFromLeft(110).reduced(2)); beforeAfter.setBounds(top.removeFromLeft(96).reduced(2)); abB.setBounds(top.removeFromLeft(50).reduced(2)); snapS.setBounds(top.removeFromLeft(60).reduced(2)); snapL.setBounds(top.removeFromLeft(60).reduced(2)); midiLearn.setBounds(top.removeFromLeft(90).reduced(2));
        status.setBounds(r.removeFromTop(18)); auto row = r.removeFromTop(28); for (auto& s : scene) s.setBounds(row.removeFromLeft(86).reduced(2)); presetBox.setBounds(row.removeFromLeft(240).reduced(2)); masterPresetBox.setBounds(row.removeFromLeft(240).reduced(2)); midiLbl.setBounds(row.reduced(2));
        auto bottom = r.removeFromBottom(250); auto right = r.removeFromRight(260); master.setBounds(right.removeFromTop(right.getHeight() - 150)); spectrum.setBounds(right.reduced(0, 2)); editor.setBounds(bottom.reduced(0, 2));
        vp.setBounds(r); const int w = 84; stripHolder.setSize(w * Host::kChannels, r.getHeight() - 14); for (int i = 0; i < strips.size(); ++i) strips[i]->setBounds(i * w, 0, w - 2, r.getHeight() - 14);
    }
};

struct MainWindow : juce::DocumentWindow {
    MainWindow() : DocumentWindow("Studio Live Pro", kBg, DocumentWindow::allButtons) { setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true); setResizable(true, true); centreWithSize(getWidth(), getHeight()); setVisible(true); }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

void crashHandler(void*) { logLine("CRASH:\n" + juce::SystemStats::getStackBacktrace()); if (auto* l = juce::Logger::getCurrentLogger()) l->writeToLog("Application terminated abnormally"); }

class App : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Studio Live Pro"; } const juce::String getApplicationVersion() override { return "1.0.0"; } bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { logger.reset(juce::FileLogger::createDateStampedLogger(logsDir().getFullPathName(), "StudioLivePro_", ".log", "Studio Live Pro 1.0.0")); juce::Logger::setCurrentLogger(logger.get()); juce::SystemStats::setApplicationCrashHandler(crashHandler); window = std::make_unique<MainWindow>(); }
    void shutdown() override { window.reset(); juce::Logger::setCurrentLogger(nullptr); logger.reset(); } void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> window; std::unique_ptr<juce::FileLogger> logger;
};
} // namespace

START_JUCE_APPLICATION(App)
