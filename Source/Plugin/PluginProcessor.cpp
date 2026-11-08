#include "PluginProcessor.h"
#include "Tempo.h"

#include "NameUtils.h"
#include "ParameterIds.h"
#include "AiService.h"
#include "PluginEditor.h"
#include "DaftMugPaths.h"

namespace dm
{
namespace
{
float parseNumber (const juce::String& text)
{
    return text.replace (",", ".").retainCharacters ("0123456789.-+").getFloatValue();
}

// "12.5 ms" / "0.2 s" -> milliseconds (a bare number is milliseconds)
float parseMilliseconds (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const float v = parseNumber (t);
    return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? v * 1000.0f : v;
}

juce::String millisecondsToText (float ms)
{
    return ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
}

std::unique_ptr<juce::AudioParameterFloat> makeMsParam (const juce::String& id, const juce::String& name, float maxMs, float defMs, float minMs = 0.0f)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (minMs, maxMs, 0.1f, 0.4f), defMs,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return millisecondsToText (v); })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseMilliseconds (t); }));
}

// Decodes a file completely on the calling (background) thread.
bool decodeFileUnchecked (const juce::File& file, std::shared_ptr<SampleData>& out, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Fichier introuvable";
        return false;
    }
    if (! file.hasReadAccess())
    {
        error = "Acces refuse (droits insuffisants)";
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg Vorbis, + MP3 (JUCE_USE_MP3AUDIOFORMAT=1)

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Format non pris en charge";
        return false;
    }

    const auto length = reader->lengthInSamples;
    if (length <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        error = "Fichier audio vide ou invalide";
        return false;
    }
    if (static_cast<double> (length) > reader->sampleRate * kMaxSampleSeconds)
    {
        error = "Sample trop long (maximum 300 s)";
        return false;
    }

    auto data = std::make_shared<SampleData>();
    data->left.assign (static_cast<size_t> (length), 0.0f);
    data->right.assign (static_cast<size_t> (length), 0.0f);
    data->sampleRate = reader->sampleRate;

    const int channels = juce::jmin (2, static_cast<int> (reader->numChannels));
    float* dest[2] = { data->left.data(), data->right.data() };
    if (! reader->read (dest, channels, 0, static_cast<int> (length)))
    {
        error = "Erreur de lecture du fichier";
        return false;
    }
    if (channels == 1)
        data->right = data->left;   // mono -> both channels

    out = std::move (data);
    return true;
}

// Never lets an exception (typically std::bad_alloc on a huge file) escape the loader thread.
bool decodeFile (const juce::File& file, std::shared_ptr<SampleData>& out, juce::String& error)
{
    try
    {
        return decodeFileUnchecked (file, out, error);
    }
    catch (const std::bad_alloc&)
    {
        out.reset();
        error = "Memoire insuffisante pour ce fichier";
    }
    catch (const std::exception&)
    {
        out.reset();
        error = "Erreur inattendue pendant la lecture";
    }
    return false;
}
} // namespace

// ============================================================================ parameters
juce::AudioProcessorValueTreeState::ParameterLayout DaftMugProcessor::createLayout()
{
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using juce::ParameterID;
    constexpr int version = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const juce::StringArray modeNames { "One-shot", "Gate", "Retrigger", "Loop" };

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "pad" + juce::String (pad + 1), "Pad " + juce::String (pad + 1).paddedLeft ('0', 2), "|");

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::gain (pad), version }, "Volume", NormalisableRange<float> (-60.0f, 12.0f, 0.01f, 2.5f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.containsIgnoreCase ("inf") ? -60.0f : parseNumber (t); })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::pitch (pad), version }, "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " st"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::pan (pad), version }, "Pan", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int)
                {
                    const int pct = juce::roundToInt (std::abs (v) * 100.0f);
                    return pct == 0 ? juce::String ("C") : juce::String (v < 0 ? "L " : "R ") + juce::String (pct) + " %";
                })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    const auto s = t.trim().toUpperCase();
                    if (s.startsWith ("C")) return 0.0f;
                    const float n = parseNumber (s) / 100.0f;
                    return s.startsWith ("L") ? -std::abs (n) : (s.startsWith ("R") ? std::abs (n) : n);
                })));

        group->addChild (std::make_unique<AudioParameterChoice> (
            ParameterID { ids::mode (pad), version }, "Mode", modeNames, 0));

        group->addChild (std::make_unique<AudioParameterInt> (
            ParameterID { ids::note (pad), version }, "MIDI note", 0, 127, kFirstMidiNote + pad,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([] (int v, int)
                {
                    return juce::String (v) + "  (" + juce::MidiMessage::getMidiNoteName (v, true, true, 3) + ")";
                })
                .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (0, 127, juce::roundToInt (parseNumber (t))); })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::vel (pad), version }, "Velocity sens.", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t) / 100.0f; })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::start (pad), version }, "Start", NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v * 100.0f, 1) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t) / 100.0f; })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::end (pad), version }, "End", NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 1.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v * 100.0f, 1) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t) / 100.0f; })));

        // --- step 2a: envelope, region fades, reverse, velocity curve
        group->addChild (makeMsParam (ids::attack (pad), "Attack", 2000.0f, 0.0f));
        group->addChild (makeMsParam (ids::decay (pad), "Decay", 5000.0f, 0.0f));
        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::sustain (pad), version }, "Sustain", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t) / 100.0f; })));
        group->addChild (makeMsParam (ids::release (pad), "Release", 5000.0f, 15.0f, 1.0f));
        group->addChild (makeMsParam (ids::fadeIn (pad), "Fade-in", 2000.0f, 0.0f));
        group->addChild (makeMsParam (ids::fadeOut (pad), "Fade-out", 2000.0f, 0.0f));
        group->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { ids::reverse (pad), version }, "Reverse", false));

        // loop point (share of the region) + seam cross-fade, choke group, chromatic keyboard
        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::loopStart (pad), version }, "Loop start", NormalisableRange<float> (0.0f, 0.95f, 0.0001f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v * 100.0f, 1) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t) / 100.0f; })));
        group->addChild (makeMsParam (ids::loopFade (pad), "Loop cross-fade", 2000.0f, 10.0f));
        group->addChild (std::make_unique<AudioParameterInt> (
            ParameterID { ids::choke (pad), version }, "Choke group", 0, kNumChokeGroups, 0,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([] (int v, int) { return v == 0 ? dm::U ("Aucun") : juce::String (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (0, kNumChokeGroups, juce::roundToInt (parseNumber (t))); })));
        group->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { ids::chromatic (pad), version }, "Chromatic", false));
        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::velCurve (pad), version }, "Velocity curve", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int)
                {
                    return std::abs (v) < 0.005f ? dm::U ("Linéaire") : (v > 0.0f ? dm::U ("Dure ") : dm::U ("Douce ")) + juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + " %";
                })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    const auto s = t.trim().toLowerCase();
                    if (s.startsWith ("lin")) return 0.0f;
                    const float n = parseNumber (s) / 100.0f;
                    return s.startsWith ("dou") ? -std::abs (n) : (s.startsWith ("dur") ? std::abs (n) : n);
                })));

        // --- step 2b: 4-band equaliser (per pad)
        group->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { ids::eqOn (pad), version }, "EQ on", true));
        {
            const EqParams defaults;
            const juce::StringArray typeNames { "Bell", "Low shelf", "High shelf", "High-pass", "Low-pass" };
            for (int b = 0; b < kEqBands; ++b)
            {
                const auto& d = defaults.band[(size_t) b];
                const juce::String n = "EQ " + juce::String (b + 1) + " ";
                group->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { ids::eqBand (pad, b, "on"), version }, n + "on", true));
                group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::eqBand (pad, b, "type"), version }, n + "type", typeNames, (int) d.type));

                NormalisableRange<float> freqRange (20.0f, 20000.0f, 0.01f);
                freqRange.setSkewForCentre (1000.0f);
                group->addChild (std::make_unique<AudioParameterFloat> (
                    ParameterID { ids::eqBand (pad, b, "freq"), version }, n + "freq", freqRange, d.freq,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, 1) + " Hz"; })
                        .withValueFromStringFunction ([] (const juce::String& t)
                        {
                            const auto s = t.trim().toLowerCase();
                            const float v = parseNumber (s);
                            return s.contains ("k") ? v * 1000.0f : v;
                        })));

                group->addChild (std::make_unique<AudioParameterFloat> (
                    ParameterID { ids::eqBand (pad, b, "gain"), version }, n + "gain", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })
                        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

                NormalisableRange<float> qRange (0.1f, 10.0f, 0.01f);
                qRange.setSkewForCentre (1.0f);
                group->addChild (std::make_unique<AudioParameterFloat> (
                    ParameterID { ids::eqBand (pad, b, "q"), version }, n + "Q", qRange, d.q,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })
                        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

                // dynamic EQ of this band
                auto dynFloat = [&] (const char* key, const juce::String& nm, float lo, float hi, float step, float def, const juce::String& unit, int dec, float skewCentre = 0.0f)
                {
                    NormalisableRange<float> range (lo, hi, step);
                    if (skewCentre > 0.0f) range.setSkewForCentre (skewCentre);
                    group->addChild (std::make_unique<AudioParameterFloat> (
                        ParameterID { ids::eqBand (pad, b, key), version }, n + nm, range, def,
                        juce::AudioParameterFloatAttributes()
                            .withStringFromValueFunction ([unit, dec] (float v, int) { return juce::String (v, dec) + unit; })
                            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));
                };
                group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::eqBand (pad, b, "dyn"), version }, n + "dynamics",
                                                                          juce::StringArray { "Off", "Compress", "Expand" }, 0));
                dynFloat ("thr", "threshold", -60.0f, 0.0f, 0.1f, d.thresholdDb, " dB", 1);
                dynFloat ("ratio", "ratio", 1.0f, 20.0f, 0.1f, d.ratio, ":1", 1, 4.0f);
                dynFloat ("att", "attack", 0.1f, 200.0f, 0.1f, d.attackMs, " ms", 1, 20.0f);
                dynFloat ("rel", "release", 5.0f, 2000.0f, 1.0f, d.releaseMs, " ms", 0, 150.0f);
                dynFloat ("knee", "knee", 0.0f, 24.0f, 0.1f, d.kneeDb, " dB", 1);
                dynFloat ("range", "range", 1.0f, 24.0f, 0.1f, d.rangeDb, " dB", 1);
            }
        }

        // --- step 2c: effects (saturation, bitcrusher, chorus, delay)
        {
            auto addFloat = [&] (const char* key, const juce::String& name, float lo, float hi, float step, float def, const juce::String& unit, int decimals, float skewCentre = 0.0f)
            {
                NormalisableRange<float> range (lo, hi, step);
                if (skewCentre > 0.0f) range.setSkewForCentre (skewCentre);
                group->addChild (std::make_unique<AudioParameterFloat> (
                    ParameterID { ids::fx (pad, key), version }, name, range, def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([unit, decimals] (float v, int) { return juce::String (v, decimals) + unit; })
                        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));
            };
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::fx (pad, "sat_mode"), version }, "Saturation",
                                                                      juce::StringArray { "Off", "Douce", "Franche", "Tube" }, 0));
            addFloat ("sat_drive", "Saturation drive", 0.0f, 24.0f, 0.1f, 6.0f, " dB", 1);
            addFloat ("sat_out", "Saturation output", -12.0f, 12.0f, 0.1f, 0.0f, " dB", 1);
            addFloat ("sat_mix", "Saturation mix", 0.0f, 100.0f, 1.0f, 100.0f, " %", 0);
            addFloat ("crush_bits", "Bitcrusher bits", 1.0f, 16.0f, 0.1f, 16.0f, " bits", 1);
            addFloat ("crush_rate", "Bitcrusher rate", 1.0f, 32.0f, 0.1f, 1.0f, " x", 1);
            addFloat ("crush_mix", "Bitcrusher mix", 0.0f, 100.0f, 1.0f, 0.0f, " %", 0);
            addFloat ("chorus_mix", "Chorus mix", 0.0f, 100.0f, 1.0f, 0.0f, " %", 0);
            addFloat ("chorus_rate", "Chorus rate", 0.1f, 5.0f, 0.01f, 0.8f, " Hz", 2);
            addFloat ("chorus_depth", "Chorus depth", 0.0f, 100.0f, 1.0f, 50.0f, " %", 0);
            addFloat ("delay_mix", "Delay mix", 0.0f, 100.0f, 1.0f, 0.0f, " %", 0);
            addFloat ("delay_time", "Delay time", 10.0f, 1000.0f, 1.0f, 250.0f, " ms", 0, 250.0f);
            addFloat ("delay_fb", "Delay feedback", 0.0f, 95.0f, 1.0f, 35.0f, " %", 0);
            group->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { ids::fx (pad, "delay_pp"), version }, "Delay ping-pong", false));
        }

        // --- step 2c: convolution reverb (ONE per pad, chosen from a list, one Wet control)
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::rvType (pad), version }, "Reverb",
                                                                  juce::StringArray { "Aucune", "Plate", "Hall", "Stadium", "SFX", "Reverb perso" }, 0));
        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::rvWet (pad), version }, "Reverb wet", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

        layout.add (std::move (group));
    }

    // --- global voice management
    auto voices = std::make_unique<juce::AudioProcessorParameterGroup> ("voices", "Voices", "|");
    voices->addChild (std::make_unique<AudioParameterInt> (ParameterID { ids::maxVoices, version }, "Max voices", 1, kMaxVoices, kMaxVoices));
    voices->addChild (std::make_unique<AudioParameterInt> (ParameterID { ids::voicesPerPad, version }, "Voices per pad", 1, kMaxVoicesPerPad, kMaxVoicesPerPad));
    voices->addChild (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::stealPolicy, version }, "Voice stealing",
        juce::StringArray { dm::U ("Couper la plus ancienne"), dm::U ("Couper la plus faible"), dm::U ("Ignorer la nouvelle note") }, 0));
    layout.add (std::move (voices));

    // --- master output: volume + limiter
    auto master = std::make_unique<juce::AudioProcessorParameterGroup> ("master", "Master", "|");
    master->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::masterGain, version }, "Master volume", NormalisableRange<float> (-60.0f, 12.0f, 0.01f, 2.5f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.containsIgnoreCase ("inf") ? -60.0f : parseNumber (t); })));
    master->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { ids::limiterOn, version }, "Limiter", true));
    master->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::limiterCeiling, version }, "Limiter ceiling", NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -0.3f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));
    layout.add (std::move (master));
    return layout;
}

// ============================================================================ lifecycle
DaftMugProcessor::DaftMugProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DAFTMUG_STATE", createLayout())
{
    for (auto& s : eqSolo) s.store (-1);
    reverbPoller.startTimerHz (10);
    for (int i = 0; i < kNumPads; ++i)
    {
        auto& p = paramPtrs[(size_t) i];
        p.gain = apvts.getRawParameterValue (ids::gain (i));
        p.pitch = apvts.getRawParameterValue (ids::pitch (i));
        p.pan = apvts.getRawParameterValue (ids::pan (i));
        p.mode = apvts.getRawParameterValue (ids::mode (i));
        p.note = apvts.getRawParameterValue (ids::note (i));
        p.vel = apvts.getRawParameterValue (ids::vel (i));
        p.start = apvts.getRawParameterValue (ids::start (i));
        p.end = apvts.getRawParameterValue (ids::end (i));
        p.attack = apvts.getRawParameterValue (ids::attack (i));
        p.decay = apvts.getRawParameterValue (ids::decay (i));
        p.sustain = apvts.getRawParameterValue (ids::sustain (i));
        p.release = apvts.getRawParameterValue (ids::release (i));
        p.fadeIn = apvts.getRawParameterValue (ids::fadeIn (i));
        p.fadeOut = apvts.getRawParameterValue (ids::fadeOut (i));
        p.reverse = apvts.getRawParameterValue (ids::reverse (i));
        p.velCurve = apvts.getRawParameterValue (ids::velCurve (i));
        p.loopStart = apvts.getRawParameterValue (ids::loopStart (i));
        p.loopFade = apvts.getRawParameterValue (ids::loopFade (i));
        p.choke = apvts.getRawParameterValue (ids::choke (i));
        p.chromatic = apvts.getRawParameterValue (ids::chromatic (i));
        p.eqOn = apvts.getRawParameterValue (ids::eqOn (i));
        p.rvType = apvts.getRawParameterValue (ids::rvType (i));
        p.rvWet = apvts.getRawParameterValue (ids::rvWet (i));
        {
            static const char* keys[] = { "sat_mode", "sat_drive", "sat_out", "sat_mix", "crush_bits", "crush_rate", "crush_mix", "chorus_mix",
                                          "chorus_rate", "chorus_depth", "delay_mix", "delay_time", "delay_fb", "delay_pp" };
            for (int k = 0; k < ParamPtrs::NumFx; ++k)
                p.fx[(size_t) k] = apvts.getRawParameterValue (ids::fx (i, keys[k]));
        }
        for (int b = 0; b < kEqBands; ++b)
        {
            auto& e = p.eq[(size_t) b];
            e.on = apvts.getRawParameterValue (ids::eqBand (i, b, "on"));
            e.type = apvts.getRawParameterValue (ids::eqBand (i, b, "type"));
            e.freq = apvts.getRawParameterValue (ids::eqBand (i, b, "freq"));
            e.gain = apvts.getRawParameterValue (ids::eqBand (i, b, "gain"));
            e.q = apvts.getRawParameterValue (ids::eqBand (i, b, "q"));
            e.dyn = apvts.getRawParameterValue (ids::eqBand (i, b, "dyn"));
            e.thr = apvts.getRawParameterValue (ids::eqBand (i, b, "thr"));
            e.ratio = apvts.getRawParameterValue (ids::eqBand (i, b, "ratio"));
            e.att = apvts.getRawParameterValue (ids::eqBand (i, b, "att"));
            e.rel = apvts.getRawParameterValue (ids::eqBand (i, b, "rel"));
            e.knee = apvts.getRawParameterValue (ids::eqBand (i, b, "knee"));
            e.range = apvts.getRawParameterValue (ids::eqBand (i, b, "range"));
        }
        pads[(size_t) i].colour = defaultPadColour (i);
        loadGeneration[(size_t) i].store (0);
    }

    maxVoicesParam = apvts.getRawParameterValue (ids::maxVoices);
    voicesPerPadParam = apvts.getRawParameterValue (ids::voicesPerPad);
    stealParam = apvts.getRawParameterValue (ids::stealPolicy);
    masterGainParam = apvts.getRawParameterValue (ids::masterGain);
    limiterOnParam = apvts.getRawParameterValue (ids::limiterOn);
    limiterCeilingParam = apvts.getRawParameterValue (ids::limiterCeiling);
    limiter.prepare (44100.0);
}

DaftMugProcessor::~DaftMugProcessor()
{
    reverbPoller.stopTimer();
    loaderPool.removeAllJobs (true, 5000);
}

juce::Colour DaftMugProcessor::defaultPadColour (int pad)
{
    // neon palette taken from the logo: cyan -> blue -> violet -> magenta -> pink
    static const juce::uint32 palette[] = {
        0xff22e4f4, 0xff18c4ff, 0xff2f9bff, 0xff4d6bff,
        0xff7a5cff, 0xffa24dff, 0xffc040ff, 0xffe030f0,
        0xffff3fc8, 0xffff4da6, 0xffff6f9a, 0xff36d6c3,
        0xff59e08a, 0xff3ec1ff, 0xff9a6bff, 0xffd84dff };
    return juce::Colour (palette[pad % 16]);
}

void DaftMugProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate);
    setLatencySamples (limiter.latency());   // constant: the limiter always delays, on or off
    masterGainLast = juce::Decibels::decibelsToGain (masterGainParam->load(), -60.0f);
    engineRate.store (sampleRate);   // impulse responses are rebuilt for the new rate by pollReverb()
}

// ============================================================================ convolution reverb
const char* DaftMugProcessor::irTypeName (int type)
{
    static const char* names[] = { "", "Plate", "Hall", "Stadium", "SFX", "" };
    return names[juce::jlimit (0, kNumIrTypes - 1, type)];
}

juce::StringArray DaftMugProcessor::getIrSearchFolders() const
{
    juce::StringArray f;
    const auto env = juce::SystemStats::getEnvironmentVariable ("DAFTMUG_IR_DIR", {});
    if (env.isNotEmpty()) f.add (env);
    f.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DaftMug").getChildFile ("IR").getFullPathName());
    f.add (juce::String (DAFTMUG_SOURCE_IR_DIR));   // development: the IR folder of the source tree
    return f;
}

juce::File DaftMugProcessor::findIrFile (int type) const
{
    const juce::String base = irTypeName (type);
    if (base.isEmpty()) return {};
    for (const auto& folder : getIrSearchFolders())
        for (const char* ext : { ".wav", ".aif", ".aiff", ".flac" })
        {
            const auto f = juce::File (folder).getChildFile (base + ext);
            if (f.existsAsFile()) return f;
        }
    return {};
}

void DaftMugProcessor::setCustomIr (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= kNumPads) return;
    {
        const juce::ScopedLock lock (metaLock);
        pads[(size_t) pad].customIr = file;
    }
    pollReverb();
    sendChangeMessage();
}

juce::String DaftMugProcessor::getReverbStatus (int pad) const
{
    const juce::ScopedLock lock (metaLock);
    return pads[(size_t) juce::jlimit (0, kNumPads - 1, pad)].reverbStatus;
}

ReverbHandle DaftMugProcessor::createReverbForExport (int pad, double rate) const
{
    if (pad < 0 || pad >= kNumPads || rate <= 0.0) return {};
    const int type = juce::jlimit (0, kNumIrTypes - 1, juce::roundToInt (paramPtrs[(size_t) pad].rvType->load()));
    if (type == 0) return {};
    juce::File file;
    if (type == kNumIrTypes - 1)
    {
        const juce::ScopedLock lock (metaLock);
        file = pads[(size_t) pad].customIr;
    }
    else
        file = findIrFile (type);
    std::shared_ptr<SampleData> data;
    juce::String error;
    if (! file.existsAsFile() || ! decodeFile (file, data, error) || data == nullptr) return {};
    const bool mono = data->left == data->right;
    auto kernel = buildConvKernel (data->left.data(), mono ? nullptr : data->right.data(), data->numFrames(), data->sampleRate, rate);
    return kernel ? std::make_shared<ConvInstance> (kernel) : ReverbHandle();
}

ai::AiService& DaftMugProcessor::getAi()
{
    if (aiService == nullptr)
        aiService = std::make_unique<ai::AiService> (ai::createDefaultSecretStore(), ai::makeDefaultTransport());
    return *aiService;
}

void DaftMugProcessor::setAiForTesting (std::unique_ptr<ai::AiService> service)
{
    aiService = std::move (service);
}

void DaftMugProcessor::pollReverb()
{
    const double rate = engineRate.load();
    if (rate <= 0.0) return;   // not prepared yet

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const auto i = (size_t) pad;
        const int type = juce::jlimit (0, kNumIrTypes - 1, juce::roundToInt (paramPtrs[i].rvType->load()));
        juce::File file;
        if (type == kNumIrTypes - 1)
        {
            const juce::ScopedLock lock (metaLock);
            file = pads[i].customIr;
        }
        else
            file = findIrFile (type);

        ReverbKey key { type, file.getFullPathName(), rate };
        if (key == reverbApplied[i]) continue;
        reverbApplied[i] = key;
        const int generation = ++reverbGeneration[i];

        auto setStatus = [this, i] (const juce::String& s)
        {
            const juce::ScopedLock lock (metaLock);
            pads[i].reverbStatus = s;
        };

        if (type == 0)
        {
            setStatus ({});
            engine.setPadReverb (pad, nullptr);
            continue;
        }
        if (! file.existsAsFile())
        {
            setStatus (type == kNumIrTypes - 1 ? dm::U ("Choisissez un fichier d'IR")
                                               : dm::U ("IR introuvable : déposez ") + juce::String (irTypeName (type)) + dm::U (".wav dans le dossier IR"));
            engine.setPadReverb (pad, nullptr);
            continue;
        }

        const auto cacheKey = file.getFullPathName() + "@" + juce::String (rate) + "@" + juce::String (file.getLastModificationTime().toMilliseconds());
        if (auto it = kernelCache.find (cacheKey); it != kernelCache.end())
        {
            setStatus ({});
            if (! engine.setPadReverb (pad, std::make_shared<ConvInstance> (it->second)))
                reverbApplied[i] = {};   // queue full: try again on the next poll
            continue;
        }

        setStatus (dm::U ("Chargement de l'IR…"));
        juce::WeakReference<DaftMugProcessor> weak (this);
        loaderPool.addJob ([weak, pad, file, rate, generation, cacheKey]
        {
            std::shared_ptr<SampleData> data;
            juce::String error;
            KernelHandle kernel;
            if (decodeFile (file, data, error) && data != nullptr)
            {
                const bool mono = data->left == data->right;
                kernel = buildConvKernel (data->left.data(), mono ? nullptr : data->right.data(), data->numFrames(), data->sampleRate, rate);
                if (! kernel) error = dm::U ("IR vide ou illisible");
            }
            juce::MessageManager::callAsync ([weak, pad, generation, cacheKey, kernel, error]
            {
                auto* self = weak.get();
                if (self == nullptr || generation != self->reverbGeneration[(size_t) pad]) return;
                {
                    const juce::ScopedLock lock (self->metaLock);
                    self->pads[(size_t) pad].reverbStatus = kernel ? juce::String() : dm::U ("IR : ") + error;
                }
                if (kernel)
                {
                    self->kernelCache[cacheKey] = kernel;
                    if (! self->engine.setPadReverb (pad, std::make_shared<ConvInstance> (kernel)))
                        self->reverbApplied[(size_t) pad] = {};
                }
                else
                    self->engine.setPadReverb (pad, nullptr);
                self->sendChangeMessage();
            });
        });
    }
}

bool DaftMugProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* DaftMugProcessor::createEditor()
{
    return new DaftMugEditor (*this);
}

// ============================================================================ audio
PadParams DaftMugProcessor::readPadParams (int pad) const noexcept
{
    const auto& p = paramPtrs[(size_t) pad];
    PadParams out;
    out.gainDb = p.gain->load();
    out.pitchSemitones = p.pitch->load();
    out.pan = p.pan->load();
    out.mode = static_cast<PlayMode> (juce::jlimit (0, 3, juce::roundToInt (p.mode->load())));
    out.midiNote = juce::jlimit (0, 127, juce::roundToInt (p.note->load()));
    out.velocitySensitivity = p.vel->load();
    out.startFraction = p.start->load();
    out.endFraction = p.end->load();
    out.attackSeconds = p.attack->load() * 0.001f;
    out.decaySeconds = p.decay->load() * 0.001f;
    out.sustain = p.sustain->load();
    out.releaseSeconds = p.release->load() * 0.001f;
    out.fadeInSeconds = p.fadeIn->load() * 0.001f;
    out.fadeOutSeconds = p.fadeOut->load() * 0.001f;
    out.reverse = p.reverse->load() >= 0.5f;
    out.velocityCurve = p.velCurve->load();
    out.loopStartFraction = p.loopStart->load();
    out.loopCrossfadeSeconds = p.loopFade->load() * 0.001f;
    out.chokeGroup = juce::jlimit (0, kNumChokeGroups, juce::roundToInt (p.choke->load()));
    out.chromatic = p.chromatic->load() >= 0.5f;
    {
        using P = ParamPtrs;
        auto f = [&] (int k) { return p.fx[(size_t) k]->load(); };
        auto& x = out.fx;
        x.satMode = static_cast<SatMode> (juce::jlimit (0, 3, juce::roundToInt (f (P::SatMode))));
        x.satDriveDb = f (P::SatDrive);   x.satOutDb = f (P::SatOut);   x.satMix = f (P::SatMix) * 0.01f;
        x.crushBits = f (P::CrushBits);   x.crushRate = f (P::CrushRate);   x.crushMix = f (P::CrushMix) * 0.01f;
        x.chorusMix = f (P::ChorusMix) * 0.01f;   x.chorusRateHz = f (P::ChorusRate);   x.chorusDepth = f (P::ChorusDepth) * 0.01f;
        x.delayMix = f (P::DelayMix) * 0.01f;   x.delayMs = f (P::DelayTime);   x.delayFeedback = f (P::DelayFb) * 0.01f;
        x.delayPingPong = f (P::DelayPp) >= 0.5f;
    }
    out.reverbWet = juce::roundToInt (p.rvType->load()) > 0 ? p.rvWet->load() * 0.01f : 0.0f;
    if (previewActive[(size_t) pad].load (std::memory_order_relaxed))
    {
        out.startFraction = previewStart[(size_t) pad].load (std::memory_order_relaxed);
        out.endFraction = previewEnd[(size_t) pad].load (std::memory_order_relaxed);
    }
    out.eq.on = p.eqOn->load() >= 0.5f;
    const bool mutating = mutatePreview[(size_t) pad].active.load (std::memory_order_relaxed);
    if (mutating)
    {
        const auto& mp = mutatePreview[(size_t) pad].v;
        out.pitchSemitones = mp[0].load (std::memory_order_relaxed);
        out.pan = mp[1].load (std::memory_order_relaxed);
        out.gainDb = mp[2].load (std::memory_order_relaxed);
        out.attackSeconds = mp[3].load (std::memory_order_relaxed) * 0.001f;
        if (juce::roundToInt (p.rvType->load()) > 0) out.reverbWet = mp[4].load (std::memory_order_relaxed);
    }
    out.eq.soloBand = eqSolo[(size_t) pad].load (std::memory_order_relaxed);
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto& e = p.eq[(size_t) b];
        auto& o = out.eq.band[(size_t) b];
        o.on = e.on->load() >= 0.5f;
        o.type = static_cast<EqType> (juce::jlimit (0, 4, juce::roundToInt (e.type->load())));
        o.freq = e.freq->load();
        o.gainDb = e.gain->load();
        o.q = e.q->load();
        if (mutating) o.gainDb = mutatePreview[(size_t) pad].v[(size_t) (5 + b)].load (std::memory_order_relaxed);
        o.dynMode = juce::jlimit (0, 2, juce::roundToInt (e.dyn->load()));
        o.thresholdDb = e.thr->load();
        o.ratio = e.ratio->load();
        o.attackMs = e.att->load();
        o.releaseMs = e.rel->load();
        o.kneeDb = e.knee->load();
        o.rangeDb = e.range->load();
    }
    return out;
}

void DaftMugProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    // CPU: time spent in this call relative to the audio time it covers (smoothed)
    struct LoadTimer
    {
        std::atomic<float>& out; double seconds; juce::int64 t0;
        ~LoadTimer()
        {
            if (seconds <= 0.0) return;
            const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            const float load = (float) (used / seconds);
            out.store (out.load (std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
        }
    } loadTimer { cpuLoad, getSampleRate() > 0.0 ? (double) numSamples / getSampleRate() : 0.0, juce::Time::getHighResolutionTicks() };

    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm, std::memory_order_relaxed);

    for (int pad = 0; pad < kNumPads; ++pad)
        engine.setPadParams (pad, readPadParams (pad));

    EngineSettings settings;
    settings.maxVoices = juce::roundToInt (maxVoicesParam->load());
    settings.maxVoicesPerPad = juce::roundToInt (voicesPerPadParam->load());
    settings.stealPolicy = static_cast<StealPolicy> (juce::jlimit (0, 2, juce::roundToInt (stealParam->load())));
    engine.setSettings (settings);

    int numEvents = 0;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int offset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
        if (msg.isNoteOnOrOff())
            midiActivity.fetch_add (1, std::memory_order_relaxed);

        if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            engine.allSoundOff();
        }
        else if (msg.isNoteOn())
        {
            const int armed = learnPad.load();
            if (armed >= 0)
            {
                learnedPad.store (armed);
                learnedNote.store (msg.getNoteNumber());   // applied by the UI thread
                learnPad.store (-1);
                continue;                                  // the learning note itself does not play
            }
            if (numEvents < (int) midiEvents.size())
                midiEvents[(size_t) numEvents++] = { offset, true, msg.getNoteNumber(), msg.getFloatVelocity() };
        }
        else if (msg.isNoteOff())
        {
            if (numEvents < (int) midiEvents.size())
                midiEvents[(size_t) numEvents++] = { offset, false, msg.getNoteNumber(), 0.0f };
        }
    }
    midi.clear();

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples, midiEvents.data(), numEvents);

    // master volume (ramped over the block: no zipper noise) then the look-ahead limiter
    {
        const float target = masterGainParam->load() <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (masterGainParam->load(), -60.0f);
        buffer.applyGainRamp (0, 0, numSamples, masterGainLast, target);
        buffer.applyGainRamp (1, 0, numSamples, masterGainLast, target);
        masterGainLast = target;
        limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples,
                         juce::Decibels::decibelsToGain (limiterCeilingParam->load()), limiterOnParam->load() >= 0.5f);
    }

    // master meter: block peak (max held until the UI reads it) and RMS with a ~300 ms exponential integration
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (1);
        float pl = 0.0f, pr = 0.0f;
        double sl = 0.0, sr = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            pl = juce::jmax (pl, std::abs (l[i]));
            pr = juce::jmax (pr, std::abs (r[i]));
            sl += (double) l[i] * l[i];
            sr += (double) r[i] * r[i];
        }
        if (pl > meterPeakL.load (std::memory_order_relaxed)) meterPeakL.store (pl, std::memory_order_relaxed);
        if (pr > meterPeakR.load (std::memory_order_relaxed)) meterPeakR.store (pr, std::memory_order_relaxed);
        const double rate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        const float a = (float) (1.0 - std::exp (-(double) numSamples / (0.3 * rate)));
        const float ml = meterMsL.load (std::memory_order_relaxed), mr = meterMsR.load (std::memory_order_relaxed);
        meterMsL.store (ml + a * ((float) (sl / numSamples) - ml), std::memory_order_relaxed);
        meterMsR.store (mr + a * ((float) (sr / numSamples) - mr), std::memory_order_relaxed);
    }
}

bool DaftMugProcessor::consumeLearnedNote (int& pad, int& note)
{
    const int p = learnedPad.exchange (-1);
    if (p < 0) return false;
    pad = p;
    note = learnedNote.load();
    return true;
}

// ============================================================================ pads
PadMeta DaftMugProcessor::getPadMeta (int pad) const
{
    const juce::ScopedLock lock (metaLock);
    return pads[(size_t) juce::jlimit (0, kNumPads - 1, pad)];
}

void DaftMugProcessor::loadSampleAsync (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= kNumPads) return;

    const int generation = loadGeneration[(size_t) pad].fetch_add (1) + 1;
    ++tempoGeneration[(size_t) pad];   // a stretch of the previous sample must not land on the new one
    tempoBefore[(size_t) pad].reset();
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        // undo: remember the pad as it was before the FIRST of a series of loads (a load superseded by another keeps that snapshot)
        if (! loadBefore[(size_t) pad].has_value())
        {
            loadBefore[(size_t) pad] = m;
            loadUndoable[(size_t) pad] = history.isRecording();   // false while a project / kit is being restored
        }
        m.file = file;
        m.state = LoadState::Loading;
        m.error.clear();
    }
    sendChangeMessage();

    juce::WeakReference<DaftMugProcessor> weak (this);

    // same file already decoded and still used by another pad: share it instead of decoding (and storing) it again
    if (auto it = sampleCache.find (sampleCacheKey (file)); it != sampleCache.end())
    {
        if (auto cached = it->second.lock())
        {
            juce::MessageManager::callAsync ([weak, pad, file, generation, cached]() mutable
            {
                if (auto* self = weak.get())
                    self->finishLoad (pad, file, generation, std::move (cached), {});
            });
            return;
        }
        sampleCache.erase (it);
    }

    loaderPool.addJob ([weak, pad, file, generation]
    {
        std::shared_ptr<SampleData> data;
        juce::String error;
        if (! decodeFile (file, data, error))
            data.reset();

        juce::MessageManager::callAsync ([weak, pad, file, generation, data, error]() mutable
        {
            if (auto* self = weak.get())
                self->finishLoad (pad, file, generation, std::move (data), error);
        });
    });
}

void DaftMugProcessor::finishLoad (int pad, const juce::File& file, int generation,
                                   std::shared_ptr<SampleData> data, const juce::String& error)
{
    if (generation != loadGeneration[(size_t) pad].load())
        return;   // a newer load (or a clear) superseded this one

    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        if (data != nullptr)
        {
            data->name = file.getFileNameWithoutExtension().toStdString();
            sampleCache[sampleCacheKey (file)] = data;
            for (auto it = sampleCache.begin(); it != sampleCache.end();)   // forget samples nobody uses any more
                it = it->second.expired() ? sampleCache.erase (it) : std::next (it);
            m.sample = data;
            m.original = data;
            m.state = LoadState::Loaded;
            m.error.clear();
            m.tempoMessage.clear();
            m.stretching = false;
            {
                const auto t = detectTempo (*data);   // short samples (one-shots) give bpm = 0
                auto& saved = tempoRestore[(size_t) pad];
                if (saved.valid)   // project restore: keep what the user had chosen
                {
                    m.originalBpm = saved.original;
                    m.targetBpm = saved.target;
                    saved = {};
                }
                else
                {
                    m.originalBpm = t.bpm;
                    m.targetBpm = 0.0;
                }
                m.tempoConfidence = t.confidence;
            }
            if (! m.userRenamed)
                m.name = juce::String (cleanSampleName (data->name));
        }
        else
        {
            m.sample.reset();
            m.original.reset();
            m.originalBpm = 0.0; m.targetBpm = 0.0; m.tempoConfidence = 0.0f;
            m.state = LoadState::Error;
            m.error = error;
        }
    }

    if (! engine.setPadSample (pad, data != nullptr ? SampleHandle (data) : SampleHandle()))
    {
        DBG ("Daft Mug: command queue full, sample not delivered to the engine");
    }
    if (data != nullptr)
        startStretch (pad);   // only does something when a target tempo is set (restored project)

    if (loadBefore[(size_t) pad].has_value())
    {
        PadMeta before = std::move (*loadBefore[(size_t) pad]);
        loadBefore[(size_t) pad].reset();
        if (loadUndoable[(size_t) pad])
        {
            PadMeta after = getPadMeta (pad);
            history.record (dm::U ("Charger un sample"),
                            [this, pad, before] { restoreSampleState (pad, before); },
                            [this, pad, after] { restoreSampleState (pad, after); });
        }
    }

    sendChangeMessage();
}

void DaftMugProcessor::clearPad (int pad)
{
    if (pad < 0 || pad >= kNumPads) return;
    loadGeneration[(size_t) pad].fetch_add (1);   // cancels any load in flight
    ++tempoGeneration[(size_t) pad];
    tempoBefore[(size_t) pad].reset();
    PadMeta before = loadBefore[(size_t) pad].value_or (getPadMeta (pad));
    const bool undoable = ! loadBefore[(size_t) pad].has_value() ? history.isRecording()
                                                                : (loadUndoable[(size_t) pad] && history.isRecording());
    loadBefore[(size_t) pad].reset();
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        m.sample.reset();
        m.original.reset();
        m.originalBpm = 0.0; m.targetBpm = 0.0; m.tempoConfidence = 0.0f; m.stretching = false; m.tempoMessage.clear();
        m.file = juce::File();
        m.state = LoadState::Empty;
        m.error.clear();
        if (! m.userRenamed)
            m.name.clear();
    }
    engine.setPadSample (pad, SampleHandle());
    if (undoable && (before.sample != nullptr || before.state != LoadState::Empty))
    {
        PadMeta after = getPadMeta (pad);
        history.record (dm::U ("Vider le pad"),
                        [this, pad, before] { restoreSampleState (pad, before); },
                        [this, pad, after] { restoreSampleState (pad, after); });
    }
    sendChangeMessage();
}

void DaftMugProcessor::restoreSampleState (int pad, const PadMeta& meta)
{
    loadGeneration[(size_t) pad].fetch_add (1);   // cancels any load / stretch in flight
    ++tempoGeneration[(size_t) pad];
    loadBefore[(size_t) pad].reset();
    tempoBefore[(size_t) pad].reset();
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        m.file = meta.file;
        m.sample = meta.sample;
        m.original = meta.original;
        m.originalBpm = meta.originalBpm;
        m.tempoConfidence = meta.tempoConfidence;
        m.targetBpm = meta.targetBpm;
        m.name = meta.name;
        m.userRenamed = meta.userRenamed;
        m.error = meta.error;
        m.state = meta.sample != nullptr ? LoadState::Loaded : (meta.state == LoadState::Error ? LoadState::Error : LoadState::Empty);
        m.stretching = false;
        m.tempoMessage.clear();
    }
    engine.setPadSample (pad, meta.sample != nullptr ? meta.sample : SampleHandle());
    sendChangeMessage();
}

void DaftMugProcessor::setPadName (int pad, const juce::String& newName)
{
    if (pad < 0 || pad >= kNumPads) return;
    juce::String oldName, newN;
    bool oldRenamed = false, newRenamed = false;
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        oldName = m.name; oldRenamed = m.userRenamed;
        m.name = newName.trim();
        m.userRenamed = m.name.isNotEmpty();
        newN = m.name; newRenamed = m.userRenamed;
    }
    sendChangeMessage();
    if (oldName != newN || oldRenamed != newRenamed)
    {
        auto setName = [this, pad] (const juce::String& n, bool renamed)
        {
            { const juce::ScopedLock lock (metaLock); pads[(size_t) pad].name = n; pads[(size_t) pad].userRenamed = renamed; }
            sendChangeMessage();
        };
        history.record (dm::U ("Renommer le pad"), [setName, oldName, oldRenamed] { setName (oldName, oldRenamed); }, [setName, newN, newRenamed] { setName (newN, newRenamed); });
    }
}

void DaftMugProcessor::setPadColour (int pad, juce::Colour colour)
{
    if (pad < 0 || pad >= kNumPads) return;
    juce::Colour oldColour;
    {
        const juce::ScopedLock lock (metaLock);
        oldColour = pads[(size_t) pad].colour;
        pads[(size_t) pad].colour = colour;
    }
    sendChangeMessage();
    if (oldColour != colour)
    {
        auto setColour = [this, pad] (juce::Colour c)
        {
            { const juce::ScopedLock lock (metaLock); pads[(size_t) pad].colour = c; }
            sendChangeMessage();
        };
        history.record (dm::U ("Couleur du pad"), [setColour, oldColour] { setColour (oldColour); }, [setColour, colour] { setColour (colour); });
    }
}

void DaftMugProcessor::triggerPad (int pad, float velocity)
{
    engine.triggerPad (pad, velocity);
}

void DaftMugProcessor::releasePad (int pad)
{
    engine.releasePad (pad);
}

juce::Result DaftMugProcessor::applySlices (int sourcePad, const std::vector<SliceAssignment>& slices)
{
    const History::Suppress noHistory (history);   // slicing has its own undo (Annuler le découpage)
    if (sourcePad < 0 || sourcePad >= kNumPads) return juce::Result::fail ("Pad invalide");
    if (slices.empty() || slices.size() > (size_t) kNumPads) return juce::Result::fail (dm::U ("Entre 1 et 16 tranches"));

    SampleHandle sample;
    juce::File file;
    {
        const juce::ScopedLock lock (metaLock);
        sample = pads[(size_t) sourcePad].sample;
        file = pads[(size_t) sourcePad].file;
    }
    if (sample == nullptr || ! sample->isValid()) return juce::Result::fail (dm::U ("Le pad source ne contient pas de sample chargé"));

    std::array<bool, kNumPads> used {};
    for (const auto& s : slices)
    {
        if (s.pad < 0 || s.pad >= kNumPads || used[(size_t) s.pad]) return juce::Result::fail (dm::U ("Répartition sur les pads invalide"));
        if (! (s.startFraction >= 0.0 && s.endFraction <= 1.0 && s.endFraction > s.startFraction)) return juce::Result::fail (dm::U ("Tranche invalide"));
        used[(size_t) s.pad] = true;
    }

    // remember what is about to be replaced (one undo level)
    sliceUndo.clear();
    for (const auto& s : slices)
    {
        SliceUndoEntry u;
        u.pad = s.pad;
        {
            const juce::ScopedLock lock (metaLock);
            const auto& m = pads[(size_t) s.pad];
            u.file = m.file;
            u.name = m.name;
            u.renamed = m.userRenamed;
        }
        u.start = apvts.getRawParameterValue (ids::start (s.pad))->load();
        u.end = apvts.getRawParameterValue (ids::end (s.pad))->load();
        sliceUndo.push_back (u);
    }

    auto setParam = [this] (const juce::String& id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    for (const auto& s : slices)
    {
        loadGeneration[(size_t) s.pad].fetch_add (1);   // cancels any load in flight on that pad
        {
            const juce::ScopedLock lock (metaLock);
            auto& m = pads[(size_t) s.pad];
            m.file = file;
            m.sample = sample;
            m.original = sample;
            m.originalBpm = 0.0; m.targetBpm = 0.0; m.tempoConfidence = 0.0f; m.stretching = false; m.tempoMessage.clear();
            ++tempoGeneration[(size_t) s.pad];
            m.state = LoadState::Loaded;
            m.error.clear();
            m.name = s.name;
            m.userRenamed = s.name.isNotEmpty();
        }
        engine.setPadSample (s.pad, sample);
        setParam (ids::start (s.pad), (float) s.startFraction);
        setParam (ids::end (s.pad), (float) s.endFraction);
    }
    sendChangeMessage();
    return juce::Result::ok();
}

// ============================================================================ Mutate
MutateValues DaftMugProcessor::readMutateValues (int pad) const
{
    MutateValues v;
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    v.pitchSemitones = raw (ids::pitch (pad));
    v.pan = raw (ids::pan (pad));
    v.gainDb = raw (ids::gain (pad));
    v.attackMs = raw (ids::attack (pad));
    v.reverbWet = raw (ids::rvWet (pad)) * 0.01f;
    for (int b = 0; b < kEqBands; ++b) v.eqGainDb[(size_t) b] = raw (ids::eqBand (pad, b, "gain"));
    return v;
}

bool DaftMugProcessor::padHasReverb (int pad) const
{
    return pad >= 0 && pad < kNumPads && juce::roundToInt (paramPtrs[(size_t) pad].rvType->load()) > 0;
}

void DaftMugProcessor::setPreviewMutation (int pad, const MutateValues& v) noexcept
{
    if (pad < 0 || pad >= kNumPads) return;
    auto& m = mutatePreview[(size_t) pad];
    m.v[0].store (v.pitchSemitones); m.v[1].store (v.pan); m.v[2].store (v.gainDb); m.v[3].store (v.attackMs); m.v[4].store (v.reverbWet);
    for (size_t b = 0; b < 4; ++b) m.v[5 + b].store (v.eqGainDb[b]);
    m.active.store (true);
}

static void writeParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void DaftMugProcessor::applyMutation (int pad, const MutateValues& v)
{
    if (pad < 0 || pad >= kNumPads) return;
    mutateUndoPad = pad;
    mutateUndoValues = readMutateValues (pad);
    clearPreviewMutation (pad);
    writeParam (apvts, ids::pitch (pad), v.pitchSemitones);
    writeParam (apvts, ids::pan (pad), v.pan);
    writeParam (apvts, ids::gain (pad), v.gainDb);
    writeParam (apvts, ids::attack (pad), v.attackMs);
    if (padHasReverb (pad)) writeParam (apvts, ids::rvWet (pad), v.reverbWet * 100.0f);
    for (int b = 0; b < kEqBands; ++b) writeParam (apvts, ids::eqBand (pad, b, "gain"), v.eqGainDb[(size_t) b]);
    sendChangeMessage();
}

void DaftMugProcessor::undoMutation()
{
    if (mutateUndoPad < 0) return;
    const int pad = mutateUndoPad;
    const auto& v = mutateUndoValues;
    mutateUndoPad = -1;
    writeParam (apvts, ids::pitch (pad), v.pitchSemitones);
    writeParam (apvts, ids::pan (pad), v.pan);
    writeParam (apvts, ids::gain (pad), v.gainDb);
    writeParam (apvts, ids::attack (pad), v.attackMs);
    if (padHasReverb (pad)) writeParam (apvts, ids::rvWet (pad), v.reverbWet * 100.0f);
    for (int b = 0; b < kEqBands; ++b) writeParam (apvts, ids::eqBand (pad, b, "gain"), v.eqGainDb[(size_t) b]);
    sendChangeMessage();
}

void DaftMugProcessor::undoSlices()
{
    const History::Suppress noHistory (history);
    const auto entries = sliceUndo;
    sliceUndo.clear();
    for (const auto& u : entries)
    {
        for (auto [id, v] : { std::pair<juce::String, float> { ids::start (u.pad), u.start }, { ids::end (u.pad), u.end } })
            if (auto* prm = apvts.getParameter (id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                prm->endChangeGesture();
            }
        if (u.file != juce::File())
        {
            setPadName (u.pad, u.renamed ? u.name : juce::String());
            loadSampleAsync (u.pad, u.file);
        }
        else
        {
            setPadName (u.pad, juce::String());   // forget the slice name (clearPad keeps user-given names)
            clearPad (u.pad);
        }
    }
    sendChangeMessage();
}

// ============================================================================ tempo
void DaftMugProcessor::setPadTempo (int pad, double originalBpm, double targetBpm)
{
    if (pad < 0 || pad >= kNumPads) return;
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        if (m.original == nullptr) return;
        if (! tempoBefore[(size_t) pad].has_value() && history.isRecording())
            tempoBefore[(size_t) pad] = m;   // recorded when the (possibly asynchronous) change is complete
        m.originalBpm = juce::jlimit (0.0, 400.0, originalBpm);
        m.targetBpm = juce::jlimit (0.0, 400.0, targetBpm);
    }
    startStretch (pad);
}

void DaftMugProcessor::redetectTempo (int pad)
{
    if (pad < 0 || pad >= kNumPads) return;
    SampleHandle original;
    {
        const juce::ScopedLock lock (metaLock);
        original = pads[(size_t) pad].original;
    }
    if (original == nullptr) return;
    const auto t = detectTempo (*original);
    double target = 0.0;
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        if (! tempoBefore[(size_t) pad].has_value() && history.isRecording())
            tempoBefore[(size_t) pad] = m;
        m.originalBpm = t.bpm;
        m.tempoConfidence = t.confidence;
        target = m.targetBpm;
        m.tempoMessage = t.bpm > 0.0 ? juce::String() : dm::U ("Tempo non détecté (sample trop court ou sans rythme) : saisissez-le à la main.");
    }
    if (target > 0.0) startStretch (pad);
    else sendChangeMessage();
}

void DaftMugProcessor::startStretch (int pad)
{
    SampleHandle original;
    double orig = 0.0, target = 0.0;
    {
        const juce::ScopedLock lock (metaLock);
        const auto& m = pads[(size_t) pad];
        original = m.original; orig = m.originalBpm; target = m.targetBpm;
    }
    if (original == nullptr) return;
    const int generation = ++tempoGeneration[(size_t) pad];

    const double ratio = (orig > 0.0 && target > 0.0) ? orig / target : 1.0;
    const bool identity = std::abs (ratio - 1.0) < 0.002;
    if (target <= 0.0 || orig <= 0.0 || identity || ratio < 0.5 || ratio > 2.0)
    {
        // back to the original audio (or refuse an out-of-range ratio)
        {
            const juce::ScopedLock lock (metaLock);
            auto& m = pads[(size_t) pad];
            m.sample = original;
            m.stretching = false;
            m.tempoMessage = (target > 0.0 && orig > 0.0 && ! identity) ? dm::U ("Écart de tempo trop grand : limite ×0,5 à ×2 du tempo d'origine.") : juce::String();
        }
        engine.setPadSample (pad, original);
        commitTempoHistory (pad);
        sendChangeMessage();
        return;
    }

    {
        const juce::ScopedLock lock (metaLock);
        pads[(size_t) pad].stretching = true;
        pads[(size_t) pad].tempoMessage.clear();
    }
    sendChangeMessage();

    juce::WeakReference<DaftMugProcessor> weak (this);
    loaderPool.addJob ([weak, pad, generation, original, ratio]
    {
        auto out = timeStretch (*original, ratio);
        juce::MessageManager::callAsync ([weak, pad, generation, original, out]() mutable
        {
            if (auto* self = weak.get())
                self->finishStretch (pad, generation, original, std::move (out));
        });
    });
}

void DaftMugProcessor::finishStretch (int pad, int generation, SampleHandle original, std::shared_ptr<SampleData> stretched)
{
    if (generation != tempoGeneration[(size_t) pad])
        return;   // superseded
    SampleHandle toPlay = original;
    {
        const juce::ScopedLock lock (metaLock);
        auto& m = pads[(size_t) pad];
        if (m.original != original) return;   // the pad got another sample meanwhile
        m.stretching = false;
        if (stretched != nullptr) { m.sample = SampleHandle (stretched); toPlay = m.sample; m.tempoMessage.clear(); }
        else { m.sample = original; m.tempoMessage = dm::U ("Étirement impossible : l'audio d'origine est conservé."); }
    }
    engine.setPadSample (pad, toPlay);
    commitTempoHistory (pad);
    sendChangeMessage();
}

void DaftMugProcessor::commitTempoHistory (int pad)
{
    if (! tempoBefore[(size_t) pad].has_value()) return;
    PadMeta before = std::move (*tempoBefore[(size_t) pad]);
    tempoBefore[(size_t) pad].reset();
    PadMeta after = getPadMeta (pad);
    history.record (dm::U ("Tempo du sample"),
                    [this, pad, before] { restoreSampleState (pad, before); },
                    [this, pad, after] { restoreSampleState (pad, after); });
}

size_t DaftMugProcessor::getSampleMemoryBytes() const
{
    std::vector<const SampleData*> seen;
    size_t total = 0;
    const juce::ScopedLock lock (metaLock);
    for (const auto& m : pads)
        for (const auto& h : { m.sample, m.original })
            if (h != nullptr && std::find (seen.begin(), seen.end(), h.get()) == seen.end())
            {
                seen.push_back (h.get());
                total += (h->left.size() + h->right.size()) * sizeof (float);
            }
    return total;
}

void DaftMugProcessor::housekeeping()
{
    engine.collectGarbage();
    pollReverb();
}

// ============================================================================ state
// The saved state contains parameters + pad names/colours/file paths. It NEVER contains
// secrets (the optional Gemini API key will live in the OS secure store, not here).
void DaftMugProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    if (auto old = state.getChildWithName ("DAFTMUG"); old.isValid())
        state.removeChild (old, nullptr);

    juce::ValueTree ui ("DAFTMUG");
    ui.setProperty ("schema", kStateSchemaVersion, nullptr);
    {
        const juce::ScopedLock lock (metaLock);
        for (int i = 0; i < kNumPads; ++i)
        {
            const auto& m = pads[(size_t) i];
            juce::ValueTree p ("PAD");
            p.setProperty ("index", i, nullptr);
            p.setProperty ("name", m.name, nullptr);
            p.setProperty ("renamed", m.userRenamed, nullptr);
            p.setProperty ("colour", juce::String::toHexString ((int) m.colour.getARGB()), nullptr);
            p.setProperty ("file", m.file.getFullPathName(), nullptr);
            p.setProperty ("ir", m.customIr.getFullPathName(), nullptr);
            if (m.targetBpm > 0.0 && m.originalBpm > 0.0)
            {
                p.setProperty ("bpmOrig", m.originalBpm, nullptr);
                p.setProperty ("bpmTarget", m.targetBpm, nullptr);
            }
            ui.appendChild (p, nullptr);
        }
    }
    state.appendChild (ui, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DaftMugProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    juce::WeakReference<DaftMugProcessor> weak (this);

    auto apply = [weak, tree]
    {
        if (auto* self = weak.get())
            self->applyStateTree (tree);
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}

void DaftMugProcessor::applyStateTree (const juce::ValueTree& tree)
{
    const History::Suppress noHistory (history);   // restoring a whole project: nothing of it is undoable...
    history.clear();                               // ...and the previous history no longer applies
    for (auto& b : loadBefore) b.reset();
    for (auto& b : tempoBefore) b.reset();

    auto ui = tree.getChildWithName ("DAFTMUG");
    const int schema = ui.isValid() ? (int) ui.getProperty ("schema", 0) : 0;

    // Migration hook: states written by older versions are upgraded here.
    // (schema 0 = no pad information; schema 1 = current.)
    juce::ignoreUnused (schema);

    apvts.replaceState (tree);

    if (! ui.isValid())
        return;


    for (auto p : ui)
    {
        if (! p.hasType ("PAD")) continue;
        const int i = p.getProperty ("index", -1);
        if (i < 0 || i >= kNumPads) continue;

        const juce::File file (p.getProperty ("file").toString());
        {
            const juce::ScopedLock lock (metaLock);
            auto& m = pads[(size_t) i];
            m.name = p.getProperty ("name").toString();
            m.userRenamed = (bool) p.getProperty ("renamed", false);
            m.colour = juce::Colour ((juce::uint32) p.getProperty ("colour").toString().getHexValue64());
            if (m.colour.isTransparent()) m.colour = defaultPadColour (i);
            m.customIr = juce::File (p.getProperty ("ir").toString());
            tempoRestore[(size_t) i] = {};
            if (p.hasProperty ("bpmTarget"))
                tempoRestore[(size_t) i] = { true, (double) p.getProperty ("bpmOrig", 0.0), (double) p.getProperty ("bpmTarget", 0.0) };
        }

        if (file != juce::File() )
            loadSampleAsync (i, file);   // reports "Fichier introuvable" if the file moved
        else
            clearPad (i);
    }
    sendChangeMessage();
}
} // namespace dm

// Entry point used by the JUCE plugin / standalone wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dm::DaftMugProcessor();
}
