#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "ParameterInspector.h"
#include "VisionFrameReader.h"

class PluginBrowserComponent;

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
    class RackSlotButton final : public juce::TextButton
    {
    public:
        std::function<void (const juce::MouseEvent&)> dragDown;
        std::function<void (const juce::MouseEvent&)> dragMove;
        std::function<void (const juce::MouseEvent&)> dragUp;
        std::function<void (const juce::MouseEvent&)> hoverMove;
        std::function<void()> hoverExit;

        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDown (e);
            if (dragDown)
                dragDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDrag (e);
            if (dragMove)
                dragMove (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (dragUp)
                dragUp (e);
            juce::TextButton::mouseUp (e);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseMove (e);
            if (hoverMove)
                hoverMove (e);
        }

        void mouseExit (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseExit (e);
            if (hoverExit)
                hoverExit();
        }
    };

    void timerCallback() override;
    void showPluginBrowser();
    void hidePluginBrowser();
    void updateSlotButtons();
    void handleSlotMouseDown (int slotIndex, const juce::MouseEvent& e);
    void handleSlotMouseDrag (const juce::MouseEvent& e);
    void handleSlotMouseUp (const juce::MouseEvent& e);
    int findNearestSlot (juce::Point<int> editorPoint) const;
    void drawHandOverlay (juce::Graphics&,
                          juce::Rectangle<float> imageArea,
                          const gr::HandSnapshot&,
                          juce::Colour colour);
    juce::Rectangle<int> getGesturePaletteBounds() const;

    GestureRackAudioProcessor& processor;

    std::array<RackSlotButton, GestureRackAudioProcessor::slotCount> slotButtons;
    std::array<juce::Rectangle<int>, 7> gestureRects {};
    juce::Rectangle<int> activeTargetRect;
    juce::Rectangle<int> bypassTargetRect;
    bool gestureDragging = false;
    gr::ControlGesture draggedGesture = gr::ControlGesture::unknown;
    juce::Point<int> gestureDragPoint;
    juce::Point<int> lastMousePos { -9999, -9999 };
    int hoveredSlot = -1;

    int slotDragSource = -1;
    int slotDragTarget = -1;
    bool slotDragging = false;
    juce::Point<int> slotDragOrigin;

    juce::TextButton loadButton { "PLUGINS" };
    juce::TextButton openButton { "OPEN" };
    juce::TextButton removeButton { "REMOVE" };
    juce::TextButton bypassButton { "ACTIVE" };
    juce::TextButton enableButton { "GESTURE ON" };
    juce::TextButton calibrateHandsButton { "CALIBRATE" };
    juce::TextButton swapHandsButton { "SWAP L/R" };

    gr::ParameterInspector parameterInspector;
    gr::VisionFrameReader frameReader;
    gr::VisionCameraFrame cameraFrame;
    std::unique_ptr<PluginBrowserComponent> pluginBrowser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
