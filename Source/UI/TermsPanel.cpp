#include "TermsPanel.h"

#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
using namespace theme;

juce::String TermsPanel::getText()
{
    return dm::U (
        "CONDITIONS D'UTILISATION ET CONFIDENTIALITÉ — DAFT MUG\n"
        "PROJET DE TEXTE : à faire relire et valider (juridiquement) avant toute distribution. Les mentions « À COMPLÉTER » sont à renseigner.\n\n"
        "1. Éditeur du logiciel\n"
        "Oxy Gen Prod. (c) Philippe Marin - 2026\n\n"
        "2. Licence de Daft Mug : gratuit et non commercial\n"
        "Daft Mug est gratuit et destiné à un usage NON COMMERCIAL (personnel, pédagogique, associatif, loisir). Sans autorisation écrite d'Oxy Gen Prod., "
        "il est interdit de le vendre, le louer, le redistribuer (hors installateur officiel), le modifier ou l'utiliser dans un cadre commercial ou professionnel rémunéré. "
        "Le nom et le logo « Daft Mug » restent la propriété d'Oxy Gen Prod.\n\n"
        "3. Règles d'utilisation\n"
        "Daft Mug est un sampler : vous l'utilisez pour créer de la musique et du son. Ne l'utilisez pas pour porter atteinte aux droits d'autrui.\n\n"
        "4. Limites de responsabilité\n"
        "Un niveau sonore excessif peut endommager du matériel ou l'audition (réglez toujours votre volume d'écoute avec prudence).\n\n"
        "5. Samples et droits\n"
        "Vous êtes seul responsable des fichiers audio que vous importez : vous devez détenir les droits nécessaires (licence, autorisation) pour les utiliser, "
        "les modifier, les exporter et les distribuer. Daft Mug ne vérifie pas ces droits. Les fonctions de collecte et d'export copient vos fichiers sur votre ordinateur uniquement.\n\n"
        "6. Module IA facultatif (Daft Mug AI)\n"
        "• Il est désactivé par défaut et le sampler fonctionne entièrement sans lui (hors ligne, sans clé, ou si le service est indisponible).\n"
        "• Il fonctionne avec VOTRE propre clé API Gemini, que vous obtenez et gérez vous-même auprès de Google. Aucune clé n'est fournie avec Daft Mug.\n"
        "• L'IA est un conseiller : ses propositions ne sont jamais appliquées sans votre action. Vous les prévisualisez, les comparez, les acceptez ou les annulez. "
        "Elles peuvent être inexactes ou ne pas produire le son attendu.\n"
        "• Le moteur audio de Daft Mug reste local : l'IA ne traite jamais les notes MIDI ni le signal audio en temps réel.\n\n"
        "7. Données transmises à Google\n"
        "Lorsque vous envoyez une demande à l'IA, Daft Mug transmet au service Gemini de Google, via HTTPS : le texte de votre demande, le type de sample que vous avez choisi, "
        "le nom du pad, la liste des réglages disponibles avec leurs valeurs actuelles pour ce pad, et votre clé API (pour vous identifier auprès de Google). "
        "Daft Mug n'envoie PAS vos fichiers audio, les chemins de vos fichiers, votre projet complet ni vos autres pads. "
        "Le traitement de ces données par Google est régi par les conditions et la politique de confidentialité de Google applicables à l'API Gemini pour votre compte "
        "(notamment selon que vous utilisez une offre gratuite ou payante). Ne saisissez pas d'informations confidentielles dans vos demandes.\n\n"
        "8. Coûts éventuels\n"
        "L'utilisation de l'API Gemini peut être limitée par des quotas ou facturée par Google selon votre compte et le modèle choisi. Ces coûts sont à votre charge et relèvent de votre relation avec Google. "
        "Daft Mug ne perçoit rien et ne peut pas les limiter à votre place : surveillez votre consommation dans votre compte Google.\n\n"
        "9. Votre clé API\n"
        "Elle est conservée dans le coffre d'identifiants du système lorsqu'il est disponible (Windows : gestionnaire d'informations d'identification), jamais dans le code, "
        "les presets, les projets de votre station audio, les journaux ou les messages d'erreur. Aucun stockage n'est parfaitement sûr : un programme malveillant s'exécutant "
        "sous votre session pourrait en théorie y accéder. Vous pouvez effacer la clé à tout moment depuis la page « IA et confidentialité » et la révoquer chez Google.\n");
}

TermsPanel::TermsPanel()
{
    setOpaque (true);
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };
    body.setMultiLine (true, true);
    body.setReadOnly (true);
    body.setCaretVisible (false);
    body.setScrollbarsShown (true);
    body.setFont (makeFont (14.0f));
    body.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
    body.setColour (juce::TextEditor::textColourId, text);
    body.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.12f));
    body.setText (getText(), false);
    addAndMakeVisible (body);
}

void TermsPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (text);
    g.setFont (makeFont (22.0f, true));
    g.drawText (dm::U ("Conditions d'utilisation"), 170, 12, 500, 44, juce::Justification::centredLeft);
}

void TermsPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 12);
    backButton.setBounds (area.removeFromTop (44).removeFromLeft (120).reduced (0, 4));
    area.removeFromTop (8);
    body.setBounds (area.withTrimmedBottom (8));
}
} // namespace dm::ui
