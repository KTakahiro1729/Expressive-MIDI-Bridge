#pragma once
#include <JuceHeader.h>
#include "ClipModel.h"
#include "UiStyle.h"

// Before/after view: grey outlines = original, filled = AI result (brightness = velocity),
// with a velocity lane at the bottom.
class PianoRollPreview final : public juce::Component
{
public:
    void setClips(const expressive::Clip* original, const expressive::Clip* rendered)
    {
        source = original;
        result = rendered;
        repaint();
    }
    void setEmptyText(const juce::String& t) { emptyText = t; repaint(); }
    void paint(juce::Graphics&) override;
private:
    const expressive::Clip* source = nullptr;
    const expressive::Clip* result = nullptr;
    juce::String emptyText;
};

// Drag source that hands the AI result (.mid) to the host. A plain click calls onClick.
class DragToDawHandle final : public juce::Component
{
public:
    std::function<juce::File()> prepareFile;
    std::function<void()> onClick;
    void setText(const juce::String& title, const juce::String& subtitle);
    void setActive(bool a)
    {
        active = a;
        setMouseCursor(a ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    juce::String title, subtitle;
    bool active = false, hover = false, dragging = false;
};
