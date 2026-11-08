#include "AiCatalog.h"

#include "ParameterIds.h"

namespace dm::ai
{
namespace
{
struct Def { const char* key; const char* description; };

// logical key -> real id builder is done in the constructor (switch on key) to stay close to ParameterIds.h
juce::String idFor (const juce::String& key, int pad)
{
    if (key == "pitch") return ids::pitch (pad);
    if (key == "volume") return ids::gain (pad);
    if (key == "pan") return ids::pan (pad);
    if (key == "attack") return ids::attack (pad);
    if (key == "decay") return ids::decay (pad);
    if (key == "sustain") return ids::sustain (pad);
    if (key == "release") return ids::release (pad);
    if (key == "fade_in") return ids::fadeIn (pad);
    if (key == "fade_out") return ids::fadeOut (pad);
    if (key == "reverse") return ids::reverse (pad);
    if (key == "eq_on") return ids::eqOn (pad);
    if (key == "reverb_type") return ids::rvType (pad);
    if (key == "reverb_wet") return ids::rvWet (pad);
    if (key.startsWith ("eq") && key.length() > 4 && key[3] == '_')
    {
        const int band = key.substring (2, 3).getIntValue() - 1;
        return ids::eqBand (pad, band, key.substring (4).toRawUTF8());
    }
    return ids::fx (pad, key.toRawUTF8());   // sat_*, crush_*, chorus_*, delay_*
}

const Def kDefs[] = {
    { "pitch", "pitch shift in semitones (also changes duration: lower = deeper and longer)" },
    { "volume", "pad level in dB" },
    { "pan", "stereo position (-1 left, 0 centre, +1 right)" },
    { "attack", "envelope attack: time to fade the sound in at the start, in ms" },
    { "decay", "envelope decay after the attack, in ms" },
    { "sustain", "envelope sustain level (%), used with decay" },
    { "release", "envelope release after note-off, in ms" },
    { "fade_in", "fade-in applied to the start of the selected region, in ms" },
    { "fade_out", "fade-out applied to the end of the selected region, in ms" },
    { "reverse", "plays the sample backwards (0 = off, 1 = on)" },
    { "eq_on", "master switch of the 4-band EQ (0 = bypass, 1 = active)" },
    { "eq1_on", "EQ band 1 on/off" }, { "eq1_type", "EQ band 1 shape" }, { "eq1_freq", "EQ band 1 frequency" }, { "eq1_gain", "EQ band 1 gain (dB)" }, { "eq1_q", "EQ band 1 Q (narrowness)" },
    { "eq2_on", "EQ band 2 on/off" }, { "eq2_type", "EQ band 2 shape" }, { "eq2_freq", "EQ band 2 frequency" }, { "eq2_gain", "EQ band 2 gain (dB)" }, { "eq2_q", "EQ band 2 Q (narrowness)" },
    { "eq3_on", "EQ band 3 on/off" }, { "eq3_type", "EQ band 3 shape" }, { "eq3_freq", "EQ band 3 frequency" }, { "eq3_gain", "EQ band 3 gain (dB)" }, { "eq3_q", "EQ band 3 Q (narrowness)" },
    { "eq4_on", "EQ band 4 on/off" }, { "eq4_type", "EQ band 4 shape" }, { "eq4_freq", "EQ band 4 frequency" }, { "eq4_gain", "EQ band 4 gain (dB)" }, { "eq4_q", "EQ band 4 Q (narrowness)" },
    { "sat_mode", "saturation character" }, { "sat_drive", "saturation drive (dB)" }, { "sat_out", "saturation output compensation (dB)" }, { "sat_mix", "saturation dry/wet (%)" },
    { "crush_bits", "bitcrusher bit depth" }, { "crush_rate", "bitcrusher sample-rate reduction factor" }, { "crush_mix", "bitcrusher dry/wet (%)" },
    { "chorus_mix", "chorus dry/wet (%)" }, { "chorus_rate", "chorus modulation rate (Hz)" }, { "chorus_depth", "chorus depth (%)" },
    { "delay_mix", "delay dry/wet (%)" }, { "delay_time", "delay time (ms)" }, { "delay_fb", "delay feedback (%)" }, { "delay_pp", "delay ping-pong (0/1)" },
    { "reverb_type", "convolution reverb: which impulse response (index = label)" },
    { "reverb_wet", "reverb wet amount (%) - the dry sound is untouched" },
};

// plain text only: control characters become spaces, the rest (accents included) is kept; the UI never interprets markup
juce::String cleanText (const juce::String& in, int maxLen)
{
    juce::String out;
    for (auto c : in)
    {
        if (c < 32 || c == 127) c = ' ';
        out << juce::String::charToString (c);
        if (out.length() >= maxLen) break;
    }
    return out.trim();
}
} // namespace

Catalog::Catalog (DaftMugProcessor& p, int pad)
{
    for (const auto& d : kDefs)
    {
        ParamInfo info;
        info.key = d.key;
        info.paramId = idFor (info.key, pad);
        info.description = d.description;
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (info.paramId));
        if (prm == nullptr) continue;   // a parameter that does not exist is simply not offered
        info.param = prm;
        const auto range = prm->getNormalisableRange();
        info.minValue = range.start;
        info.maxValue = range.end;
        info.defaultValue = prm->convertFrom0to1 (prm->getDefaultValue());
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (prm)) { info.isChoice = true; info.choices = c->choices; info.minValue = 0.0f; info.maxValue = (float) c->choices.size() - 1.0f; }
        else if (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr) { info.isBool = true; info.minValue = 0.0f; info.maxValue = 1.0f; }
        params.push_back (info);
    }
}

const ParamInfo* Catalog::find (const juce::String& key) const
{
    for (const auto& p : params) if (p.key == key) return &p;
    return nullptr;
}

float Catalog::currentValue (const ParamInfo& p) const
{
    return p.param->convertFrom0to1 (p.param->getValue());
}

void Catalog::setValue (const ParamInfo& p, float value, bool gesture) const
{
    if (gesture) p.param->beginChangeGesture();
    p.param->setValueNotifyingHost (p.param->convertTo0to1 (juce::jlimit (p.minValue, p.maxValue, value)));
    if (gesture) p.param->endChangeGesture();
}

juce::String Catalog::formatValue (const ParamInfo& p, float value) const
{
    if (p.isChoice) return p.choices[juce::jlimit (0, p.choices.size() - 1, juce::roundToInt (value))];
    if (p.isBool) return value >= 0.5f ? "on" : "off";
    return p.param->getText (p.param->convertTo0to1 (value), 0);
}

juce::String Catalog::describeForPrompt() const
{
    juce::String s;
    for (const auto& p : params)
    {
        s << "- " << p.key << ": " << p.description << ". ";
        if (p.isChoice)
        {
            s << "Allowed integer values: ";
            for (int i = 0; i < p.choices.size(); ++i) s << i << "=" << p.choices[i] << (i + 1 < p.choices.size() ? ", " : "");
        }
        else if (p.isBool) s << "Allowed values: 0 or 1";
        else s << "Range " << juce::String (p.minValue, 2) << " to " << juce::String (p.maxValue, 2);
        s << ". Current: " << (p.isChoice ? juce::String (juce::roundToInt (currentValue (p))) + "=" + formatValue (p, currentValue (p))
                                          : juce::String (currentValue (p), 2)) << "\n";
    }
    return s;
}

// ------------------------------------------------------------------------------------------------ parsing
namespace
{
juce::Result strictObject (const juce::var& v, std::initializer_list<const char*> allowed, juce::DynamicObject*& out, const juce::String& where)
{
    out = v.getDynamicObject();
    if (out == nullptr) return juce::Result::fail (where + dm::U (" : objet attendu"));
    for (const auto& prop : out->getProperties())
    {
        bool ok = false;
        for (const char* a : allowed) if (prop.name.toString() == a) ok = true;
        if (! ok) return juce::Result::fail (dm::U ("Champ inconnu refusé : ") + cleanText (prop.name.toString(), 40) + " (" + where + ")");
    }
    return juce::Result::ok();
}
}

juce::Result parseRecipe (const juce::String& json, const Catalog& catalog, Recipe& out)
{
    out = {};
    if (json.length() > 200000) return juce::Result::fail (dm::U ("Réponse trop volumineuse"));
    const auto root = juce::JSON::parse (json);
    if (root.isVoid()) return juce::Result::fail (dm::U ("La réponse de l'IA n'est pas un JSON valide"));

    juce::DynamicObject* obj = nullptr;
    if (auto r = strictObject (root, { "title", "summary", "unsupported", "steps" }, obj, "recette"); r.failed()) return r;

    out.title = cleanText (obj->getProperty ("title").toString(), 100);
    out.summary = cleanText (obj->getProperty ("summary").toString(), 600);
    out.unsupported = cleanText (obj->getProperty ("unsupported").toString(), 600);

    const auto stepsVar = obj->getProperty ("steps");
    if (! stepsVar.isArray()) return juce::Result::fail (dm::U ("Liste d'étapes absente"));
    if (stepsVar.size() > kMaxSteps) return juce::Result::fail (dm::U ("Trop d'étapes proposées (maximum ") + juce::String (kMaxSteps) + ")");

    for (int i = 0; i < stepsVar.size(); ++i)
    {
        juce::DynamicObject* so = nullptr;
        if (auto r = strictObject (stepsVar[i], { "name", "param", "value", "explanation", "expected" }, so, "étape " + juce::String (i + 1)); r.failed()) return r;

        Step st;
        st.name = cleanText (so->getProperty ("name").toString(), 80);
        st.key = so->getProperty ("param").toString().trim();
        st.explanation = cleanText (so->getProperty ("explanation").toString(), 400);
        st.expected = cleanText (so->getProperty ("expected").toString(), 300);
        const auto label = st.name.isEmpty() ? st.key : st.name;

        const auto valueVar = so->getProperty ("value");
        if (! (valueVar.isInt() || valueVar.isInt64() || valueVar.isDouble() || valueVar.isBool()))
        {
            out.rejected.push_back ({ label, dm::U ("valeur non numérique") });
            continue;
        }
        const auto* info = catalog.find (st.key);
        if (info == nullptr)
        {
            out.rejected.push_back ({ label, dm::U ("paramètre non autorisé : ") + cleanText (st.key, 40) });
            continue;
        }
        const double v = (double) valueVar;
        if (! std::isfinite (v) || v < (double) info->minValue - 1.0e-6 || v > (double) info->maxValue + 1.0e-6)
        {
            out.rejected.push_back ({ label, dm::U ("valeur hors plage pour ") + info->key + " (" + juce::String (info->minValue, 2) + " à " + juce::String (info->maxValue, 2) + ")" });
            continue;
        }
        if ((info->isChoice || info->isBool) && std::abs (v - std::round (v)) > 1.0e-6)
        {
            out.rejected.push_back ({ label, dm::U ("valeur entière attendue pour ") + info->key });
            continue;
        }
        st.value = (float) v;
        out.steps.push_back (st);
    }
    return juce::Result::ok();
}

juce::var recipeSchema (const Catalog& catalog)
{
    auto prop = [] (const char* type, const char* desc)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", type);
        o->setProperty ("description", desc);
        return juce::var (o);
    };
    juce::Array<juce::var> keys;
    for (const auto& p : catalog.all()) keys.add (p.key);

    auto* paramProp = new juce::DynamicObject();
    paramProp->setProperty ("type", "STRING");
    paramProp->setProperty ("enum", juce::var (keys));
    paramProp->setProperty ("description", "Parameter key, exactly one of the allowed keys");

    auto* stepProps = new juce::DynamicObject();
    stepProps->setProperty ("name", prop ("STRING", "Short name of the step"));
    stepProps->setProperty ("param", juce::var (paramProp));
    stepProps->setProperty ("value", prop ("NUMBER", "Proposed value, inside the allowed range"));
    stepProps->setProperty ("explanation", prop ("STRING", "One or two concise sentences: why this setting"));
    stepProps->setProperty ("expected", prop ("STRING", "Expected audible result"));
    auto* stepObj = new juce::DynamicObject();
    stepObj->setProperty ("type", "OBJECT");
    stepObj->setProperty ("properties", juce::var (stepProps));
    stepObj->setProperty ("required", juce::Array<juce::var> { "name", "param", "value", "explanation", "expected" });

    auto* steps = new juce::DynamicObject();
    steps->setProperty ("type", "ARRAY");
    steps->setProperty ("items", juce::var (stepObj));

    auto* props = new juce::DynamicObject();
    props->setProperty ("title", prop ("STRING", "Title of the proposal"));
    props->setProperty ("summary", prop ("STRING", "Short summary of the approach and of the expected sound"));
    props->setProperty ("unsupported", prop ("STRING", "Anything requested that Daft Mug cannot do, with a feasible alternative. Empty string if none"));
    props->setProperty ("steps", juce::var (steps));

    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "OBJECT");
    root->setProperty ("properties", juce::var (props));
    root->setProperty ("required", juce::Array<juce::var> { "title", "summary", "unsupported", "steps" });
    return juce::var (root);
}

juce::var explanationSchema()
{
    auto* answer = new juce::DynamicObject();
    answer->setProperty ("type", "STRING");
    answer->setProperty ("description", "Clear, friendly explanation for a beginner, with precise details for experts");
    auto* props = new juce::DynamicObject();
    props->setProperty ("answer", juce::var (answer));
    auto* root = new juce::DynamicObject();
    root->setProperty ("type", "OBJECT");
    root->setProperty ("properties", juce::var (props));
    root->setProperty ("required", juce::Array<juce::var> { "answer" });
    return juce::var (root);
}

juce::Result parseExplanation (const juce::String& json, juce::String& out)
{
    out.clear();
    const auto root = juce::JSON::parse (json);
    if (root.isVoid()) return juce::Result::fail (dm::U ("La réponse de l'IA n'est pas un JSON valide"));
    juce::DynamicObject* obj = nullptr;
    if (auto r = strictObject (root, { "answer" }, obj, "explication"); r.failed()) return r;
    out = obj->getProperty ("answer").toString().removeCharacters (juce::String::charToString (0)).substring (0, 4000).trim();
    // keep newlines for readability, drop other control characters
    juce::String clean;
    for (auto c : out) if (c == '\n' || c >= 32) clean << juce::String::charToString (c);
    out = clean;
    return out.isEmpty() ? juce::Result::fail (dm::U ("Réponse vide")) : juce::Result::ok();
}

Snapshot takeSnapshot (const Catalog& catalog)
{
    Snapshot s;
    for (const auto& p : catalog.all()) s.normalised.push_back (p.param->getValue());
    s.valid = true;
    return s;
}

void restoreSnapshot (const Catalog& catalog, const Snapshot& s)
{
    if (! s.valid || s.normalised.size() != catalog.all().size()) return;
    for (size_t i = 0; i < s.normalised.size(); ++i)
    {
        const auto& p = catalog.all()[i];
        if (std::abs (p.param->getValue() - s.normalised[i]) < 1.0e-7f) continue;
        p.param->beginChangeGesture();
        p.param->setValueNotifyingHost (s.normalised[i]);
        p.param->endChangeGesture();
    }
}

void applyRecipe (const Catalog& catalog, const Recipe& recipe)
{
    for (const auto& st : recipe.steps)
        if (const auto* info = catalog.find (st.key)) catalog.setValue (*info, st.value);   // later steps override earlier ones
}
} // namespace dm::ai
