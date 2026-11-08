#include "AiService.h"

namespace dm::ai
{
// ------------------------------------------------------------------------------------------------ transport
Transport makeDefaultTransport()
{
    return [] (const HttpRequest& req) -> HttpResponse
    {
        HttpResponse out;
        // the key may only ever travel to Google's API host over HTTPS
        if (! req.url.startsWith (AiService::kHost)) { out.error = "blocked"; return out; }

        juce::URL url (req.url);
        const bool post = req.method == "POST";
        if (post) url = url.withPOSTData (req.body);

        juce::String headers = "x-goog-api-key: " + req.apiKey + "\r\n";
        if (post) headers << "Content-Type: application/json\r\n";

        int status = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (post ? juce::URL::ParameterHandling::inPostData : juce::URL::ParameterHandling::inAddress)
                                                 .withConnectionTimeoutMs (req.timeoutMs)
                                                 .withExtraHeaders (headers)
                                                 .withHttpRequestCmd (req.method)
                                                 .withStatusCode (&status)
                                                 .withNumRedirectsToFollow (0));
        if (stream == nullptr) { out.error = "connection"; return out; }
        out.status = status;
        out.body = stream->readEntireStreamAsString().substring (0, 2000000);
        return out;
    };
}

// ------------------------------------------------------------------------------------------------ helpers
juce::File AiService::defaultSettingsFile()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("DAFTMUG_SETTINGS_DIR", {});
    const auto dir = env.isNotEmpty() ? juce::File (env)
                                      : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DaftMug");
    return dir.getChildFile ("settings.xml");
}

bool AiService::isValidModelName (const juce::String& m)
{
    if (m.isEmpty() || m.length() > 64) return false;
    for (auto c : m)
        if (! (juce::CharacterFunctions::isLetterOrDigit (c) && c < 128) && c != '.' && c != '-' && c != '_') return false;
    return true;
}

juce::String AiService::redact (const juce::String& text, const juce::String& key)
{
    auto s = text;
    if (key.length() >= 8) s = s.replace (key, "***");
    // anything that looks like a Google API key is hidden too (e.g. a key echoed by a proxy or typed in the request)
    juce::String out;
    int i = 0;
    while (i < s.length())
    {
        if (s.substring (i).startsWith ("AIza"))
        {
            int j = i + 4;
            while (j < s.length() && (juce::CharacterFunctions::isLetterOrDigit (s[j]) || s[j] == '_' || s[j] == '-')) ++j;
            if (j - i >= 20) { out << "***"; i = j; continue; }
        }
        out << juce::String::charToString (s[i]);
        ++i;
    }
    return out;
}

juce::String AiService::httpErrorText (const HttpResponse& r, const juce::String& key)
{
    if (r.status == 0)
        return dm::U ("Pas de réponse du service Gemini (hors ligne, pare-feu ou délai dépassé). Le sampler fonctionne normalement sans l'IA.");

    juce::String apiMessage;
    const auto v = juce::JSON::parse (r.body);
    if (auto* o = v.getDynamicObject())
        if (auto* e = o->getProperty ("error").getDynamicObject())
            apiMessage = redact (e->getProperty ("message").toString(), key).replaceCharacters ("\r\n", "  ").substring (0, 200);

    juce::String base;
    switch (r.status)
    {
        case 400: base = dm::U ("Requête refusée (clé API invalide ou requête non acceptée)"); break;
        case 401:
        case 403: base = dm::U ("Accès refusé : vérifiez votre clé API et que l'API Gemini est activée pour elle"); break;
        case 404: base = dm::U ("Modèle introuvable : choisissez un autre modèle dans les options"); break;
        case 429: base = dm::U ("Limite de requêtes ou de quota atteinte : réessayez plus tard"); break;
        default:  base = r.status >= 500 ? dm::U ("Le service Gemini est temporairement indisponible") : dm::U ("Erreur du service Gemini"); break;
    }
    return base + " (HTTP " + juce::String (r.status) + ")" + (apiMessage.isNotEmpty() ? " : " + apiMessage : juce::String());
}

// ------------------------------------------------------------------------------------------------ prompts
juce::String AiService::buildSystemPrompt (Mode mode, const Catalog& catalog)
{
    juce::String s;
    s << "You are the sound-design advisor inside \"Daft Mug\", a 16-pad sampler plug-in. You only ADVISE: the plug-in's local audio engine "
         "executes, and the user always reviews and accepts your proposal. Answer in the user's language (French by default).\n\n";

    if (mode == Mode::Explain)
    {
        s << "Task: explain audio concepts simply and accurately (pitch, EQ, frequency and Q, attack vs fade-in, reverb, wet vs dry, how to make a sound darker, "
             "brighter, more aggressive or more spacious). Be pedagogic for beginners but give precise figures for experienced users. "
             "Relate explanations to the real controls of Daft Mug listed below. Reply as JSON with a single field \"answer\".\n\n";
    }
    else
    {
        s << "Task: " << (mode == Mode::Settings ? "propose a coherent set of parameter values that achieves the user's request for the current pad."
                                                  : "propose a sound-design RECIPE: an ordered list of named steps that transform the current sound as requested.")
          << " Each step sets exactly one parameter to one value, with a concise explanation and the expected audible result. "
             "Change only what is useful; use few steps (usually 3 to 14). Reply as JSON following the response schema.\n\n"
             "Hard rules:\n"
             "- Use ONLY the parameter keys listed below and stay inside their ranges. Never invent a parameter, effect or feature.\n"
             "- Never output code, file paths, commands or URLs.\n"
             "- If something requested cannot be done with these controls (for example a compressor, a filter other than the 4 EQ bands, "
             "time-stretching, synthesis, granular processing), say so plainly in \"unsupported\" and propose the closest feasible alternative "
             "using the listed parameters. Do not pretend the plug-in can do it.\n"
             "- You cannot hear the sample; reason from the user's description and the current values.\n\n";
    }
    s << "Daft Mug controls available for the current pad (key: meaning. range. current value):\n" << catalog.describeForPrompt();
    s << "\nNot available in Daft Mug: compressor/limiter, filters other than the 4 EQ bands, time-stretch, pitch-independent speed, "
         "synthesis, granular, sidechain, stereo widening, sample editing beyond fades/reverse/region.\n";
    return s;
}

juce::String AiService::buildUserPrompt (const Question& q, const Catalog&)
{
    juce::String s;
    if (q.sampleType.isNotEmpty()) s << "Sample type (chosen by the user): " << q.sampleType.substring (0, 60) << "\n";
    if (q.padName.isNotEmpty()) s << "Pad name: " << q.padName.substring (0, 60) << "\n";
    s << "Request: " << q.request.substring (0, 1500) << "\n";
    return s;
}

// ------------------------------------------------------------------------------------------------ service
AiService::AiService (std::unique_ptr<SecretStore> s, Transport t, juce::File file)
    : store (std::move (s)), transport (std::move (t)), settingsFile (file == juce::File() ? defaultSettingsFile() : file)
{
    if (auto xml = juce::parseXML (settingsFile))
        if (xml->hasTagName ("DAFTMUG_SETTINGS"))
        {
            enabled = xml->getBoolAttribute ("aiEnabled", false);
            const auto m = xml->getStringAttribute ("aiModel");
            if (isValidModelName (m)) model = m;
        }
}

AiService::~AiService()
{
    pool.removeAllJobs (true, 25000);
}

void AiService::saveSettings() const
{
    juce::XmlElement xml ("DAFTMUG_SETTINGS");   // never a secret here: only these two preferences
    xml.setAttribute ("aiEnabled", enabled);
    xml.setAttribute ("aiModel", model);
    if (settingsFile.getParentDirectory().createDirectory()) xml.writeTo (settingsFile);
}

void AiService::setEnabled (bool on) { enabled = on; saveSettings(); }

void AiService::setModel (const juce::String& m)
{
    if (! isValidModelName (m)) return;
    model = m;
    saveSettings();
}

bool AiService::hasKey() { return getKey().isNotEmpty(); }

juce::String AiService::getKey()
{
    if (! keyLoaded) { cachedKey = store->load(); keyLoaded = true; }
    return cachedKey;
}

juce::Result AiService::setKey (const juce::String& raw)
{
    const auto key = raw.trim();
    if (key.length() < 10 || key.length() > 256) return juce::Result::fail (dm::U ("La clé API n'a pas une longueur plausible."));
    for (auto c : key)
        if (c <= 32 || c >= 127) return juce::Result::fail (dm::U ("La clé API ne doit contenir ni espace ni caractère spécial."));
    if (auto r = store->save (key); r.failed()) return r;
    cachedKey = key;
    keyLoaded = true;
    return juce::Result::ok();
}

juce::Result AiService::clearKey()
{
    cachedKey = {};
    keyLoaded = true;
    models.clear();
    return store->clear();
}

void AiService::run (HttpRequest req, std::function<void (HttpResponse)> done)
{
    busy = true;
    juce::WeakReference<AiService> weak (this);
    auto transportCopy = transport;
    pool.addJob ([weak, transportCopy, req, done]
    {
        HttpResponse resp;
        try { resp = transportCopy (req); }
        catch (...) { resp = {}; }
        juce::MessageManager::callAsync ([weak, done, resp]
        {
            if (auto* self = weak.get()) { self->busy = false; done (resp); }
        });
    });
}

void AiService::testConnection (std::function<void (juce::Result, juce::StringArray)> done)
{
    if (! enabled) { done (juce::Result::fail (dm::U ("L'IA est désactivée.")), {}); return; }
    const auto key = getKey();
    if (key.isEmpty()) { done (juce::Result::fail (dm::U ("Aucune clé API enregistrée.")), {}); return; }
    if (busy) { done (juce::Result::fail (dm::U ("Une requête est déjà en cours.")), {}); return; }

    HttpRequest req;
    req.url = juce::String (kHost) + "v1beta/models?pageSize=200";
    req.apiKey = key;
    req.timeoutMs = 10000;
    juce::WeakReference<AiService> weak (this);
    run (req, [weak, key, done] (HttpResponse r)
    {
        auto* self = weak.get();
        if (self == nullptr) return;
        if (r.status != 200) { done (juce::Result::fail (httpErrorText (r, key)), {}); return; }

        juce::StringArray found;
        const auto parsed = juce::JSON::parse (r.body);   // keep the var alive: getDynamicObject() points into it
        if (auto* o = parsed.getDynamicObject())
        {
            const auto list = o->getProperty ("models");
            for (int i = 0; i < list.size(); ++i)
            {
                auto* m = list[i].getDynamicObject();
                if (m == nullptr) continue;
                bool canGenerate = false;
                const auto methods = m->getProperty ("supportedGenerationMethods");
                for (int k = 0; k < methods.size(); ++k) if (methods[k].toString() == "generateContent") canGenerate = true;
                auto name = m->getProperty ("name").toString();
                if (name.startsWith ("models/")) name = name.substring (7);
                if (! canGenerate || ! name.startsWithIgnoreCase ("gemini") || ! isValidModelName (name)) continue;
                bool excluded = false;
                for (const char* bad : { "embedding", "image", "tts", "audio", "live", "robotics", "computer-use", "vision" })
                    if (name.containsIgnoreCase (bad)) excluded = true;
                if (! excluded) found.addIfNotAlreadyThere (name);
            }
        }
        else
        {
            done (juce::Result::fail (dm::U ("Réponse inattendue du service Gemini.")), {});
            return;
        }
        found.sort (true);
        self->models = found;
        if (found.isEmpty()) { done (juce::Result::fail (dm::U ("La clé fonctionne, mais aucun modèle de génération de texte n'est disponible pour elle.")), {}); return; }
        if (self->model.isEmpty() || ! found.contains (self->model))
        {
            // prefer a "flash" model (fast and inexpensive), otherwise the first one
            juce::String pick = found[0];
            for (const auto& m : found) if (m.containsIgnoreCase ("flash") && ! m.containsIgnoreCase ("lite") && ! m.containsIgnoreCase ("preview")) { pick = m; break; }
            if (self->model.isEmpty()) self->setModel (pick);
        }
        done (juce::Result::ok(), found);
    });
}

void AiService::ask (const Question& q, const Catalog& catalog, std::function<void (juce::Result, juce::String)> done)
{
    if (! enabled) { done (juce::Result::fail (dm::U ("L'IA est désactivée dans les options.")), {}); return; }
    const auto key = getKey();
    if (key.isEmpty()) { done (juce::Result::fail (dm::U ("Aucune clé API : saisissez la vôtre dans Options › IA et confidentialité.")), {}); return; }
    if (! isValidModelName (model)) { done (juce::Result::fail (dm::U ("Aucun modèle choisi : utilisez « Authentifier la clé API » dans les options.")), {}); return; }
    if (busy) { done (juce::Result::fail (dm::U ("Une requête est déjà en cours.")), {}); return; }
    if (q.request.trim().isEmpty()) { done (juce::Result::fail (dm::U ("Écrivez d'abord votre demande.")), {}); return; }

    auto* root = new juce::DynamicObject();
    auto part = [] (const juce::String& t) { auto* p = new juce::DynamicObject(); p->setProperty ("text", t); return juce::var (p); };
    auto content = [&] (const juce::String& t, const char* role)
    {
        auto* c = new juce::DynamicObject();
        if (role != nullptr) c->setProperty ("role", role);
        c->setProperty ("parts", juce::Array<juce::var> { part (t) });
        return juce::var (c);
    };
    root->setProperty ("systemInstruction", content (buildSystemPrompt (q.mode, catalog), nullptr));
    root->setProperty ("contents", juce::Array<juce::var> { content (buildUserPrompt (q, catalog), "user") });
    auto* cfg = new juce::DynamicObject();
    cfg->setProperty ("responseMimeType", "application/json");
    cfg->setProperty ("responseSchema", q.mode == Mode::Explain ? explanationSchema() : recipeSchema (catalog));
    cfg->setProperty ("temperature", 0.4);
    cfg->setProperty ("maxOutputTokens", 4096);
    root->setProperty ("generationConfig", juce::var (cfg));

    HttpRequest req;
    req.method = "POST";
    req.url = juce::String (kHost) + "v1beta/models/" + model + ":generateContent";
    req.body = juce::JSON::toString (juce::var (root), true);
    req.apiKey = key;
    req.timeoutMs = 45000;

    run (req, [key, done] (HttpResponse r)
    {
        if (r.status != 200) { done (juce::Result::fail (httpErrorText (r, key)), {}); return; }
        const auto parsed = juce::JSON::parse (r.body);   // keep the var alive: getDynamicObject() points into it
        auto* o = parsed.getDynamicObject();
        if (o == nullptr) { done (juce::Result::fail (dm::U ("Réponse inattendue du service Gemini.")), {}); return; }

        const auto candidates = o->getProperty ("candidates");
        if (! candidates.isArray() || candidates.size() == 0)
        {
            juce::String why;
            if (auto* fb = o->getProperty ("promptFeedback").getDynamicObject()) why = fb->getProperty ("blockReason").toString();
            done (juce::Result::fail (dm::U ("Aucune réponse exploitable") + (why.isNotEmpty() ? " (" + redact (why, key).substring (0, 60) + ")" : juce::String())), {});
            return;
        }
        auto* cand = candidates[0].getDynamicObject();
        const auto finish = cand != nullptr ? cand->getProperty ("finishReason").toString() : juce::String();
        juce::String text;
        if (cand != nullptr)
            if (auto* c = cand->getProperty ("content").getDynamicObject())
            {
                const auto parts = c->getProperty ("parts");
                for (int i = 0; i < parts.size(); ++i)
                    if (auto* p = parts[i].getDynamicObject())
                        if (! (bool) p->getProperty ("thought")) text << p->getProperty ("text").toString();
            }
        if (text.isEmpty())
        {
            done (juce::Result::fail (dm::U ("Réponse vide de l'IA") + (finish.isNotEmpty() && finish != "STOP" ? " (" + redact (finish, key).substring (0, 40) + ")" : juce::String())), {});
            return;
        }
        if (finish == "MAX_TOKENS") { done (juce::Result::fail (dm::U ("Réponse tronquée par l'IA : reformulez avec une demande plus simple.")), {}); return; }
        done (juce::Result::ok(), text);
    });
}
} // namespace dm::ai
