// Smoke test of the real JUCE layer: processor, sample loading, MIDI, parameters, state, editor.
// Usage: DaftMugSmokeTest <output-dir>     (writes screenshots of the UI as PNG files there)
#include <cstdio>

#include "PadExporter.h"
#include "ParameterIds.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "Collector.h"
#include "KitExporter.h"
#include "AiService.h"
#include "UI/ReverbSection.h"
#include "UI/ShortcutTextEditor.h"
#include <mutex>

using namespace dm;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do { if (! (cond)) { std::printf ("  FAIL  line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static bool waitForState (DaftMugProcessor& p, int pad, LoadState wanted, int timeoutMs = 8000)
{
    for (int waited = 0; waited < timeoutMs; waited += 20)
    {
        if (p.getPadMeta (pad).state == wanted) return true;
        pump (20);
    }
    return false;
}

static juce::File writeTestWav (const juce::File& dir, const juce::String& name, double sr, int seconds, int channels)
{
    auto file = dir.getChildFile (name);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (sr)
                                                                          .withNumChannels (channels)
                                                                          .withBitsPerSample (16));
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buf (channels, (int) (sr * seconds));
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (c, i, 0.8f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) sr));
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    return file;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daftmug_smoke");
    tmp.createDirectory();

    const auto wav = writeTestWav (tmp, "kick_heavy-01.wav", 44100.0, 1, 1);        // mono, 44.1 kHz
    const auto wav2 = writeTestWav (tmp, "snare_roll.wav", 48000.0, 1, 2);          // stereo, 48 kHz
    CHECK (wav.existsAsFile() && wav2.existsAsFile());

    std::printf ("Processor: loading, naming, MIDI\n");
    auto proc = std::make_unique<DaftMugProcessor>();
    proc->setRateAndBufferSizeDetails (48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    proc->loadSampleAsync (0, wav);
    CHECK (proc->getPadMeta (0).state == LoadState::Loading || proc->getPadMeta (0).state == LoadState::Loaded);
    CHECK (waitForState (*proc, 0, LoadState::Loaded));
    CHECK (proc->getPadMeta (0).name == "Kick heavy 01");   // auto-named from the cleaned file name

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    buffer.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 100);
    proc->processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 0, 100) < 1.0e-6f);        // silent before the note
    CHECK (buffer.getMagnitude (0, 200, 300) > 0.05f);      // sound after the MIDI note
    CHECK (proc->getEngine().isPadPlaying (0));

    std::printf ("Processor: pad independence + parameters\n");
    proc->loadSampleAsync (1, wav2);
    CHECK (waitForState (*proc, 1, LoadState::Loaded));
    CHECK (proc->getPadMeta (1).name == "Snare roll");
    proc->setPadName (1, "Ma caisse claire");
    CHECK (proc->getPadMeta (1).userRenamed);
    proc->loadSampleAsync (1, wav);                          // re-import: custom name is kept
    CHECK (waitForState (*proc, 1, LoadState::Loaded));
    CHECK (proc->getPadMeta (1).name == "Ma caisse claire");

    if (auto* g = proc->apvts.getParameter (ids::gain (1)))
        g->setValueNotifyingHost (g->convertTo0to1 (-12.0f));
    CHECK (std::abs (proc->apvts.getRawParameterValue (ids::gain (0))->load()) < 0.001f);   // pad 1 untouched
    CHECK (std::abs (proc->apvts.getRawParameterValue (ids::gain (1))->load() + 12.0f) < 0.01f);

    proc->triggerPad (1, 1.0f);                              // mouse path
    buffer.clear(); midi.clear();
    proc->processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 300, 100) > 0.05f);
    CHECK (proc->getEngine().padTriggerCount (1) == 1);

    if (argc > 2)   // optional real MP3 file produced by ffmpeg
    {
        std::printf ("Processor: MP3 import\n");
        proc->loadSampleAsync (5, juce::File (argv[2]));
        CHECK (waitForState (*proc, 5, LoadState::Loaded));
        const auto m = proc->getPadMeta (5);
        CHECK (m.sample != nullptr && m.sample->numFrames() > 40000);
        proc->triggerPad (5, 1.0f);
        buffer.clear(); midi.clear();
        proc->processBlock (buffer, midi);
        CHECK (buffer.getMagnitude (0, 300, 100) > 0.05f);
    }

    std::printf ("WAV export\n");
    {
        auto readBack = [&] (const juce::File& f, int& frames, double& rate, int& bits, bool& isFloat, float& peakValue)
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return false;
            frames = (int) r->lengthInSamples;
            rate = r->sampleRate;
            bits = (int) r->bitsPerSample;
            isFloat = r->usesFloatingPointData;
            juce::AudioBuffer<float> b ((int) r->numChannels, frames);
            r->read (&b, 0, frames, 0, true, true);
            peakValue = b.getMagnitude (0, frames);
            return r->numChannels == 2;
        };

        const auto sample = proc->getPadMeta (0).sample;
        const auto base = proc->readPadParams (0);
        int frames = 0, bits = 0; double rate = 0; bool isFloat = false; float pk = 0;

        const auto f24 = tmp.getChildFile ("export24.wav");
        auto res = exportPadToWav (sample, base, { 24, 0.0, false, {} }, f24);
        CHECK (res.wasOk());
        CHECK (readBack (f24, frames, rate, bits, isFloat, pk));
        CHECK (std::abs (frames - 44100) < 40 && std::abs (rate - 44100.0) < 0.5 && bits == 24 && ! isFloat);
        CHECK (pk > 0.75f && pk < 0.85f);                // plays at its own level, not boosted

        const auto fn = tmp.getChildFile ("export_norm.wav");
        CHECK (exportPadToWav (sample, base, { 16, 0.0, true, {} }, fn).wasOk());
        CHECK (readBack (fn, frames, rate, bits, isFloat, pk) && bits == 16 && pk > 0.97f && pk <= 1.0f);

        const auto ff = tmp.getChildFile ("export_f32_96k.wav");
        CHECK (exportPadToWav (sample, base, { 32, 96000.0, false, {} }, ff).wasOk());
        CHECK (readBack (ff, frames, rate, bits, isFloat, pk));
        CHECK (std::abs (frames - 96000) < 80 && std::abs (rate - 96000.0) < 0.5 && bits == 32 && isFloat);

        auto edited = base;                               // region 50..100 %, one octave up
        edited.startFraction = 0.5f; edited.pitchSemitones = 12.0f; edited.gainDb = -6.0f;
        const auto fe = tmp.getChildFile ("export_edit.wav");
        CHECK (exportPadToWav (sample, edited, { 24, 0.0, false, {} }, fe).wasOk());
        CHECK (readBack (fe, frames, rate, bits, isFloat, pk));
        CHECK (std::abs (frames - 11025) < 40);           // 0.5 s of audio at double speed = 0.25 s
        CHECK (pk > 0.35f && pk < 0.45f);                 // 0.8 at -6 dB

        {   // a delay adds its echoes after the sound: the file is longer, never cut
            auto echo = base;
            echo.fx.delayMix = 0.5f; echo.fx.delayMs = 300.0f; echo.fx.delayFeedback = 0.5f;
            const auto fd = tmp.getChildFile ("export_delay.wav");
            CHECK (exportPadToWav (sample, echo, { 24, 0.0, false, {} }, fd).wasOk());
            CHECK (readBack (fd, frames, rate, bits, isFloat, pk));
            CHECK (frames > 44100 + 20000);
        }
        CHECK (! exportPadToWav (sample, base, { 24, 0.0, false, {} }, tmp.getChildFile ("no_such_dir/x.wav")).wasOk());
        CHECK (! exportPadToWav ({}, base, { 24, 0.0, false, {} }, f24).wasOk());
    }

    std::printf ("Step 2a: host automation of live + new parameters\n");
    {
        proc->loadSampleAsync (7, wav);
        CHECK (waitForState (*proc, 7, LoadState::Loaded));
        auto setParam = [&] (const juce::String& id, float value)
        {
            if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            else CHECK (false);
        };
        setParam (ids::note (7), 80.0f);
        setParam (ids::attack (7), 50.0f);                    // 50 ms attack
        proc->prepareToPlay (48000.0, 512);

        buffer.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 80, (juce::uint8) 127), 0);
        proc->processBlock (buffer, midi);                    // 512 samples = 10.7 ms of a 50 ms attack
        const float early = buffer.getMagnitude (0, 400, 100);
        for (int b = 0; b < 8; ++b) { buffer.clear(); midi.clear(); proc->processBlock (buffer, midi); }   // ~ 94 ms later
        const float later = buffer.getMagnitude (0, 0, 512);
        CHECK (early < 0.3f && later > 0.5f);                 // the attack parameter really shapes the sound

        // live gain automation while the sound plays: smooth, no jump
        buffer.clear(); midi.clear(); proc->processBlock (buffer, midi);
        float last = buffer.getSample (0, 511);               // continue exactly where the sound is
        setParam (ids::gain (7), -30.0f);
        float maxStep = 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            buffer.clear(); midi.clear(); proc->processBlock (buffer, midi);
            for (int i = 0; i < 512; ++i) { maxStep = juce::jmax (maxStep, std::abs (buffer.getSample (0, i) - last)); last = buffer.getSample (0, i); }
        }
        CHECK (maxStep < 0.1f);

        setParam (ids::maxVoices, 5.0f);
        setParam (ids::stealPolicy, 2.0f);
        CHECK ((int) proc->apvts.getRawParameterValue (ids::maxVoices)->load() == 5);
        setParam (ids::reverse (7), 1.0f);
        CHECK (proc->readPadParams (7).reverse);
        CHECK (std::abs (proc->readPadParams (7).attackSeconds - 0.05f) < 1.0e-4f);
        if (auto* prm = proc->apvts.getParameter (ids::attack (7))) CHECK (prm->getCurrentValueAsText().contains ("50"));
    }

    std::printf ("Step 2b: EQ parameters through the host\n");
    {
        auto setParam = [&] (const juce::String& id, float value)
        {
            if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            else CHECK (false);
        };
        setParam (ids::eqBand (7, 2, "gain"), 12.0f);
        setParam (ids::eqBand (7, 2, "freq"), 3000.0f);
        setParam (ids::eqBand (7, 0, "type"), 3.0f);            // high-pass
        const auto eq = proc->readPadParams (7).eq;
        CHECK (eq.on);
        CHECK (std::abs (eq.band[2].gainDb - 12.0f) < 0.11f);
        CHECK (std::abs (eq.band[2].freq - 3000.0f) < 30.0f);
        CHECK (eq.band[0].type == dm::EqType::HighPass);
        if (auto* prm = proc->apvts.getParameter (ids::eqBand (7, 2, "freq"))) CHECK (prm->getCurrentValueAsText().contains ("k"));
        setParam (ids::eqOn (7), 0.0f);
        CHECK (! proc->readPadParams (7).eq.on);
        setParam (ids::eqOn (7), 1.0f);

        // dynamic EQ parameters of band 3
        setParam (ids::eqBand (7, 2, "dyn"), 1.0f);
        setParam (ids::eqBand (7, 2, "thr"), -30.0f);
        setParam (ids::eqBand (7, 2, "ratio"), 8.0f);
        setParam (ids::eqBand (7, 2, "att"), 5.0f);
        setParam (ids::eqBand (7, 2, "rel"), 250.0f);
        setParam (ids::eqBand (7, 2, "knee"), 10.0f);
        setParam (ids::eqBand (7, 2, "range"), 18.0f);
        const auto dq = proc->readPadParams (7).eq.band[2];
        CHECK (dq.dynMode == 1);
        CHECK (std::abs (dq.thresholdDb + 30.0f) < 0.11f && std::abs (dq.ratio - 8.0f) < 0.11f);
        CHECK (std::abs (dq.attackMs - 5.0f) < 0.15f && std::abs (dq.releaseMs - 250.0f) < 1.5f);
        CHECK (std::abs (dq.kneeDb - 10.0f) < 0.11f && std::abs (dq.rangeDb - 18.0f) < 0.11f);
        CHECK (proc->readPadParams (7).eq.band[1].dynMode == 0);      // other bands untouched
        buffer.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 80, (juce::uint8) 127), 0);
        proc->processBlock (buffer, midi);
        for (int b = 0; b < 20; ++b) { buffer.clear(); midi.clear(); proc->processBlock (buffer, midi); CHECK (std::isfinite (buffer.getMagnitude (0, 512))); }
        setParam (ids::eqBand (7, 2, "dyn"), 0.0f);

        // band solo: a listening aid, readable by the engine, not a parameter, never saved, ignored by the export
        CHECK (proc->readPadParams (7).eq.soloBand == -1);
        proc->setEqSolo (7, 2);
        CHECK (proc->readPadParams (7).eq.soloBand == 2 && proc->readPadParams (6).eq.soloBand == -1);
        for (int b = 0; b < 10; ++b) { buffer.clear(); midi.clear(); proc->processBlock (buffer, midi); CHECK (std::isfinite (buffer.getMagnitude (0, 512))); }
        {
            juce::MemoryBlock blk; proc->getStateInformation (blk);
            CHECK (! blk.toString().contains ("solo"));
        }
        proc->setEqSolo (7, -1);
    }

    std::printf ("Step 2c: effect parameters through the host\n");
    {
        auto setParam = [&] (const juce::String& id, float value)
        {
            if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            else CHECK (false);
        };
        setParam (ids::fx (7, "sat_mode"), 3.0f);
        setParam (ids::fx (7, "crush_bits"), 4.0f);
        setParam (ids::fx (7, "crush_mix"), 50.0f);
        setParam (ids::fx (7, "delay_time"), 500.0f);
        setParam (ids::fx (7, "delay_mix"), 25.0f);
        setParam (ids::fx (7, "delay_pp"), 1.0f);
        const auto fx = proc->readPadParams (7).fx;
        CHECK (fx.satMode == dm::SatMode::Tube);
        CHECK (std::abs (fx.crushBits - 4.0f) < 0.11f);
        CHECK (std::abs (fx.crushMix - 0.5f) < 0.01f);
        CHECK (std::abs (fx.delayMs - 500.0f) < 1.5f);
        CHECK (std::abs (fx.delayMix - 0.25f) < 0.01f);
        CHECK (fx.delayPingPong);
        CHECK (proc->readPadParams (6).fx.satMode == dm::SatMode::Off);   // other pads untouched
        if (auto* prm = proc->apvts.getParameter (ids::fx (7, "delay_time"))) CHECK (prm->getCurrentValueAsText().contains ("ms"));
        // restore defaults so later checks (state, screenshots) start from a clean pad
        for (auto k : { "sat_mode", "crush_mix", "delay_mix", "delay_pp" })
            if (auto* prm = proc->apvts.getParameter (ids::fx (7, k))) prm->setValueNotifyingHost (prm->getDefaultValue());
        // processing with the effects on must not crash and stays finite
        setParam (ids::fx (7, "delay_mix"), 40.0f);
        buffer.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 80, (juce::uint8) 127), 0);
        proc->processBlock (buffer, midi);
        for (int b = 0; b < 30; ++b) { buffer.clear(); midi.clear(); proc->processBlock (buffer, midi); CHECK (std::isfinite (buffer.getMagnitude (0, 512))); }
        setParam (ids::fx (7, "delay_mix"), 0.0f);
    }

    std::printf ("Step 2c: convolution reverb through the host\n");
    {
        // a synthetic impulse response written by the test itself (no IR is bundled with the project)
        const auto irFile = tmp.getChildFile ("test_ir.wav");
        {
            juce::WavAudioFormat fmt;
            irFile.deleteFile();
            std::unique_ptr<juce::OutputStream> os = irFile.createOutputStream();
            auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
            if (w != nullptr)
            {
                juce::AudioBuffer<float> ir (1, 14400);
                ir.clear();
                ir.setSample (0, 0, 1.0f);
                ir.setSample (0, 9600, 0.6f);
                w->writeFromAudioSampleBuffer (ir, 0, 14400);
            }
        }
        CHECK (irFile.existsAsFile());
        auto setParam = [&] (const juce::String& id, float value)
        {
            if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            else CHECK (false);
        };
        auto flush = [&] { midi.clear(); midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
                           for (int b = 0; b < 200; ++b) { buffer.clear(); proc->processBlock (buffer, midi); midi.clear(); } };
        auto renderSum = [&] {
            float sum = 0.0f;
            buffer.clear(); midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 80, (juce::uint8) 127), 0);
            proc->processBlock (buffer, midi);
            for (int b = 0; b < 60; ++b)
            {
                buffer.clear(); midi.clear(); proc->processBlock (buffer, midi);
                if (b >= 20) sum += buffer.getMagnitude (0, 0, 512);
            }
            return sum;
        };

        CHECK (proc->getReverbStatus (7).isEmpty());                     // type "Aucune": nothing to report
        flush();
        const float dry = renderSum();

        setParam (ids::rvType (7), 1.0f);                                // Plate: no Plate.wav anywhere -> clear message, no crash
        pump (300);
        CHECK (proc->getReverbStatus (7).contains ("Plate"));

        proc->setCustomIr (7, irFile);
        setParam (ids::rvType (7), 5.0f);                                // custom IR
        setParam (ids::rvWet (7), 100.0f);
        for (int i = 0; i < 40 && proc->getReverbStatus (7).isNotEmpty(); ++i) pump (100);
        CHECK (proc->getReverbStatus (7).isEmpty());
        CHECK (std::abs (proc->readPadParams (7).reverbWet - 1.0f) < 1.0e-3f);
        pump (300);
        flush();
        const float wet = renderSum();
        CHECK (std::abs (wet - dry) > 0.01f * dry);                       // the reverb really changes the sound

        setParam (ids::rvType (7), 0.0f);
        pump (300);
        flush();
        CHECK (std::abs (renderSum() - dry) < 1.0e-3f * (dry + 1.0f));    // back to the dry sound
        CHECK (proc->readPadParams (7).reverbWet < 1.0e-6f);
    }

    std::printf ("Mutate\n");
    {
        proc->loadSampleAsync (9, wav);
        CHECK (waitForState (*proc, 9, LoadState::Loaded));
        auto setP = [&] (const juce::String& id, float v) { if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); else CHECK (false); };
        auto getP = [&] (const juce::String& id) { return proc->apvts.getRawParameterValue (id)->load(); };
        setP (ids::pitch (9), 1.5f);
        setP (ids::pan (9), 0.1f);
        const auto before = proc->readMutateValues (9);
        CHECK (std::abs (before.pitchSemitones - 1.5f) < 0.02f && ! proc->padHasReverb (9));

        // preview: the engine sees the variant, the host-visible parameters do NOT change
        MutateValues v = before; v.pitchSemitones = 4.0f; v.pan = -0.5f; v.attackMs = 30.0f; v.eqGainDb[1] = 6.0f;
        proc->setPreviewMutation (9, v);
        const auto pp = proc->readPadParams (9);
        CHECK (std::abs (pp.pitchSemitones - 4.0f) < 1e-3f && std::abs (pp.pan + 0.5f) < 1e-3f);
        CHECK (std::abs (pp.attackSeconds - 0.03f) < 1e-4f && std::abs (pp.eq.band[1].gainDb - 6.0f) < 1e-3f);
        CHECK (std::abs (getP (ids::pitch (9)) - 1.5f) < 0.02f);                       // parameter untouched
        proc->clearPreviewMutation (9);
        CHECK (std::abs (proc->readPadParams (9).pitchSemitones - 1.5f) < 0.02f);

        // apply + undo (one level); the sample is not touched
        const auto sampleBefore = proc->getPadMeta (9).sample;
        proc->applyMutation (9, v);
        CHECK (std::abs (getP (ids::pitch (9)) - 4.0f) < 0.02f && std::abs (getP (ids::attack (9)) - 30.0f) < 0.2f);
        CHECK (std::abs (getP (ids::eqBand (9, 1, "gain")) - 6.0f) < 0.11f);
        CHECK (proc->canUndoMutation());
        proc->undoMutation();
        CHECK (std::abs (getP (ids::pitch (9)) - 1.5f) < 0.02f && std::abs (getP (ids::pan (9)) - 0.1f) < 0.02f);
        CHECK (std::abs (getP (ids::eqBand (9, 1, "gain"))) < 0.11f && ! proc->canUndoMutation());
        CHECK (proc->getPadMeta (9).sample == sampleBefore);

        // the panel itself (UI): variants are generated, variant 0 is the original, applying the chosen one writes parameters
        auto editor = std::make_unique<DaftMugEditor> (*proc);
        editor->setBounds (0, 0, 1180, 740);
        editor->openMutate (9);
        pump (100);
        CHECK (editor->isMutateOpen());
        if (auto* mp = editor->getMutatePanel())
        {
            CHECK (mp->numVariants() == 8);
            CHECK (std::abs (mp->getVariants()[0].pitchSemitones - 1.5f) < 0.02f);
            bool someDifferent = false;
            for (int i = 1; i < mp->numVariants(); ++i) someDifferent = someDifferent || std::abs (mp->getVariants()[(size_t) i].pitchSemitones - 1.5f) > 0.01f;
            CHECK (someDifferent);
            mp->audition (3);
            CHECK (std::abs (proc->readPadParams (9).pitchSemitones - mp->getVariants()[3].pitchSemitones) < 1e-3f);
            auto snapM = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat pngM;
            juce::FileOutputStream os (outDir.getChildFile ("mutate.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); pngM.writeImageToStream (snapM, os); }
            mp->choose (3);
            const float expected = mp->getVariants()[3].pitchSemitones;
            CHECK (mp->applyChosen().wasOk());
            CHECK (std::abs (getP (ids::pitch (9)) - expected) < 0.02f);
        }
        editor.reset();
        CHECK (std::abs (proc->readPadParams (9).pitchSemitones - getP (ids::pitch (9))) < 1e-3f);   // closing the panel clears the preview
        proc->undoMutation();
        proc->clearPad (9);
    }

    std::printf ("Daft Mug AI (fake Gemini server, no real network)\n");
    {
        using namespace ai;
        const juce::String key = "AIzaSyTESTKEY1234567890abcdefghijk";
        const auto settingsFile = tmp.getChildFile ("ai_settings").getChildFile ("settings.xml");
        settingsFile.getParentDirectory().deleteRecursively();

        struct Fake { std::mutex m; int status = 200; juce::String body; std::vector<HttpRequest> seen; };
        auto fake = std::make_shared<Fake>();
        Transport transport = [fake] (const HttpRequest& r)
        {
            std::lock_guard<std::mutex> lock (fake->m);
            fake->seen.push_back (r);
            HttpResponse o; o.status = fake->status; o.body = fake->body; return o;
        };
        auto setFake = [&] (int status, const juce::String& body) { std::lock_guard<std::mutex> lock (fake->m); fake->status = status; fake->body = body; fake->seen.clear(); };
        auto lastReq = [&] { std::lock_guard<std::mutex> lock (fake->m); return fake->seen.empty() ? HttpRequest() : fake->seen.back(); };
        auto numReq = [&] { std::lock_guard<std::mutex> lock (fake->m); return (int) fake->seen.size(); };

        proc->setAiForTesting (std::make_unique<AiService> (std::make_unique<MemorySecretStore>(), transport, settingsFile));
        auto& svc = proc->getAi();
        CHECK (! svc.isEnabled() && ! svc.hasKey());                                   // opt-in
        CHECK (! svc.setKey ("short").wasOk() && ! svc.setKey ("has a space inside 123456789").wasOk());
        CHECK (svc.setKey (key).wasOk() && svc.hasKey());

        Catalog cat (*proc, 3);
        CHECK (cat.find ("pitch") != nullptr && cat.find ("eq4_q") != nullptr && cat.find ("reverb_wet") != nullptr);
        CHECK (cat.find ("note") == nullptr && cat.find ("mode") == nullptr && cat.find ("start") == nullptr);   // notes, modes, regions are NOT reachable

        auto doAsk = [&] (Question q, juce::Result& r, juce::String& json)
        {
            bool done = false;
            svc.ask (q, cat, [&] (juce::Result rr, juce::String j) { r = rr; json = j; done = true; });
            for (int i = 0; i < 500 && ! done; ++i) pump (10);
            CHECK (done);
        };
        auto doTest = [&] (juce::Result& r, juce::StringArray& models)
        {
            bool done = false;
            svc.testConnection ([&] (juce::Result rr, juce::StringArray m) { r = rr; models = m; done = true; });
            for (int i = 0; i < 500 && ! done; ++i) pump (10);
            CHECK (done);
        };

        // disabled: refuses and never touches the network
        {
            setFake (200, "{}");
            juce::Result r = juce::Result::ok(); juce::String js;
            Question q; q.request = "plus lourd";
            doAsk (q, r, js);
            CHECK (r.failed() && numReq() == 0);
        }
        svc.setEnabled (true);

        // connection test: model list filtered to generation models; the key travels only as apiKey, never in the URL
        {
            setFake (200, R"({"models":[
                {"name":"models/gemini-flash-x","supportedGenerationMethods":["generateContent"]},
                {"name":"models/gemini-embedding-001","supportedGenerationMethods":["embedContent"]},
                {"name":"models/gemini-pro-x","supportedGenerationMethods":["generateContent","countTokens"]},
                {"name":"models/imagen-3","supportedGenerationMethods":["predict"]}]})");
            juce::Result r = juce::Result::fail ("x"); juce::StringArray models;
            doTest (r, models);
            CHECK (r.wasOk() && models.size() == 2 && models.contains ("gemini-flash-x") && models.contains ("gemini-pro-x"));
            CHECK (svc.getModel() == "gemini-flash-x");
            const auto req = lastReq();
            CHECK (req.method == "GET" && req.url.startsWith (AiService::kHost) && ! req.url.contains (key) && req.apiKey == key && req.body.isEmpty());
        }
        // errors never reveal the key
        {
            setFake (403, "{\"error\":{\"message\":\"API key " + key + " not valid\"}}");
            juce::Result r = juce::Result::ok(); juce::StringArray models;
            doTest (r, models);
            CHECK (r.failed() && ! r.getErrorMessage().contains (key) && r.getErrorMessage().contains ("403"));
            setFake (0, {});
            doTest (r, models);
            CHECK (r.failed() && r.getErrorMessage().contains ("Pas de r"));
            setFake (429, "{}");
            doTest (r, models);
            CHECK (r.failed() && r.getErrorMessage().contains ("429"));
            CHECK (AiService::redact ("x " + key + " y AIzaSyAAAAAAAAAAAAAAAAAAAAAAAAAA", "") == "x *** y ***");
        }

        // a proposal: 4 valid steps, 2 rejected (unknown parameter, out of range) plus a non-numeric one
        const juce::String recipeJson = R"({"title":"Kick lourd","summary":"Plus grave et plus sombre","unsupported":"Compression impossible",
            "steps":[
             {"name":"Pitch bas","param":"pitch","value":-3,"explanation":"Plus grave","expected":"Plus lourd"},
             {"name":"Volume","param":"volume","value":-2,"explanation":"Marge","expected":"Moins fort"},
             {"name":"Graves","param":"eq1_gain","value":5.5,"explanation":"Boost","expected":"Plus de poids"},
             {"name":"Reverb","param":"reverb_wet","value":30,"explanation":"Espace","expected":"Plus large"},
             {"name":"Compresseur","param":"compressor_ratio","value":4,"explanation":"x","expected":"x"},
             {"name":"Pitch fou","param":"pitch","value":99,"explanation":"x","expected":"x"},
             {"name":"Texte","param":"pan","value":"loud","explanation":"x","expected":"x"}]})";
        auto envelope = [] (const juce::String& inner)
        {
            auto* part = new juce::DynamicObject(); part->setProperty ("text", inner);
            auto* content = new juce::DynamicObject(); content->setProperty ("parts", juce::Array<juce::var> { juce::var (part) });
            auto* cand = new juce::DynamicObject(); cand->setProperty ("finishReason", "STOP"); cand->setProperty ("content", juce::var (content));
            auto* root = new juce::DynamicObject(); root->setProperty ("candidates", juce::Array<juce::var> { juce::var (cand) });
            return juce::JSON::toString (juce::var (root));
        };
        {
            setFake (200, envelope (recipeJson));
            juce::Result r = juce::Result::fail ("x"); juce::String js;
            Question q; q.mode = Mode::Recipe; q.request = "Rends ce kick plus lourd"; q.sampleType = "Kick"; q.padName = "Mon Kick";
            doAsk (q, r, js);
            CHECK (r.wasOk());
            const auto req = lastReq();
            CHECK (req.method == "POST" && req.url.startsWith (AiService::kHost) && req.url.contains ("gemini-flash-x:generateContent") && ! req.url.contains (key));
            CHECK (! req.body.contains (key) && req.body.contains ("Mon Kick") && req.body.contains ("responseSchema") && req.body.contains ("application/json"));
            CHECK (! req.body.contains (wav.getFullPathName()) && ! req.body.contains (tmp.getFullPathName()));   // no file path leaves the machine
            Recipe rec;
            CHECK (parseRecipe (js, cat, rec).wasOk());
            CHECK (rec.steps.size() == 4 && rec.rejected.size() == 3 && rec.unsupported.isNotEmpty());
        }
        // strict validation
        {
            Recipe rec;
            CHECK (! parseRecipe (R"({"title":"x","summary":"y","unsupported":"","steps":[],"code":"system"})", cat, rec).wasOk());       // unknown field
            CHECK (! parseRecipe (R"({"title":"x","summary":"y","unsupported":"","steps":[{"name":"a","param":"pitch","value":1,"explanation":"","expected":"","path":"/etc"}]})", cat, rec).wasOk());
            CHECK (! parseRecipe ("not json at all", cat, rec).wasOk());
            CHECK (parseRecipe (R"({"title":"x","summary":"y","unsupported":"","steps":[{"name":"n","param":"note","value":70,"explanation":"","expected":""}]})", cat, rec).wasOk() && rec.steps.empty() && rec.rejected.size() == 1);
            CHECK (parseRecipe (R"({"title":"x","summary":"y","unsupported":"","steps":[{"name":"n","param":"reverb_type","value":1.5,"explanation":"","expected":""}]})", cat, rec).wasOk() && rec.steps.empty());
            juce::String ex;
            CHECK (parseExplanation (R"({"answer":"Le pitch change la hauteur."})", ex).wasOk() && ex.isNotEmpty());
            CHECK (! parseExplanation (R"({"answer":"a","extra":1})", ex).wasOk());
        }
        // blocked prompt, quota, truncation
        {
            juce::Result r = juce::Result::ok(); juce::String js;
            Question q; q.request = "x";
            setFake (200, R"({"promptFeedback":{"blockReason":"SAFETY"}})");
            doAsk (q, r, js);
            CHECK (r.failed() && r.getErrorMessage().contains ("SAFETY"));
            setFake (429, "{}");
            doAsk (q, r, js);
            CHECK (r.failed() && r.getErrorMessage().contains ("429"));
            setFake (200, R"({"candidates":[{"finishReason":"MAX_TOKENS","content":{"parts":[{"text":"{"}]}}]})");
            doAsk (q, r, js);
            CHECK (r.failed());
        }

        // the assistant panel: nothing is applied until the user asks; preview / compare / accept / restore
        {
            auto getP = [&] (const juce::String& id) { return proc->apvts.getRawParameterValue (id)->load(); };
            auto setP = [&] (const juce::String& id, float v) { if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
            setP (ids::pitch (3), 0.0f); setP (ids::gain (3), 0.0f); setP (ids::eqBand (3, 0, "gain"), 0.0f); setP (ids::rvWet (3), 0.0f);
            setFake (200, envelope (recipeJson));

            auto editor = std::make_unique<DaftMugEditor> (*proc);
            editor->setBounds (0, 0, 1180, 740);
            editor->openAiAssistant (3);
            pump (100);
            auto* ap = editor->getAiAssistantPanel();
            CHECK (ap != nullptr);
            if (ap != nullptr)
            {
                ap->setQuestion (Mode::Recipe, "Kick", "Rends ce kick plus lourd et plus sombre");
                ap->send();
                for (int i = 0; i < 500 && ap->getRecipe().steps.empty(); ++i) pump (10);
                CHECK (ap->getRecipe().steps.size() == 4);
                CHECK (std::abs (getP (ids::pitch (3))) < 0.01f);                           // received, NOT applied
                ap->preview();
                CHECK (ap->isShowingProposal() && std::abs (getP (ids::pitch (3)) + 3.0f) < 0.02f && std::abs (getP (ids::gain (3)) + 2.0f) < 0.11f);
                CHECK (std::abs (getP (ids::eqBand (3, 0, "gain")) - 5.5f) < 0.11f && std::abs (getP (ids::rvWet (3)) - 30.0f) < 0.6f);
                ap->toggleCompare();
                CHECK (! ap->isShowingProposal() && std::abs (getP (ids::pitch (3))) < 0.01f && std::abs (getP (ids::gain (3))) < 0.11f);
                ap->toggleCompare();
                CHECK (ap->isShowingProposal() && std::abs (getP (ids::pitch (3)) + 3.0f) < 0.02f);
                ap->setStepChecked (0, false);                                              // un-tick the pitch step: follows live
                CHECK (std::abs (getP (ids::pitch (3))) < 0.01f && std::abs (getP (ids::gain (3)) + 2.0f) < 0.11f);
                ap->setStepChecked (0, true);
                auto snapA = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat pngA;
                juce::FileOutputStream osA (outDir.getChildFile ("ai_assistant.png"));
                if (osA.openedOk()) { osA.setPosition (0); osA.truncate(); pngA.writeImageToStream (snapA, osA); }
                ap->accept();
                CHECK (ap->isAccepted() && std::abs (getP (ids::pitch (3)) + 3.0f) < 0.02f);
                ap->restore();
                CHECK (! ap->isShowingProposal() && std::abs (getP (ids::pitch (3))) < 0.01f && std::abs (getP (ids::rvWet (3))) < 0.6f);
                ap->preview();
                CHECK (std::abs (getP (ids::pitch (3)) + 3.0f) < 0.02f);
            }
            editor.reset();                                                                  // closing without accepting restores
            CHECK (std::abs (getP (ids::pitch (3))) < 0.01f && std::abs (getP (ids::eqBand (3, 0, "gain"))) < 0.11f);

            // an explanation changes nothing
            setFake (200, envelope (R"({"answer":"Le pitch change la hauteur ET la durée."})"));
            auto editor2 = std::make_unique<DaftMugEditor> (*proc);
            editor2->setBounds (0, 0, 1180, 740);
            editor2->openAiAssistant (3);
            pump (50);
            if (auto* ap2 = editor2->getAiAssistantPanel())
            {
                ap2->setQuestion (Mode::Explain, {}, "C'est quoi le pitch ?");
                ap2->send();
                for (int i = 0; i < 500 && ap2->getOutputText().isEmpty(); ++i) pump (10);
                CHECK (ap2->getOutputText().contains ("hauteur") && std::abs (getP (ids::pitch (3))) < 0.01f);
            }
        }

        // settings page
        {
            auto editor = std::make_unique<DaftMugEditor> (*proc);
            editor->setBounds (0, 0, 1180, 740);
            editor->openAiSettings();
            pump (100);
            if (auto* sp = editor->getAiSettingsPanel())
            {
                CHECK (sp->isKeyFieldMasked() && sp->getKeyFieldText().isEmpty());            // a stored key is never shown by default
                sp->toggleReveal();
                CHECK (! sp->isKeyFieldMasked() && sp->getKeyFieldText() == key);
                sp->toggleReveal();
                CHECK (sp->isKeyFieldMasked() && sp->getKeyFieldText().isEmpty());
                setFake (200, R"({"models":[{"name":"models/gemini-flash-x","supportedGenerationMethods":["generateContent"]}]})");
                sp->testConnection();
                for (int i = 0; i < 300 && ! sp->getStatusText().contains ("r\xC3\xA9ussie"); ++i) pump (10);
                CHECK (sp->getStatusText().contains ("r\xC3\xA9ussie"));
                auto snapS = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat pngS;
                juce::FileOutputStream osS (outDir.getChildFile ("ai_settings.png"));
                if (osS.openedOk()) { osS.setPosition (0); osS.truncate(); pngS.writeImageToStream (snapS, osS); }
                sp->setKeyFieldTextForTest (key + "X");
                CHECK (sp->saveKeyFromField().wasOk() && svc.getKey() == key + "X");
                sp->setEnabledFromUi (false);
                CHECK (! svc.isEnabled());
            }
            editor->openTerms (false);
            pump (50);
            CHECK (editor->getTermsPanel() != nullptr);
            editor.reset();
            svc.setEnabled (true);
            CHECK (svc.setKey (key).wasOk());
        }

        // the key is nowhere it must not be: plug-in state, presets, settings file
        {
            juce::MemoryBlock blob;
            proc->getStateInformation (blob);
            CHECK (! blob.toString().contains (key));
            const auto settingsText = settingsFile.loadFileAsString();
            CHECK (settingsText.contains ("aiEnabled") && ! settingsText.contains (key) && ! settingsText.contains ("AIza"));
            PresetManager pmAi (*proc);
            CHECK (pmAi.saveKit ("Kit Sans Cle", "").wasOk());
            CHECK (pmAi.savePad (3, "Pad Sans Cle", "", false).wasOk());
            for (const auto& f : { pmAi.getFolder (PresetKind::Kit).getChildFile ("Kit Sans Cle.dmkit"), pmAi.getFolder (PresetKind::Pad).getChildFile ("Pad Sans Cle.dmpad") })
                CHECK (f.existsAsFile() && ! f.loadFileAsString().contains (key));
            CHECK (svc.clearKey().wasOk() && ! svc.hasKey());
        }
        proc->setAiForTesting (nullptr);
    }

    std::printf ("Kit audio export\n");
    {
        proc->loadSampleAsync (4, wav);
        CHECK (waitForState (*proc, 4, LoadState::Loaded));
        proc->setPadName (4, "Caisse / claire:*?");
        const auto out = tmp.getChildFile ("kit_export");
        out.deleteRecursively();
        const auto items = KitExporter::prepare (*proc, 16, 0.0, true);
        CHECK (! items.empty());
        const auto rep = KitExporter::run (items, out);
        CHECK (rep.failed == 0 && rep.written == (int) items.size());
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        for (const auto& name : rep.files)
        {
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (out.getChildFile (name)));
            CHECK (rd != nullptr && rd->lengthInSamples > 100 && rd->numChannels == 2 && rd->bitsPerSample == 16);
            CHECK (! name.containsAnyOf ("/:*?\\"));      // pad names are made file-safe
        }
        const auto rep2 = KitExporter::run (items, out);   // never overwrites: numbered copies
        CHECK (rep2.written == rep.written && out.findChildFiles (juce::File::findFiles, false, "*.wav").size() == 2 * rep.written);
        proc->clearPad (4);
    }

    std::printf ("Tempo: detection, stretch without pitch change, restore, state\n");
    {
        const auto dirT = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dm_tempo_test");
        dirT.deleteRecursively(); dirT.createDirectory();
        const auto loopFile = dirT.getChildFile ("loop120.wav");
        {   // 8 s: kick on each beat (120 BPM) + noisy hat on the off-beats
            juce::WavAudioFormat wavF;
            std::unique_ptr<juce::OutputStream> o = loopFile.createOutputStream();
            auto w = wavF.createWriterFor (o, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (16));
            const int n = 44100 * 8;
            juce::AudioBuffer<float> b (2, n); b.clear();
            const double beat = 0.5 * 44100.0;
            juce::Random rnd (7);
            for (int k = 0; k * beat < n; ++k)
            {
                const int at = (int) (k * beat), hat = (int) ((k + 0.5) * beat);
                for (int i = 0; i < 6000 && at + i < n; ++i) { const float v = 0.9f * std::sin (juce::MathConstants<float>::twoPi * 60.0f * (float) i / 44100.0f) * std::exp (-(float) i / 1500.0f); b.addSample (0, at + i, v); b.addSample (1, at + i, v); }
                for (int i = 0; i < 1500 && hat + i < n; ++i) { const float v = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / 400.0f); b.addSample (0, hat + i, v); b.addSample (1, hat + i, v); }
            }
            w->writeFromAudioSampleBuffer (b, 0, n);
        }
        proc->loadSampleAsync (11, loopFile);
        CHECK (waitForState (*proc, 11, LoadState::Loaded));
        auto meta = proc->getPadMeta (11);
        CHECK (std::abs (meta.originalBpm - 120.0) < 1.5 || std::abs (meta.originalBpm - 60.0) < 1.0);   // folded into 75..165: expect 120
        CHECK (meta.targetBpm == 0.0 && meta.sample == meta.original);
        const int origFrames = meta.original->numFrames();

        proc->setPadTempo (11, 120.0, 90.0);   // slower: 120 -> 90 BPM = 1.333x longer
        for (int i = 0; i < 400 && proc->getPadMeta (11).stretching; ++i) pump (20);
        pump (50);
        meta = proc->getPadMeta (11);
        CHECK (! meta.stretching && meta.sample != meta.original);
        CHECK (std::abs (meta.sample->numFrames() - (int) std::lround (origFrames * 120.0 / 90.0)) <= 1);
        CHECK (meta.original->numFrames() == origFrames);   // the original is untouched

        proc->setPadTempo (11, 120.0, 400.0);   // ratio out of range: keeps the original and explains
        pump (50);
        meta = proc->getPadMeta (11);
        CHECK (meta.sample == meta.original && meta.tempoMessage.isNotEmpty());

        proc->setPadTempo (11, 120.0, 150.0);
        for (int i = 0; i < 400 && proc->getPadMeta (11).stretching; ++i) pump (20);
        pump (50);
        juce::MemoryBlock blobT;
        proc->getStateInformation (blobT);
        proc->clearPad (11);
        CHECK (proc->getPadMeta (11).original == nullptr);
        proc->setStateInformation (blobT.getData(), (int) blobT.getSize());
        CHECK (waitForState (*proc, 11, LoadState::Loaded));
        for (int i = 0; i < 400 && proc->getPadMeta (11).stretching; ++i) pump (20);
        pump (50);
        meta = proc->getPadMeta (11);
        CHECK (std::abs (meta.targetBpm - 150.0) < 0.01 && std::abs (meta.originalBpm - 120.0) < 1.5 && meta.sample != meta.original);

        proc->setPadTempo (11, meta.originalBpm, 0.0);   // back to the original tempo
        pump (50);
        meta = proc->getPadMeta (11);
        CHECK (meta.sample == meta.original && meta.targetBpm == 0.0);
        CHECK (proc->getSampleMemoryBytes() > 0);
        proc->clearPad (11);
        dirT.deleteRecursively();
    }

    std::printf ("Sample sharing between pads\n");
    {
        proc->loadSampleAsync (13, wav2);
        CHECK (waitForState (*proc, 13, LoadState::Loaded));
        proc->loadSampleAsync (14, wav2);
        CHECK (waitForState (*proc, 14, LoadState::Loaded));
        CHECK (proc->getPadMeta (13).sample.get() == proc->getPadMeta (14).sample.get());   // one decoded copy for both pads
        proc->clearPad (13);
        proc->clearPad (14);
    }

    std::printf ("Presets: pad presets and kits\n");
    {
        const auto presetDir = tmp.getChildFile ("presets");
        presetDir.deleteRecursively();
       #if defined (_WIN32)
        _putenv_s ("DAFTMUG_PRESET_DIR", presetDir.getFullPathName().toRawUTF8());
       #else
        setenv ("DAFTMUG_PRESET_DIR", presetDir.getFullPathName().toRawUTF8(), 1);
       #endif
        PresetManager pm (*proc);
        auto setP = [&] (const juce::String& id, float v) { if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); else CHECK (false); };
        auto getP = [&] (const juce::String& id) { return proc->apvts.getRawParameterValue (id)->load(); };

        setP (ids::gain (7), -9.0f);
        setP (ids::eqBand (7, 2, "gain"), 7.0f);
        setP (ids::eqBand (7, 2, "dyn"), 2.0f);
        setP (ids::fx (7, "delay_mix"), 33.0f);
        setP (ids::rvType (7), 3.0f);
        setP (ids::note (7), 99.0f);

        CHECK (pm.savePad (7, "Mon Preset", "Kicks", false).wasOk());
        CHECK (! pm.savePad (7, "   ", "x", false).wasOk());
        const auto list = pm.list (PresetKind::Pad);
        CHECK (list.size() == 1 && list[0].name == "Mon Preset" && list[0].category == "Kicks");

        // load into ANOTHER pad: settings follow, the MIDI note of that pad is kept
        const float noteBefore = getP (ids::note (3));
        CHECK (pm.loadPad (3, list[0].file).wasOk());
        CHECK (std::abs (getP (ids::gain (3)) + 9.0f) < 0.11f);
        CHECK (std::abs (getP (ids::eqBand (3, 2, "gain")) - 7.0f) < 0.11f);
        CHECK (juce::roundToInt (getP (ids::eqBand (3, 2, "dyn"))) == 2);
        CHECK (std::abs (getP (ids::fx (3, "delay_mix")) - 33.0f) < 0.6f);
        CHECK (juce::roundToInt (getP (ids::rvType (3))) == 3);
        CHECK (std::abs (getP (ids::note (3)) - noteBefore) < 0.01f);

        // the file holds no secret and nothing outside the whitelist
        {
            const auto text = list[0].file.loadFileAsString().toLowerCase();
            CHECK (! text.contains ("apikey") && ! text.contains ("api_key") && ! text.contains ("secret") && ! text.contains ("token"));
        }

        // corrupt / foreign / future files are refused without touching anything
        const auto bad = presetDir.getChildFile ("bad.dmpad");
        bad.replaceWithText ("this is not xml");
        CHECK (! pm.loadPad (3, bad).wasOk());
        bad.replaceWithText ("<DAFTMUG_PAD_PRESET schema=\"99\"/>");
        CHECK (! pm.loadPad (3, bad).wasOk());
        bad.replaceWithText ("<OTHER/>");
        CHECK (! pm.loadPad (3, bad).wasOk());
        CHECK (std::abs (getP (ids::gain (3)) + 9.0f) < 0.11f);

        // kit: save, change everything, load back
        proc->setPadName (7, "Nom perso");
        CHECK (pm.saveKit ("Kit Test", "").wasOk());
        setP (ids::gain (7), 3.0f);
        setP (ids::note (7), 50.0f);
        proc->setPadName (7, "Autre");
        proc->clearPad (0);
        const auto kits = pm.list (PresetKind::Kit);
        CHECK (kits.size() == 1);
        const auto kr = pm.loadKit (kits[0].file);
        CHECK (kr.wasOk());
        CHECK (std::abs (getP (ids::gain (7)) + 9.0f) < 0.11f);
        CHECK (std::abs (getP (ids::note (7)) - 99.0f) < 0.01f);          // notes ARE part of a kit
        CHECK (waitForState (*proc, 0, LoadState::Loaded));               // pad 1's sample came back
        CHECK (proc->getPadMeta (7).name == "Nom perso");
        {
            const auto text = kits[0].file.loadFileAsString().toLowerCase();
            CHECK (! text.contains ("apikey") && ! text.contains ("secret"));
        }

        // ---- portable collection: copies, name collisions, relative paths, moving the folder
        {
            const auto dirA = tmp.getChildFile ("colA"), dirB = tmp.getChildFile ("colB");
            dirA.deleteRecursively(); dirB.deleteRecursively();
            dirA.createDirectory(); dirB.createDirectory();
            CHECK (wav.copyFileTo (dirA.getChildFile ("kick.wav")) && wav2.copyFileTo (dirB.getChildFile ("kick.wav")));
            proc->loadSampleAsync (5, dirA.getChildFile ("kick.wav"));
            proc->loadSampleAsync (6, dirB.getChildFile ("kick.wav"));        // same NAME, different content
            proc->loadSampleAsync (9, dirA.getChildFile ("kick.wav"));        // same FILE as pad 5: one copy only
            CHECK (waitForState (*proc, 5, LoadState::Loaded) && waitForState (*proc, 6, LoadState::Loaded) && waitForState (*proc, 9, LoadState::Loaded));

            const auto out = tmp.getChildFile ("collected");
            out.deleteRecursively();
            CollectReport rep;
            CHECK (Collector (*proc).collect (out, "Kit Portable", rep).wasOk());
            CHECK (out.getChildFile ("Samples/kick.wav").existsAsFile() && out.getChildFile ("Samples/kick_2.wav").existsAsFile());
            CHECK (rep.renamed == 1 && rep.kitFile.existsAsFile());
            CHECK (dirA.getChildFile ("kick.wav").existsAsFile() && dirB.getChildFile ("kick.wav").existsAsFile());   // originals untouched
            CHECK (proc->getPadMeta (5).file.isAChildOf (out) && proc->getPadMeta (5).file == proc->getPadMeta (9).file);
            CHECK (proc->getPadMeta (6).file != proc->getPadMeta (5).file && proc->getPadMeta (6).file.isAChildOf (out));
            {
                const auto kitText = rep.kitFile.loadFileAsString();
                CHECK (kitText.contains ("Samples/kick.wav") && ! kitText.contains (tmp.getFullPathName()));
            }
            CHECK (waitForState (*proc, 5, LoadState::Loaded) && waitForState (*proc, 6, LoadState::Loaded));

            // second collection into the same folder reuses identical files (no duplicates)
            CollectReport rep2;
            CHECK (Collector (*proc).collect (out, "Kit Portable", rep2).wasOk() && rep2.renamed == 0);
            CHECK (out.getChildFile ("Samples").findChildFiles (juce::File::findFiles, false, "*").size() == 3   // kick, kick_2 and pad 1's own sample
               && ! out.getChildFile ("Samples/kick_3.wav").existsAsFile());

            // the folder moves elsewhere: the kit still loads, every path being relative
            const auto moved = tmp.getChildFile ("moved_elsewhere");
            moved.deleteRecursively();
            CHECK (out.moveFileTo (moved));
            proc->clearPad (5); proc->clearPad (6); proc->clearPad (9);
            PresetManager pm2 (*proc);
            CHECK (pm2.loadKit (moved.getChildFile ("Kit Portable.dmkit")).wasOk());
            CHECK (waitForState (*proc, 5, LoadState::Loaded) && waitForState (*proc, 6, LoadState::Loaded) && waitForState (*proc, 9, LoadState::Loaded));
            CHECK (proc->getPadMeta (6).file.isAChildOf (moved));

            // unwritable destination: refused, nothing changes
            const auto notAFolder = tmp.getChildFile ("plainfile.txt");
            notAFolder.replaceWithText ("x");
            CollectReport rep3;
            CHECK (! Collector (*proc).collect (notAFolder.getChildFile ("sub"), "X", rep3).wasOk());
            for (int pad : { 5, 6, 9 }) proc->clearPad (pad);
        }
        setP (ids::note (7), 43.0f);   // restore for the next blocks of the smoke test
    }

    std::printf ("Processor: errors and MIDI learn\n");
    proc->loadSampleAsync (2, tmp.getChildFile ("does_not_exist.wav"));
    CHECK (waitForState (*proc, 2, LoadState::Error));
    const auto notAudio = tmp.getChildFile ("fake.wav");
    notAudio.replaceWithText ("this is not audio");
    proc->loadSampleAsync (3, notAudio);
    CHECK (waitForState (*proc, 3, LoadState::Error));

    proc->startMidiLearn (2);
    buffer.clear(); midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 90), 5);
    proc->processBlock (buffer, midi);
    int lp = -1, ln = -1;
    CHECK (proc->consumeLearnedNote (lp, ln) && lp == 2 && ln == 72);

    std::printf ("State save / restore\n");
    if (auto* n = proc->apvts.getParameter (ids::note (0)))
        n->setValueNotifyingHost (n->convertTo0to1 (60.0f));
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    CHECK (state.getSize() > 100);
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        CHECK (xml != nullptr);
        const auto text = xml != nullptr ? xml->toString() : juce::String();
        CHECK (text.contains ("DAFTMUG") && text.contains ("Kick heavy 01"));
        CHECK (! text.containsIgnoreCase ("apikey") && ! text.containsIgnoreCase ("secret"));
    }

    auto proc2 = std::make_unique<DaftMugProcessor>();
    proc2->setRateAndBufferSizeDetails (48000.0, 512);
    proc2->prepareToPlay (48000.0, 512);
    proc2->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (waitForState (*proc2, 0, LoadState::Loaded));
    CHECK (proc2->getPadMeta (0).name == "Kick heavy 01");
    CHECK (proc2->getPadMeta (1).name == "Ma caisse claire" && proc2->getPadMeta (1).userRenamed);
    CHECK (std::abs (proc2->apvts.getRawParameterValue (ids::gain (1))->load() + 12.0f) < 0.01f);
    CHECK ((int) proc2->apvts.getRawParameterValue (ids::note (0))->load() == 60);
    CHECK (std::abs (proc2->apvts.getRawParameterValue (ids::attack (7))->load() - 50.0f) < 0.2f);   // step 2a params persist
    CHECK (proc2->readPadParams (7).reverse && (int) proc2->apvts.getRawParameterValue (ids::maxVoices)->load() == 5);

    // restored sampler really plays on its new note (60)
    buffer.clear(); midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
    proc2->processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 100, 200) > 0.05f);

    // garbage state must not crash
    const char junk[] = "not a state";
    proc2->setStateInformation (junk, (int) sizeof (junk));

    std::printf ("Text fields: select all\n");
    {
        ui::ShortcutTextEditor te;
        te.setBounds (0, 0, 200, 30);
        te.setText ("Kick heavy", false);
        for (int code : { (int) 'a', (int) 'A' })
        {
            te.setCaretPosition (2);
            CHECK (te.keyPressed (juce::KeyPress (code, juce::ModifierKeys::commandModifier, 0)));
            CHECK (te.getHighlightedText() == "Kick heavy");
        }
    }

    std::printf ("Reverb: import and choose IR files\n");
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dm_ir_test");
        tmp.deleteRecursively();
        tmp.createDirectory();
        const auto oldHome = juce::SystemStats::getEnvironmentVariable ("HOME", {});
        setenv ("HOME", tmp.getFullPathName().toRawUTF8(), 1);   // the user IR folder lands in the temp dir
        const auto src = writeTestWav (tmp, "MaSalle.wav", 44100.0, 1, 2);
        ui::ReverbSection rs (*proc, 4);
        rs.setBounds (0, 0, 900, 140);
        rs.refreshList();
        const int before = rs.getNumIrs();
        rs.importFiles (juce::Array<juce::File> { src });
        CHECK (rs.getNumIrs() == before + 1);
        int found = -1;
        // the imported IR sits in the user IR folder and is listed
        const auto userDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DaftMug").getChildFile ("IR");
        CHECK (userDir.getChildFile ("MaSalle.wav").existsAsFile());
        for (int i = 0; i < rs.getNumIrs(); ++i) { rs.selectIr (i); if (proc->getPadMeta (4).customIr.getFileName() == "MaSalle.wav") { found = i; break; } }
        CHECK (found >= 0);
        CHECK (proc->apvts.getRawParameterValue (ids::rvType (4))->load() == (float) (DaftMugProcessor::kNumIrTypes - 1));
        {
            auto snapR = rs.createComponentSnapshot (rs.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat pngR;
            juce::FileOutputStream os (outDir.getChildFile ("reverb_section.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); pngR.writeImageToStream (snapR, os); }
        }
        rs.selectIr (-1);
        CHECK (proc->apvts.getRawParameterValue (ids::rvType (4))->load() == 0.0f);
        setenv ("HOME", oldHome.toRawUTF8(), 1);
        tmp.deleteRecursively();
    }

    std::printf ("Editor: layout, gear icon, screenshots\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> base (proc->createEditor());
        auto* editor = dynamic_cast<DaftMugEditor*> (base.get());
        CHECK (editor != nullptr);
        editor->setBounds (0, 0, 1180, 740);
        pump (100);

        // 16 pads, each with a gear in its bottom-left corner that is a separate hit target
        int gearsOk = 0;
        for (int i = 0; i < kNumPads; ++i)
        {
            auto* pad = editor->getPad (i);
            CHECK (pad != nullptr);
            if (pad == nullptr) continue;
            const auto inPad = juce::Point<int> (20, pad->getHeight() - 20);       // bottom-left corner
            auto* hit = pad->getComponentAt (inPad);
            const auto centre = juce::Point<int> (pad->getWidth() / 2, pad->getHeight() / 2);
            if (hit != nullptr && hit != pad && pad->getComponentAt (centre) == pad)
                ++gearsOk;
        }
        CHECK (gearsOk == kNumPads);

        // drag & drop of files from the computer onto a pad
        {
            auto* target = editor->getPad (11);
            CHECK (target != nullptr);
            const auto txt = tmp.getChildFile ("notes.txt");
            txt.replaceWithText ("not audio");
            CHECK (! target->isInterestedInFileDrag (juce::StringArray (txt.getFullPathName())));
            CHECK (target->isInterestedInFileDrag (juce::StringArray (wav2.getFullPathName())));
            CHECK (target->isInterestedInFileDrag (juce::StringArray (txt.getFullPathName(), wav2.getFullPathName())));   // mixed: audio is enough
            proc->clearPad (11);
            target->filesDropped (juce::StringArray (txt.getFullPathName(), wav2.getFullPathName()), 5, 5);
            CHECK (waitForState (*proc, 11, LoadState::Loaded));
            CHECK (proc->getPadMeta (11).file == wav2);
            CHECK (proc->getPadMeta (10).state == LoadState::Empty || proc->getPadMeta (10).file != wav2);   // only the target pad
            proc->clearPad (11);
        }

        auto snap = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        {
            juce::FileOutputStream os (outDir.getChildFile ("main.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (snap, os); }
        }

        editor->openOptions();
        pump (100);
        CHECK (editor->isOptionsOpen());
        {
            auto snapO = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (outDir.getChildFile ("options.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (snapO, os); }
        }

        editor->openPadEditor (7);
        pump (100);
        CHECK (editor->isPadEditorOpen() && ! editor->isOptionsOpen());
        {   // Space bar plays the pad being edited
            const auto before7 = proc->getEngine().padTriggerCount (7);
            CHECK (editor->getPadEditor() != nullptr && editor->getPadEditor()->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
            juce::AudioBuffer<float> b7 (2, 512); juce::MidiBuffer m7;
            proc->processBlock (b7, m7);
            CHECK (proc->getEngine().padTriggerCount (7) == before7 + 1);
            {   // the EQ analyser of the open editor receives this pad's signal
                std::vector<float> tap (512);
                proc->getEngine().readAnalyzer (tap.data(), 512);
                float mx = 0.0f; for (float v : tap) mx = std::max (mx, std::abs (v));
                CHECK (mx > 0.001f);
            }
            for (int b = 0; b < 40; ++b) { b7.clear(); m7.clear(); proc->processBlock (b7, m7); }   // ~0.5 s of sound
            editor->setBounds (0, 0, 1180, 2800);
            pump (80);                                           // the analyser timer reads it
            auto snapLive = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            { juce::PNGImageFormat pngL; juce::FileOutputStream os (outDir.getChildFile ("eq_live_editor.png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); pngL.writeImageToStream (snapLive, os); } }
            editor->setBounds (0, 0, 1180, 740);
            editor->getPadEditor()->keyStateChanged (false);   // key released
            {   // master meter sees the output (the buffer above holds the pad's sound)
                auto& mt = editor->getMeter();
                for (int k = 0; k < 6; ++k) { b7.clear(); m7.clear(); proc->triggerPad (7, 1.0f); proc->processBlock (b7, m7); mt.tick(); }
                CHECK (mt.getPeakDb (0) > -60.0f && mt.getRmsDb (0) > -100.0f && mt.getPeakDb (0) <= 6.0f);
            }
        }
        auto snap2 = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        {
            juce::FileOutputStream os (outDir.getChildFile ("pad_editor.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (snap2, os); }
        }
        editor->setBounds (0, 0, 1180, 2800);                 // tall window: whole scrollable body in one picture
        pump (100);
        {
            auto snapT = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (outDir.getChildFile ("pad_editor_full.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (snapT, os); }
        }

        // ---- Slice to Pads
        std::printf ("Slice to Pads\n");
        {
            editor->setBounds (0, 0, 1180, 740);
            // a loop with 4 clear attacks (decaying noise bursts), written by the test
            const auto loop = tmp.getChildFile ("drum_loop.wav");
            {
                juce::WavAudioFormat fmt;
                loop.deleteFile();
                std::unique_ptr<juce::OutputStream> os = loop.createOutputStream();
                auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
                CHECK (w != nullptr);
                if (w != nullptr)
                {
                    juce::AudioBuffer<float> b (1, 48000 * 2);
                    b.clear();
                    juce::Random rnd (3);
                    for (double t : { 0.10, 0.60, 1.05, 1.50 })
                        for (int k = 0; k < 20000; ++k)
                            b.setSample (0, (int) (t * 48000) + k, 0.8f * (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) k / 1400.0f));
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
            }
            for (int p = 0; p < kNumPads; ++p) proc->clearPad (p);
            proc->loadSampleAsync (12, loop);
            CHECK (waitForState (*proc, 12, LoadState::Loaded));
            proc->loadSampleAsync (14, wav);                          // a pad that will be overwritten (and restored by undo)
            CHECK (waitForState (*proc, 14, LoadState::Loaded));
            const auto before14 = proc->getPadMeta (14).file;

            editor->openSlicer (12);
            pump (100);
            CHECK (editor->isSlicerOpen());
            auto* sp = editor->getSlicePanel();
            if (sp != nullptr)
            {
                auto pts = sp->getView().getPoints();
                CHECK (pts.size() == 4);                                // 4 attacks found; only 4 pads (13-16) are free below pad 13
                // preview borrows the source pad's start/end and gives them back
                const float s0 = proc->apvts.getRawParameterValue (ids::start (12))->load();
                if (sp->getView().onPreviewStart) sp->getView().onPreviewStart (2);
                CHECK (proc->readPadParams (12).startFraction > 0.4f);                              // the engine plays slice 3...
                CHECK (std::abs (proc->apvts.getRawParameterValue (ids::start (12))->load() - s0) < 1.0e-6f);   // ...without touching the parameter
                if (sp->getView().onPreviewEnd) sp->getView().onPreviewEnd();
                for (int i = 0; i < 12; ++i) pump (50);                                             // the override clears itself
                CHECK (proc->readPadParams (12).startFraction < 0.01f);

                auto snapS = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                {
                    juce::FileOutputStream os (outDir.getChildFile ("slicer.png"));
                    if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (snapS, os); }
                }

                CHECK (sp->apply().wasOk());                            // slices go to pads 13..16 (source pad = 13)
                CHECK (std::abs (proc->apvts.getRawParameterValue (ids::start (12))->load() - 0.0f) < 0.1f);
                for (int i = 0; i < 4 && i < (int) pts.size(); ++i)
                {
                    const int pad = 12 + i;
                    CHECK (proc->getPadMeta (pad).state == LoadState::Loaded);
                    CHECK (proc->getPadMeta (pad).sample == proc->getPadMeta (12).sample);   // shared audio: nothing copied
                    CHECK (proc->getPadMeta (pad).name.endsWith (juce::String (i + 1).paddedLeft ('0', 2)));
                    const float st = proc->apvts.getRawParameterValue (ids::start (pad))->load();
                    CHECK (std::abs (st - (float) pts[(size_t) i] / 96000.0f) < 0.002f);
                }
                CHECK (std::abs (proc->apvts.getRawParameterValue (ids::end (15))->load() - 1.0f) < 0.001f);
                CHECK (proc->canUndoSlices());
                (void) s0;

                // the audio of slice 2 (pad 15): starts with an attack, and the full file is untouched
                proc->prepareToPlay (48000.0, 512);
                buffer.clear(); midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + 14, (juce::uint8) 127), 0);
                proc->processBlock (buffer, midi);
                CHECK (buffer.getMagnitude (0, 0, 512) > 0.05f);        // the slice begins on the attack, not on silence

                proc->undoSlices();
                pump (200);
                CHECK (! proc->canUndoSlices());
                CHECK (waitForState (*proc, 14, LoadState::Loaded));
                CHECK (proc->getPadMeta (14).file == before14);          // the overwritten pad came back
                CHECK (proc->getPadMeta (15).state == LoadState::Empty); // pads that were empty are empty again
                CHECK (std::abs (proc->apvts.getRawParameterValue (ids::end (12))->load() - 1.0f) < 0.001f);
            }
            editor->openPadEditor (0);
            pump (50);
            CHECK (! editor->isSlicerOpen());
        }
    }

    {
        std::printf ("Processor: loop / choke / chromatic parameters, master + limiter, undo / redo\n");
        auto q = std::make_unique<DaftMugProcessor>();
        q->setRateAndBufferSizeDetails (48000.0, 512);
        q->prepareToPlay (48000.0, 512);
        CHECK (q->getLatencySamples() > 20 && q->getLatencySamples() < 200);   // constant limiter latency reported to the host

        auto setPlain = [&] (const juce::String& id, float v, bool gesture = true)
        {
            if (auto* prm = q->apvts.getParameter (id))
            {
                if (gesture) prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                if (gesture) prm->endChangeGesture();
            }
            else CHECK (false);
        };
        auto readPlain = [&] (const juce::String& id) { return q->apvts.getRawParameterValue (id)->load(); };

        q->loadSampleAsync (0, wav);
        CHECK (waitForState (*q, 0, LoadState::Loaded));
        q->history.clear();

        // new parameters reach the engine parameters
        setPlain (ids::mode (0), 3.0f, false);
        setPlain (ids::loopStart (0), 0.5f, false);
        setPlain (ids::loopFade (0), 25.0f, false);
        setPlain (ids::choke (0), 2.0f, false);
        setPlain (ids::chromatic (0), 1.0f, false);
        {
            const auto pp = q->readPadParams (0);
            CHECK (pp.mode == dm::PlayMode::Loop);
            CHECK (std::abs (pp.loopStartFraction - 0.5f) < 0.001f && std::abs (pp.loopCrossfadeSeconds - 0.025f) < 0.0005f);
            CHECK (pp.chokeGroup == 2 && pp.chromatic);
        }
        CHECK (! q->history.canUndo());   // host-style changes (no gesture) are never recorded
        if (auto* prm = q->apvts.getParameter (ids::choke (0))) CHECK (prm->getCurrentValueAsText() == "2");
        if (auto* prm = q->apvts.getParameter (ids::mode (0))) CHECK (prm->getCurrentValueAsText() == "Loop");
        setPlain (ids::mode (0), 0.0f, false);
        setPlain (ids::chromatic (0), 0.0f, false);
        setPlain (ids::choke (0), 0.0f, false);

        // master volume + limiter: +12 dB on the pad and +12 dB master can never go over the ceiling (-0.3 dB)
        setPlain (ids::gain (0), 12.0f, false);
        setPlain (ids::masterGain, 12.0f, false);
        {
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
            float worst = 0.0f, loud = 0.0f;
            for (int i = 0; i < 40; ++i)
            {
                b.clear();
                q->processBlock (b, m);
                m.clear();
                worst = juce::jmax (worst, b.getMagnitude (0, 512), b.getMagnitude (1, 0, 512));
                loud = juce::jmax (loud, b.getMagnitude (0, 0, 512));
            }
            const float ceiling = juce::Decibels::decibelsToGain (-0.3f);
            CHECK (worst <= ceiling + 1.0e-4f);
            CHECK (loud > 0.5f);                       // limited, not muted
        }
        setPlain (ids::limiterOn, 0.0f, false);       // limiter off: the same signal now exceeds the ceiling
        {
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
            float worst = 0.0f;
            for (int i = 0; i < 40; ++i) { b.clear(); q->processBlock (b, m); m.clear(); worst = juce::jmax (worst, b.getMagnitude (0, 512)); }
            CHECK (worst > 1.0f);
        }
        setPlain (ids::limiterOn, 1.0f, false);
        setPlain (ids::masterGain, 0.0f, false);
        setPlain (ids::gain (0), 0.0f, false);
        q->history.clear();

        // undo / redo of a parameter edit (a gesture) ...
        setPlain (ids::pan (0), 0.5f);
        CHECK (q->history.canUndo() && ! q->history.canRedo());
        CHECK (std::abs (readPlain (ids::pan (0)) - 0.5f) < 0.01f);
        CHECK (q->history.undo());
        CHECK (std::abs (readPlain (ids::pan (0))) < 0.01f);
        CHECK (q->history.canRedo());
        CHECK (q->history.redo());
        CHECK (std::abs (readPlain (ids::pan (0)) - 0.5f) < 0.01f);
        CHECK (q->history.undo());
        CHECK (! q->history.canUndo());

        // ... a drag = one step (many values, one gesture)
        if (auto* prm = q->apvts.getParameter (ids::pitch (0)))
        {
            prm->beginChangeGesture();
            for (int i = 1; i <= 20; ++i) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i * 0.5f));
            prm->endChangeGesture();
        }
        CHECK (std::abs (readPlain (ids::pitch (0)) - 10.0f) < 0.02f);
        CHECK (q->history.undo());
        CHECK (std::abs (readPlain (ids::pitch (0))) < 0.02f);
        CHECK (! q->history.canUndo());

        // pad actions: rename, colour, clear, load
        juce::Thread::sleep (400);   // separate steps (changes closer than 350 ms are grouped)
        q->setPadName (0, "Ma grosse caisse");
        CHECK (q->getPadMeta (0).name == "Ma grosse caisse" && q->history.canUndo());
        const auto oldColour = q->getPadMeta (0).colour;
        juce::Thread::sleep (400);
        q->setPadColour (0, juce::Colours::orange);
        CHECK (q->history.undo());
        CHECK (q->getPadMeta (0).colour == oldColour && q->getPadMeta (0).name == "Ma grosse caisse");
        CHECK (q->history.undo());
        CHECK (q->getPadMeta (0).name == "Kick heavy 01" && ! q->getPadMeta (0).userRenamed);
        CHECK (q->history.redo() && q->history.redo());
        CHECK (q->getPadMeta (0).name == "Ma grosse caisse" && q->getPadMeta (0).colour == juce::Colours::orange);
        q->history.clear();

        juce::Thread::sleep (400);
        q->clearPad (0);
        CHECK (q->getPadMeta (0).state == LoadState::Empty && q->history.canUndo());
        CHECK (q->history.undo());
        CHECK (q->getPadMeta (0).state == LoadState::Loaded && q->getPadMeta (0).sample != nullptr);
        {
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
            q->processBlock (b, m);   // applies the restored sample
            b.clear(); m.clear();
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
            q->processBlock (b, m);
            CHECK (b.getMagnitude (0, 100, 400) > 0.05f);   // the restored pad plays again
        }
        CHECK (q->history.redo());
        CHECK (q->getPadMeta (0).state == LoadState::Empty);
        CHECK (q->history.undo());
        q->history.clear();

        juce::Thread::sleep (400);
        q->loadSampleAsync (3, wav2);
        CHECK (waitForState (*q, 3, LoadState::Loaded));
        CHECK (q->history.canUndo());
        CHECK (q->history.undo());
        CHECK (q->getPadMeta (3).state == LoadState::Empty);
        CHECK (q->history.redo());
        CHECK (q->getPadMeta (3).state == LoadState::Loaded);

        // a restored project empties the history and is not undoable
        juce::MemoryBlock state;
        q->getStateInformation (state);
        q->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (! q->history.canUndo());
        CHECK (waitForState (*q, 3, LoadState::Loaded));
        pump (100);
        CHECK (! q->history.canUndo());
    }

    proc.reset();
    proc2.reset();
    tmp.deleteRecursively();
    std::printf (failures == 0 ? "\nSMOKE TEST PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
