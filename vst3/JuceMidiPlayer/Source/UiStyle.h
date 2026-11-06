#pragma once
#include <JuceHeader.h>

// Shared colours, text helpers and small widgets for the editor.
// UTF-8 Japanese literals: MSVC is built with /utf-8 (see CMakeLists.txt).
namespace ui
{
namespace colours
{
const juce::Colour background { 0xff1e2127 };
const juce::Colour panel      { 0xff282c34 };
const juce::Colour panelEdge  { 0xff3a404b };
const juce::Colour text       { 0xffe6e6e6 };
const juce::Colour dim        { 0xff9aa3b0 };
const juce::Colour accent     { 0xff4fa3ff };
const juce::Colour ok         { 0xff4cd98a };
const juce::Colour warning    { 0xffffc05c };
const juce::Colour error      { 0xffff6b6b };
const juce::Colour lampOff    { 0xff444a55 };
const juce::Colour record     { 0xffff4f5e };
}

// Bilingual text: tr("English", "日本語").
struct Lang
{
    bool japanese = false;
    juce::String operator()(const char* en, const char* ja) const
    { return japanese ? juce::String::fromUTF8(ja) : juce::String(en); }
};

inline void styleLabel(juce::Label& label, float size, juce::Colour colour, bool bold = false)
{
    label.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setMinimumHorizontalScale(1.0f);
}

inline void styleToggle(juce::TextButton& b, int radioGroup = 0)
{
    b.setClickingTogglesState(true);
    if (radioGroup != 0) b.setRadioGroupId(radioGroup);
    b.setColour(juce::TextButton::buttonOnColourId, colours::accent.darker(0.35f));
    b.setColour(juce::TextButton::buttonColourId, colours::panel);
}

inline void stylePrimary(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, colours::accent.darker(0.25f));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
}

inline juce::String formatNumber(double v, int decimals)
{
    auto s = juce::String(v, decimals);
    if (s.containsChar('.')) s = s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return s;
}

inline void drawDashedBox(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float corner = 6.0f)
{
    juce::Path outline;
    outline.addRoundedRectangle(r, corner);
    juce::Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType(1.3f).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(c);
    g.fillPath(dashed);
}
} // namespace ui
