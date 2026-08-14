#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "ParameterInspector.h"

class GestureRackAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              public juce::FileDragAndDropTarget
{
public:
    explicit GestureRackAudioProcessorEditor (GestureRackAudioProcessor&);
    ~GestureRackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (p != lastMousePos)
        {
            lastMousePos = p;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        lastMousePos = { -9999, -9999 };
        repaint();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        auto content = getLocalBounds().reduced (24);
        content.removeFromTop (112);
        content.removeFromBottom (58);
        auto panel = content.removeFromLeft (juce::jlimit (300, 420, content.getWidth() * 32 / 100));

        g.setColour (juce::Colour::fromRGB (247, 248, 250));
        g.fillRoundedRectangle (panel.toFloat(), 14.0f);
        g.setColour (juce::Colour::fromRGB (205, 210, 218));
        g.drawRoundedRectangle (panel.toFloat(), 14.0f, 1.0f);

        g.setColour (juce::Colour::fromRGB (45, 48, 56));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("GESTURES", panel.getX() + 16, panel.getY() + 12,
                    panel.getWidth() - 32, 20, juce::Justification::centredLeft);

        g.setColour (juce::Colour::fromRGB (130, 136, 148));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (TRANS("drag \xE2\x86\x92 target / param"),
                    panel.getX() + 16, panel.getY() + 32,
                    panel.getWidth() - 32, 16, juce::Justification::centredLeft);

        const auto live = processor.getLiveRightGesture();
        const std::array<gr::ControlGesture, 7> gestures {
            gr::ControlGesture::openPalm, gr::ControlGesture::closedFist,
            gr::ControlGesture::victory, gr::ControlGesture::thumbUp,
            gr::ControlGesture::thumbDown, gr::ControlGesture::pointRight,
            gr::ControlGesture::pointLeft
        };
        auto row = panel.reduced (16).withTrimmedTop (52).removeFromTop (44);
        const auto gap = 5;
        const auto width = juce::jmax (1, (row.getWidth() - gap * 6) / 7);
        for (int i = 0; i < static_cast<int> (gestures.size()); ++i)
        {
            const auto gesture = gestures[static_cast<size_t> (i)];
            auto chip = row.removeFromLeft (width);
            gestureRects[static_cast<size_t> (i)] = chip;
            const auto isLive = gesture == live;
            const auto isHover = chip.contains (lastMousePos) && ! gestureDragging;
            if (isLive)
                g.setColour (juce::Colour::fromRGB (245, 178, 60));
            else if (isHover)
                g.setColour (juce::Colour::fromRGB (235, 238, 242));
            else
                g.setColour (juce::Colour (255, 255, 255));
            g.fillRoundedRectangle (chip.toFloat(), 8.0f);
            g.setColour (isLive ? juce::Colour::fromRGB (235, 165, 40)
                                : juce::Colour::fromRGB (205, 210, 218));
            g.drawRoundedRectangle (chip.toFloat(), 8.0f, 1.0f);
            g.setColour (juce::Colour::fromRGB (60, 42, 12));
            g.setFont (juce::FontOptions (18.0f));
            g.drawText (gr::controlGestureToEmoji (gesture), chip,
                        juce::Justification::centred);
            row.removeFromLeft (gap);
        }

        auto targets = panel.reduced (16).withTrimmedTop (108).removeFromTop (42);
        activeTargetRect = targets.removeFromLeft ((targets.getWidth() - 8) / 2);
        targets.removeFromLeft (8);
        bypassTargetRect = targets;
        learnTargetRect = panel.reduced (16).withTrimmedTop (158).removeFromTop (42);

        const auto drawTarget = [&] (juce::Rectangle<int> rect, const juce::String& icon, const juce::String& text, juce::Colour accent)
        {
            const auto hot = (gestureDragging && rect.contains (gestureDragPoint))
                          || (! gestureDragging && rect.contains (lastMousePos));
            if (hot)
                g.setColour (accent.withAlpha (0.18f));
            else
                g.setColour (juce::Colour (255, 255, 255));
            g.fillRoundedRectangle (rect.toFloat(), 8.0f);
            g.setColour (hot ? accent : juce::Colour::fromRGB (205, 210, 218));
            g.drawRoundedRectangle (rect.toFloat(), 8.0f, 1.0f);
            g.setColour (hot ? accent : juce::Colour::fromRGB (120, 126, 138));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (icon + " " + text, rect, juce::Justification::centred);
        };

        const auto armed = processor.isParameterLearnArmed();
        drawTarget (activeTargetRect, "\xE2\x96\xB6", "ENABLE", juce::Colour::fromRGB (92, 180, 120));
        drawTarget (bypassTargetRect, "\xE2\x8F\xB8", "BYPASS", juce::Colour::fromRGB (215, 80, 80));
        drawTarget (learnTargetRect, "\xE2\x9A\x99", armed ? "LEARNING..." : "LEARN", juce::Colour::fromRGB (80, 140, 220));
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& path : files)
            if (path.endsWithIgnoreCase (".vst3"))
                return true;
        return false;
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        int targetSlot = processor.getSelectedSlot();
        for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
            if (slotButtons[static_cast<size_t> (i)].getBounds().contains (x, y))
            {
                targetSlot = i;
                break;
            }

        for (const auto& path : files)
            if (path.endsWithIgnoreCase (".vst3"))
            {
                processor.setSelectedSlot (targetSlot);
                processor.loadVst3FromFile (targetSlot, juce::File (path));
                updateSlotButtons();
                repaint();
                break;
            }
    }

private:
    void timerCallback() override;
    void choosePluginForSelectedSlot();
    void updateSlotButtons();
    void drawHand (juce::Graphics&,
                   juce::Rectangle<float>,
                   const gr::HandSnapshot&,
                   bool connected,
                   juce::Colour colour,
                   const juce::String& label);

    GestureRackAudioProcessor& processor;

    std::array<juce::TextButton, GestureRackAudioProcessor::slotCount> slotButtons;
    std::array<juce::Rectangle<int>, 7> gestureRects {};
    juce::Rectangle<int> activeTargetRect;
    juce::Rectangle<int> bypassTargetRect;
    juce::Rectangle<int> learnTargetRect;
    bool gestureDragging = false;
    gr::ControlGesture draggedGesture = gr::ControlGesture::unknown;
    juce::Point<int> gestureDragPoint;
    juce::Point<int> lastMousePos { -9999, -9999 };
    int hoveredSlot = -1;

    juce::TextButton loadButton { "LOAD" };
    juce::TextButton openButton { "OPEN" };
    juce::TextButton removeButton { "REMOVE" };
    juce::TextButton bypassButton { "ACTIVE" };
    juce::TextButton enableButton { "GESTURE ON" };
    juce::TextButton calibrateHandsButton { "CALIBRATE RIGHT" };
    juce::TextButton swapHandsButton { "SWAP L/R" };

    gr::ParameterInspector parameterInspector;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
