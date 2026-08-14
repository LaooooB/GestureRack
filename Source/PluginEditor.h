#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "ParameterInspector.h"
#include "VisionFrameReader.h"

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

    void paintOverChildren (juce::Graphics& g) override;

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
    void drawHandOverlay (juce::Graphics&,
                          juce::Rectangle<float> imageArea,
                          const gr::HandSnapshot&,
                          juce::Colour colour);
    juce::Rectangle<int> getGesturePaletteBounds() const;

    GestureRackAudioProcessor& processor;

    std::array<juce::TextButton, GestureRackAudioProcessor::slotCount> slotButtons;
    std::array<juce::Rectangle<int>, 7> gestureRects {};
    juce::Rectangle<int> activeTargetRect;
    juce::Rectangle<int> bypassTargetRect;
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
    gr::VisionFrameReader frameReader;
    gr::VisionCameraFrame cameraFrame;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
