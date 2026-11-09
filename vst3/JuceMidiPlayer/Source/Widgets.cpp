#include "Widgets.h"
#include <cmath>

using namespace ui;

// ============================================================== PianoRollPreview

void PianoRollPreview::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(colours::background);
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(colours::panelEdge);
    g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

    if (source == nullptr || source->empty())
    {
        g.setColour(colours::dim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(emptyText, getLocalBounds().reduced(12), juce::Justification::centred, 3);
        return;
    }

    auto roll = area.reduced(6.0f);
    auto lane = roll.removeFromBottom(26.0f);
    roll.removeFromBottom(4.0f);

    int lo = 127, hi = 0;
    for (const auto& n : source->notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
    lo = std::max(0, lo - 2);
    hi = std::min(127, hi + 2);
    const double barLen = source->beatsPerBar > 0 ? source->beatsPerBar : 4.0;
    const double t0 = std::floor(source->firstBeat() / barLen) * barLen;
    double t1 = std::max(source->lastBeat(), result != nullptr ? result->lastBeat() : 0.0);
    if (t1 <= t0) t1 = t0 + 4.0;
    const auto x = [&](double beat)
    { return roll.getX() + static_cast<float>((beat - t0) / (t1 - t0)) * roll.getWidth(); };
    const float rowH = roll.getHeight() / static_cast<float>(hi - lo + 1);
    const auto y = [&](int pitch) { return roll.getBottom() - static_cast<float>(pitch - lo + 1) * rowH; };

    // bar lines
    const double bar = source->beatsPerBar > 0 ? source->beatsPerBar : 4.0;
    const double barsVisible = (t1 - t0) / bar;
    const int labelEvery = barsVisible > 32 ? 8 : barsVisible > 12 ? 4 : 1;
    int barIndex = 0;
    g.setFont(juce::FontOptions(10.0f));
    for (double b = t0; b <= t1 + 1e-9; b += bar, ++barIndex)
    {
        const float bx = x(b);
        g.setColour(colours::panelEdge.withAlpha(0.7f));
        g.drawVerticalLine(juce::roundToInt(bx), roll.getY(), lane.getBottom());
        if (barIndex % labelEvery == 0)
        {
            g.setColour(colours::dim);
            g.drawText(juce::String(juce::roundToInt(b / bar) + 1), juce::Rectangle<float>(bx + 2, roll.getY(), 30, 12),
                       juce::Justification::topLeft);
        }
    }

    const float minW = 1.5f;
    for (const auto& n : source->notes)
    {
        const auto r = juce::Rectangle<float>(x(n.start), y(n.pitch), std::max(minW, x(n.end) - x(n.start)),
                                              std::max(1.5f, rowH - 0.5f));
        g.setColour(colours::dim.withAlpha(0.55f));
        g.drawRect(r, 1.0f);
    }
    if (result != nullptr)
        for (const auto& n : result->notes)
        {
            const auto r = juce::Rectangle<float>(x(n.start), y(n.pitch), std::max(minW, x(n.end) - x(n.start)),
                                                  std::max(1.5f, rowH - 0.5f));
            g.setColour(colours::accent.withAlpha(0.35f + 0.65f * static_cast<float>(n.velocity) / 127.0f));
            g.fillRect(r);
        }

    // velocity lane: grey = original, blue = result
    g.setColour(colours::panel);
    g.fillRect(lane);
    for (const auto& n : source->notes)
    {
        g.setColour(colours::dim.withAlpha(0.6f));
        const float h = lane.getHeight() * static_cast<float>(n.velocity) / 127.0f;
        g.fillRect(x(n.start) - 0.5f, lane.getBottom() - h, 1.0f, 1.5f);
    }
    if (result != nullptr)
        for (const auto& n : result->notes)
        {
            const float h = lane.getHeight() * static_cast<float>(n.velocity) / 127.0f;
            g.setColour(colours::accent);
            g.fillRect(x(n.start) - 0.5f, lane.getBottom() - h, 1.2f, h);
        }
}

// ============================================================== DragToDawHandle

void DragToDawHandle::setText(const juce::String& t, const juce::String& s)
{
    title = t;
    subtitle = s;
    repaint();
}

void DragToDawHandle::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const auto c = !active ? colours::panelEdge : (hover || dragging) ? colours::ok : colours::accent;
    g.setColour(active ? c.withAlpha(hover ? 0.22f : 0.14f) : colours::panel);
    g.fillRoundedRectangle(r, 7.0f);
    drawDashedBox(g, r, c, 7.0f);

    // grip icon: three horizontal bars
    auto icon = r.removeFromLeft(54.0f).withSizeKeepingCentre(22.0f, 18.0f);
    g.setColour(active ? c : colours::dim);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle(icon.getX(), icon.getY() + i * 7.0f, icon.getWidth(), 3.5f, 1.5f);

    g.setColour(active ? colours::text : colours::dim);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(title, r.removeFromTop(r.getHeight() * 0.55f).withTrimmedTop(4), juce::Justification::bottomLeft);
    g.setColour(colours::dim);
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(subtitle, r, juce::Justification::topLeft);
}

void DragToDawHandle::mouseUp(const juce::MouseEvent& e)
{
    const bool wasDrag = dragging || e.getDistanceFromDragStart() >= 6;
    dragging = false;
    repaint();
    if (active && !wasDrag && onClick) onClick();
}

void DragToDawHandle::mouseDrag(const juce::MouseEvent& e)
{
    if (!active || dragging || e.getDistanceFromDragStart() < 6 || !prepareFile) return;
    const auto file = prepareFile();
    if (!file.existsAsFile()) return;
    dragging = true;
    repaint();
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this,
                                                               [this] { dragging = false; repaint(); });
}

