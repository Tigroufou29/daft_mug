#include "PadExporter.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "SamplerEngine.h"

namespace dm
{
juce::Result exportPadToWav (const SampleHandle& sample, PadParams params,
                             const ExportOptions& options, const juce::File& destination)
{
    if (sample == nullptr || ! sample->isValid())
        return juce::Result::fail ("Aucun sample a exporter");
    if (options.bits != 16 && options.bits != 24 && options.bits != 32)
        return juce::Result::fail ("Profondeur de bits non prise en charge");

    const double outRate = options.sampleRate > 0.0 ? options.sampleRate : sample->sampleRate;

    params.eq.soloBand = -1;             // the listening aid never reaches an exported file
    params.mode = PlayMode::OneShot;
    params.chromatic = false;            // an export renders the pad as the pad itself
    params.chokeGroup = 0;
    params.midiNote = kFirstMidiNote;
    params.velocitySensitivity = 0.0f;   // full level, independent of velocity
    params.declickSeconds = 0.0f;        // no extra anti-click: the file starts exactly at the region start

    const double s0 = std::clamp ((double) params.startFraction, 0.0, 1.0);
    const double s1 = std::clamp ((double) params.endFraction, 0.0, 1.0);
    const double regionFrames = std::abs (s1 - s0) * (double) sample->numFrames();
    const double speed = std::pow (2.0, (double) params.pitchSemitones / 12.0) * sample->sampleRate / outRate;
    // An active EQ rings a little after the last sample: render a short tail so the file is not cut abruptly.
    bool eqActive = false;
    for (const auto& b : params.eq.band)
        eqActive = eqActive || ! isBandNeutral (b);
    eqActive = eqActive && params.eq.on;
    double tailSeconds = eqActive ? 0.15 : 0.0;
    tailSeconds = std::max (tailSeconds, std::min (10.0, (double) fxTailSeconds (params.fx)) + 0.05);
    if (options.reverb != nullptr && params.reverbWet > 0.0f)
        tailSeconds = std::max (tailSeconds, (double) options.reverb->tailFrames() / outRate + 0.05);
    else
        params.reverbWet = 0.0f;
    if (fxTailSeconds (params.fx) <= 0.0f && ! eqActive && params.reverbWet <= 0.0f)
        tailSeconds = 0.0;
    const auto tailFrames = static_cast<size_t> (tailSeconds * outRate);

    const auto totalFrames = static_cast<size_t> (std::ceil (regionFrames / speed)) + 16 + tailFrames;
    if (totalFrames > (size_t) (outRate * 3600.0))
        return juce::Result::fail ("Resultat trop long");

    SamplerEngine engine;
    engine.prepare (outRate, 512);
    engine.setPadSample (0, sample);
    if (options.reverb != nullptr && params.reverbWet > 0.0f)
        engine.setPadReverb (0, options.reverb);
    engine.setPadParams (0, params);

    std::vector<float> left (totalFrames, 0.0f), right (totalFrames, 0.0f);
    size_t pos = 0, voicesEndedAt = 0;
    bool first = true, voicesEnded = false;
    while (pos < totalFrames)
    {
        const int n = (int) std::min<size_t> (512, totalFrames - pos);
        const MidiEvent start { 0, true, kFirstMidiNote, 1.0f };
        engine.process (left.data() + pos, right.data() + pos, n, first ? &start : nullptr, first ? 1 : 0);
        first = false;
        pos += (size_t) n;
        if (! voicesEnded && engine.activeVoices() == 0)
        {
            voicesEnded = true;
            voicesEndedAt = pos;
        }
        if (voicesEnded && pos >= voicesEndedAt + tailFrames)
            break;
    }
    const size_t rendered = std::max<size_t> (1, pos);

    float peak = 0.0f;
    for (size_t i = 0; i < rendered; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });

    float gain = 1.0f;
    if (options.normalize && peak > 1.0e-6f)
        gain = 0.98855f / peak;   // -0.1 dBFS

    if (options.bits < 32)
        for (size_t i = 0; i < rendered; ++i)
        {
            left[i] = std::clamp (left[i] * gain, -1.0f, 1.0f);
            right[i] = std::clamp (right[i] * gain, -1.0f, 1.0f);
        }
    else if (! juce::approximatelyEqual (gain, 1.0f))
        for (size_t i = 0; i < rendered; ++i) { left[i] *= gain; right[i] *= gain; }

    destination.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();
    if (stream == nullptr)
        return juce::Result::fail ("Impossible d'ecrire le fichier (droits ou chemin)");

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate (outRate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (options.bits)
                                                   .withSampleFormat (options.bits == 32
                                                                          ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                          : juce::AudioFormatWriterOptions::SampleFormat::integral));
    if (writer == nullptr)
        return juce::Result::fail ("Frequence d'echantillonnage non prise en charge par le format WAV");

    const float* channels[2] = { left.data(), right.data() };
    if (! writer->writeFromFloatArrays (channels, 2, (int) rendered))
        return juce::Result::fail ("Erreur d'ecriture du fichier");

    return juce::Result::ok();
}
} // namespace dm
