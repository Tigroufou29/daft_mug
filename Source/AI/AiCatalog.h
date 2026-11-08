#pragma once

#include <vector>

#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm::ai
{
// The ONLY parameters an AI proposal may touch (white list). Ranges and choice labels are read from the real plug-in
// parameters, so the white list can never drift from what the audio engine really accepts.
struct ParamInfo
{
    juce::String key;            // logical name used with the AI, e.g. "eq2_gain"
    juce::String paramId;        // real parameter id for the chosen pad
    juce::String description;    // short, factual (also sent to the model)
    juce::String unit;
    bool isChoice = false, isBool = false;
    float minValue = 0.0f, maxValue = 0.0f, defaultValue = 0.0f;
    juce::StringArray choices;   // for choice parameters (value = index)
    juce::RangedAudioParameter* param = nullptr;
};

class Catalog
{
public:
    Catalog (DaftMugProcessor&, int pad);

    const std::vector<ParamInfo>& all() const noexcept { return params; }
    const ParamInfo* find (const juce::String& key) const;

    float currentValue (const ParamInfo&) const;                 // real (denormalised) value
    void setValue (const ParamInfo&, float value, bool gesture = true) const;   // value must already be validated

    // compact text for the prompt: key, range, unit, current value, meaning; plus the list of things the engine can NOT do
    juce::String describeForPrompt() const;
    juce::String formatValue (const ParamInfo&, float value) const;   // "−3.0 dB", "Hall"

private:
    std::vector<ParamInfo> params;
};

// What the model returns, once validated.
struct Step
{
    juce::String name, key, explanation, expected;
    float value = 0.0f;
};

struct Rejected
{
    juce::String what, reason;
};

struct Recipe
{
    juce::String title, summary, unsupported;
    std::vector<Step> steps;
    std::vector<Rejected> rejected;
};

// Parses and validates the JSON text returned by the model. Strict: any unknown field, wrong type or oversized list refuses the
// WHOLE answer; individual steps with an unknown parameter or an out-of-range value are rejected one by one (never clamped,
// never applied) and reported. Text fields are stripped of control characters and limited in length.
juce::Result parseRecipe (const juce::String& json, const Catalog&, Recipe& out);

// JSON schema (Gemini "responseSchema" dialect) of a recipe, and of a plain explanation.
juce::var recipeSchema (const Catalog&);
juce::var explanationSchema();
juce::Result parseExplanation (const juce::String& json, juce::String& out);

// Snapshot / restore of every white-listed parameter of the pad (used for preview, compare, accept, undo).
struct Snapshot
{
    std::vector<float> normalised;
    bool valid = false;
};
Snapshot takeSnapshot (const Catalog&);
void restoreSnapshot (const Catalog&, const Snapshot&);
void applyRecipe (const Catalog&, const Recipe&);

constexpr int kMaxSteps = 60;
} // namespace dm::ai
