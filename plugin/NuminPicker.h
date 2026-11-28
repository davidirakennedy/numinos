// NuminPicker — choosing the Numin. The current Numin's name is the button (NuminTitle): large,
// with a chevron in a small well beside it. Clicking it drops a slab of obsidian under the name
// (NuminList) listing every Numin by category, each with its blurb; the window dims behind it.
// Click one, or use the arrow keys and Return, to choose it; Escape or a click outside closes.
//
// These replace a juce::ComboBox, whose pop-up menu can't show the blurbs or the light.
#pragma once

#include "NuminosLook.h"
#include "Registry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <vector>

// The current Numin's name, as a button that opens the list.
class NuminTitle : public juce::Button
{
  public:
    explicit NuminTitle(const NuminosLook& lookToUse) : juce::Button("Numin"), look(lookToUse)
    {
        setWantsKeyboardFocus(true);
    }

    void setNuminName(const juce::String& name)
    {
        setButtonText(name);
        setTitle(name + ", choose a Numin"); // for screen readers
    }

    void setOpen(bool shouldBeOpen)
    {
        open = shouldBeOpen;
        repaint();
    }

    // How wide the name and its chevron are, to size the button to fit.
    int idealWidth() const
    {
        return (int)std::ceil(juce::GlyphArrangement::getStringWidth(nameFont(), getButtonText())) + kGap + kWell + kGlowRoom;
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        using C = NuminosLook::Colours;
        auto area = getLocalBounds().toFloat();

        g.setColour(C::title);
        g.setFont(nameFont());
        area.removeFromRight((float)kGlowRoom); // room for the chevron's glow
        auto nameArea = area.removeFromLeft(area.getWidth() - kGap - kWell);
        g.drawText(getButtonText(), nameArea, juce::Justification::centredLeft, false);

        // the chevron, in its well; it glows when you're over it and turns honey while the list is open
        const auto well = juce::Rectangle<float>((float)kWell, (float)kWell).withCentre({area.getRight() - kWell * 0.5f, area.getCentreY() + 2.0f});
        if (highlighted || open)
        {
            g.setColour(C::honey.withAlpha(0.12f));
            g.fillEllipse(well.expanded(5.0f));
        }
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawEllipse(well.translated(0.0f, 1.0f).reduced(0.5f), 1.0f);
        g.setColour(C::obsidian);
        g.fillEllipse(well);

        juce::Path chevron;
        chevron.startNewSubPath(-5.5f, -2.5f);
        chevron.lineTo(0.0f, 2.5f);
        chevron.lineTo(5.5f, -2.5f);
        if (open)
            chevron.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi));
        chevron.applyTransform(juce::AffineTransform::translation(well.getCentre()));
        g.setColour(open ? C::honey : highlighted ? juce::Colour(0xffffcf8f) : juce::Colour(0xff8f9095));
        g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (hasKeyboardFocus(false))
        {
            g.setColour(C::honey);
            g.drawEllipse(well.expanded(3.0f), 1.5f);
        }
    }

  private:
    juce::Font nameFont() const { return look.caps(52.0f, 0.04f); }

    static constexpr int kWell = 30, kGap = 14, kGlowRoom = 8;
    const NuminosLook& look;
    bool open = false;
};

// The list that drops from the name. It covers the whole editor (that's the dimming) and draws
// its panel at `anchor`, the top-left corner just under the name.
class NuminList : public juce::Component
{
  public:
    explicit NuminList(const NuminosLook& lookToUse) : look(lookToUse)
    {
        setWantsKeyboardFocus(true);
        setAlwaysOnTop(true);
    }

    std::function<void(const juce::String& name)> onPick; // a Numin was chosen
    std::function<void()> onClose;                         // closed without choosing (or after)

    void show(const juce::String& currentName, juce::Point<int> anchor)
    {
        panelTopLeft = anchor;
        current = currentName;
        layOut();
        highlighted = indexOf(current);
        setVisible(true);
        toFront(true);
        grabKeyboardFocus();
        juce::Desktop::getInstance().getAnimator().fadeIn(this, 140);
    }

    void close()
    {
        if (!isVisible())
            return;
        setVisible(false);
        if (onClose)
            onClose();
    }

    void paint(juce::Graphics& g) override
    {
        using C = NuminosLook::Colours;
        g.fillAll(juce::Colour(0xff020203).withAlpha(0.55f)); // the window dims behind the list

        // the slab: a well's dark rim around a panel of slightly lit obsidian, casting a deep shadow
        const auto outer = panel.toFloat();
        juce::Path outerShape;
        outerShape.addRoundedRectangle(outer, 18.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.85f), 60, {0, 24}).drawForPath(g, outerShape);
        g.setColour(C::obsidian);
        g.fillPath(outerShape);
        g.setColour(C::honey.withAlpha(0.10f));
        g.strokePath(outerShape, juce::PathStrokeType(1.0f));
        const auto inner = outer.reduced(6.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff18181c), inner.getX() + inner.getWidth() * 0.2f, inner.getY(),
                                               juce::Colour(0xff070708), inner.getRight(), inner.getBottom(), true));
        g.fillRoundedRectangle(inner, 13.0f);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(inner.reduced(16.0f, 0.0f).withHeight(1.0f));

        for (auto& h : headings)
        {
            g.setColour(juce::Colour(0xff7d7e84));
            g.setFont(look.caps(11.0f, 0.2f));
            g.drawText(h.name, h.area.toFloat(), juce::Justification::bottomLeft);
        }

        for (int i = 0; i < (int)rows.size(); ++i)
        {
            const auto& row = rows[(size_t)i];
            const auto r = row.area.toFloat();
            const bool isCurrent = row.name == current;
            if (i == highlighted || isCurrent)
            {
                g.setGradientFill(juce::ColourGradient(C::honey.withAlpha(i == highlighted ? 0.13f : 0.10f), r.getX(), 0.0f,
                                                       C::honey.withAlpha(0.0f), r.getX() + r.getWidth() * 0.85f, 0.0f, false));
                g.fillRoundedRectangle(r, 10.0f);
            }
            if (isCurrent)
            {
                g.setColour(C::honey);
                g.fillPath(NuminosLook::dropShape(juce::Rectangle<float>(r.getX() + 12.0f, r.getY() + 12.0f, 10.0f, 13.0f)));
            }
            auto text = r.reduced(12.0f, 9.0f).withTrimmedLeft(kDropColumn);
            g.setColour(isCurrent ? juce::Colour(0xffffdca8) : C::title);
            g.setFont(nameFont());
            g.drawText(row.name, text.removeFromTop(kNameHeight), juce::Justification::centredLeft);
            row.blurb.draw(g, text.withHeight(row.blurb.getHeight()));
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { setHighlighted(rowAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHighlighted(-1); }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const int i = rowAt(e.getPosition());
        if (i >= 0)
            pick(i);
        else if (!panel.contains(e.getPosition()))
            close(); // a click outside the list
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
            close();
        else if (key == juce::KeyPress::downKey)
            setHighlighted(juce::jmin((int)rows.size() - 1, highlighted + 1));
        else if (key == juce::KeyPress::upKey)
            setHighlighted(juce::jmax(0, highlighted - 1));
        else if (key == juce::KeyPress::returnKey && highlighted >= 0)
            pick(highlighted);
        else
            return false;
        return true;
    }

    void focusLost(FocusChangeType) override
    {
        // clicking elsewhere in the plugin (or the host) closes the list
        if (!hasKeyboardFocus(true))
            close();
    }

  private:
    struct Row
    {
        juce::String name;
        juce::TextLayout blurb;
        juce::Rectangle<int> area;
    };
    struct Heading
    {
        juce::String name;
        juce::Rectangle<int> area;
    };

    // Every Numin, grouped by category (categories in alphabetical order, as the old menu had
    // them), then any still in the shop together under their own heading at the end.
    void layOut()
    {
        std::map<juce::String, std::vector<const numinos::NuminEntry*>> byCategory;
        std::vector<const numinos::NuminEntry*> inTheShop;
        for (auto& entry : numinos::allNumins())
            if (entry.status == numinos::Status::InTheShop)
                inTheShop.push_back(&entry);
            else
                byCategory[entry.category].push_back(&entry);
        std::vector<std::pair<juce::String, std::vector<const numinos::NuminEntry*>>> groups(byCategory.begin(), byCategory.end());
        if (!inTheShop.empty())
            groups.emplace_back("In the shop", inTheShop);

        rows.clear();
        headings.clear();
        const int x = panelTopLeft.x + 12, width = kPanelWidth - 24;
        int y = panelTopLeft.y + 16;
        for (auto& [category, entries] : groups)
        {
            headings.push_back({category, {x + 12, y, width - 24, kHeadingHeight}});
            y += kHeadingHeight + 2;
            for (auto* entry : entries)
            {
                Row row;
                row.name = entry->name;
                juce::AttributedString blurb;
                blurb.append(blurbOf(*entry), look.italic(14.0f), juce::Colour(0xff85868c));
                blurb.setLineSpacing(1.0f);
                row.blurb.createLayout(blurb, (float)(width - 24 - kDropColumn));
                const int height = 18 + kNameHeight + (int)std::ceil(row.blurb.getHeight());
                row.area = {x, y, width, height};
                y += height;
                rows.push_back(std::move(row));
            }
            y += 6;
        }
        panel = {panelTopLeft.x, panelTopLeft.y, kPanelWidth, y - panelTopLeft.y + 4};
    }

    // A Numin's blurb. The registry only knows names, so make one of each (once) and ask it.
    static juce::String blurbOf(const numinos::NuminEntry& entry)
    {
        static std::map<juce::String, juce::String> blurbs;
        auto& blurb = blurbs[entry.name];
        if (blurb.isEmpty())
            blurb = entry.create()->blurb();
        return blurb;
    }

    int indexOf(const juce::String& name) const
    {
        for (int i = 0; i < (int)rows.size(); ++i)
            if (rows[(size_t)i].name == name)
                return i;
        return -1;
    }

    int rowAt(juce::Point<int> p) const
    {
        for (int i = 0; i < (int)rows.size(); ++i)
            if (rows[(size_t)i].area.contains(p))
                return i;
        return -1;
    }

    void setHighlighted(int i)
    {
        if (i != highlighted)
        {
            highlighted = i;
            repaint(panel);
        }
    }

    void pick(int i)
    {
        const auto name = rows[(size_t)i].name;
        close();
        if (onPick)
            onPick(name);
    }

    juce::Font nameFont() const { return look.caps(21.0f, 0.04f); }

    static constexpr int kPanelWidth = 380, kHeadingHeight = 16, kNameHeight = 24, kDropColumn = 24;
    const NuminosLook& look;
    juce::Point<int> panelTopLeft;
    juce::Rectangle<int> panel;
    std::vector<Row> rows;
    std::vector<Heading> headings;
    juce::String current;
    int highlighted = -1;
};
