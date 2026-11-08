#include "PadControls.h"

#include <array>

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
PadControls::PadControls (DaftMugProcessor& p, int padIndex, bool showAdvanced)
    : processor (p), index (padIndex), advanced (showAdvanced)
{
    if (! advanced)
    {
        hwLook = std::make_unique<hw::HardwareLookAndFeel>();
        setLookAndFeel (hwLook.get());
    }
    addRow (gain, "Volume", ids::gain (index));
    addRow (pitch, "Pitch", ids::pitch (index));
    addRow (pan, "Pan", ids::pan (index));
    addRow (velocity, dm::U ("Vélocité"), ids::vel (index));
    if (advanced)
    {
        addRow (velCurve, dm::U ("Courbe vélocité"), ids::velCurve (index));
        velCurve.label.setTooltip (dm::U ("Linéaire = proportionnelle. « Dure » demande plus de force pour jouer fort, « Douce » rend les notes faibles plus audibles."));
    }
    addRow (note, "Note MIDI", ids::note (index));
    if (advanced)
    {
        addRow (loopStart, dm::U ("Boucle : départ"), ids::loopStart (index));
        loopStart.label.setTooltip (dm::U ("Mode Loop : point de bouclage, en % de la région. La lecture part du début puis boucle à partir d'ici."));
        addRow (loopFade, dm::U ("Boucle : fondu"), ids::loopFade (index));
        loopFade.label.setTooltip (dm::U ("Mode Loop : durée du fondu enchaîné entre la fin de la boucle et son point de départ (supprime le clic de raccord)."));
        addRow (choke, dm::U ("Groupe de coupure"), ids::choke (index));
        choke.label.setTooltip (dm::U ("Les pads d'un même groupe se coupent entre eux (charleston ouvert coupé par le fermé). Aucun = pas de coupure."));
        chromaticButton.setTooltip (dm::U ("Ce pad joue sur toutes les notes MIDI libres, transposé par rapport à sa propre note (la note du pad = hauteur d'origine). "
                                           "Les notes de vos autres pads chargés restent à eux. Gate ou Loop conseillés pour jouer des mélodies."));
        addAndMakeVisible (chromaticButton);
        chromaticAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::chromatic (index), chromaticButton);
    }

    modeLabel.setText ("Mode", juce::dontSendNotification);
    modeLabel.setColour (juce::Label::textColourId, theme::textDim);
    modeLabel.setFont (makeFont (13.0f));
    addAndMakeVisible (modeLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (ids::mode (index))))
        modeBox.addItemList (choice->choices, 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, ids::mode (index), modeBox);

    learnButton.setClickingTogglesState (true);
    learnButton.setTooltip (dm::U ("Cliquez puis jouez une note MIDI pour l'assigner à ce pad"));
    learnButton.onClick = [this]
    {
        if (learnButton.getToggleState()) processor.startMidiLearn (index);
        else processor.cancelMidiLearn();
    };
    addAndMakeVisible (learnButton);

    if (! advanced)
    {
        styleAsKnob (gain, juce::Colour (0xffd9483b));
        styleAsKnob (pitch, juce::Colour (0xff3b82d9));
        styleAsKnob (pan, juce::Colour (0xff3fb06a));
        styleAsKnob (velocity, juce::Colour (0xffe0b13a));
        styleAsKnob (note, juce::Colour (0xffb9bcc4));
        modeLabel.setText ("MODE", juce::dontSendNotification);
        modeLabel.setJustificationType (juce::Justification::centred);
        modeLabel.setFont (makeFont (10.5f, true));
        modeLabel.setColour (juce::Label::textColourId, hw::engraving.withAlpha (0.85f));
    }

    startTimerHz (10);
}

void PadControls::styleAsKnob (Row& r, juce::Colour cap)
{
    r.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    r.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    r.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    r.slider.setTextBoxIsEditable (true);
    r.slider.setMouseDragSensitivity (160);
    r.slider.setColour (juce::Slider::rotarySliderFillColourId, cap);
    // upper case by hand: toUpperCase() leaves accented letters alone
    r.label.setText (r.label.getText().startsWith (dm::U ("Vélo")) ? juce::String (dm::U ("VÉLOCITÉ")) : r.label.getText().toUpperCase(),
                     juce::dontSendNotification);
    r.label.setJustificationType (juce::Justification::centred);
    r.label.setFont (makeFont (10.5f, true));
    r.label.setColour (juce::Label::textColourId, hw::engraving.withAlpha (0.85f));
}

void PadControls::addRow (Row& row, const juce::String& title, const juce::String& paramId)
{
    row.label.setText (title, juce::dontSendNotification);
    row.label.setColour (juce::Label::textColourId, theme::textDim);
    row.label.setFont (makeFont (13.0f));
    addAndMakeVisible (row.label);

    row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 92, 24);
    row.slider.setTextBoxIsEditable (true);   // keyboard numeric entry
    addAndMakeVisible (row.slider);
    row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, row.slider);
}

void PadControls::resized()
{
    if (! advanced)
    {
        // knob panel: 3 columns x 2 rows (volume, pitch, pan / velocity, MIDI note, mode + learn)
        auto area = getLocalBounds().reduced (0, 2);
        auto cells = [&] (juce::Rectangle<int> row) { const int w = row.getWidth() / 3; return std::array<juce::Rectangle<int>, 3> { row.removeFromLeft (w), row.removeFromLeft (w), row }; };
        auto place = [] (Row& r, juce::Rectangle<int> cell)
        {
            r.label.setBounds (cell.removeFromTop (16));
            r.slider.setBounds (cell.reduced (2, 0));
        };
        const auto top = cells (area.removeFromTop (knobRowHeight));
        const auto bottom = cells (area.removeFromTop (knobRowHeight));
        place (gain, top[0]); place (pitch, top[1]); place (pan, top[2]);
        place (velocity, bottom[0]); place (note, bottom[1]);
        auto modeCell = bottom[2].reduced (4, 0);
        modeLabel.setBounds (modeCell.removeFromTop (16));
        modeBox.setBounds (modeCell.removeFromTop (28));
        modeCell.removeFromTop (8);
        learnButton.setBounds (modeCell.removeFromTop (30));
        return;
    }

    auto area = getLocalBounds().reduced (0, 4);
    auto layoutRow = [&] (Row& r)
    {
        auto line = area.removeFromTop (rowHeight);
        r.label.setBounds (line.removeFromLeft (112));
        r.slider.setBounds (line.reduced (0, 4));
    };
    layoutRow (gain);
    layoutRow (pitch);
    layoutRow (pan);
    layoutRow (velocity);
    if (advanced) layoutRow (velCurve);
    layoutRow (note);
    if (advanced)
    {
        layoutRow (loopStart);
        layoutRow (loopFade);
        layoutRow (choke);
        chromaticButton.setBounds (area.removeFromTop (rowHeight).reduced (0, 3).withTrimmedLeft (112));
    }

    auto line = area.removeFromTop (rowHeight);
    modeLabel.setBounds (line.removeFromLeft (112));
    learnButton.setBounds (line.removeFromRight (110).reduced (0, 3));
    modeBox.setBounds (line.reduced (0, 3).withTrimmedRight (8));
}

void PadControls::timerCallback()
{
    const bool armed = processor.getMidiLearnPad() == index;
    if (learnButton.getToggleState() != armed)
        learnButton.setToggleState (armed, juce::dontSendNotification);
}
} // namespace dm::ui
