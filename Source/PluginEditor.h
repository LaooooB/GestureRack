#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "ParameterInspector.h"
#include "VisionFrameReader.h"
#include "UiTheme.h"

class PluginBrowserComponent;

class GestureRackAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              public juce::FileDragAndDropTarget
{
public:
    explicit GestureRackAudioProcessorEditor (GestureRackAudioProcessor&);
    ~GestureRackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& path : files)
            if (path.endsWithIgnoreCase (".vst3")) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    class EmbeddedEditorCanvas;
    class GesturePanel;

    class RackSlotButton final : public gr::ui::AnimatedTextButton
    {
    public:
        std::function<void (const juce::MouseEvent&)> dragDown, dragMove, dragUp;

        void setSlotVisualState (int slotNumberToUse, bool loadedToUse, bool bypassedToUse,
                                 int mappingCountToUse, juce::String pluginNameToUse);
        void paintButton (juce::Graphics&, bool, bool) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            gr::ui::AnimatedTextButton::mouseDown (e);
            if (dragDown) dragDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            gr::ui::AnimatedTextButton::mouseDrag (e);
            if (dragMove) dragMove (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (dragUp) dragUp (e);
            gr::ui::AnimatedTextButton::mouseUp (e);
        }

    private:
        int slotNumber = 1;
        int mappingCount = 0;
        bool loaded = false;
        bool bypassed = false;
        juce::String pluginName;
    };

    void timerCallback() override;
    void showPluginBrowser();
    void hidePluginBrowser();
    void updateSlotButtons();
    void updateEmbeddedEditor();
    void updateViewportScrollbars();
    void adaptEditorToNativeSize (bool force = false);
    void handleSlotMouseDown (int slotIndex, const juce::MouseEvent& e);
    void handleSlotMouseDrag (const juce::MouseEvent& e);
    void handleSlotMouseUp (const juce::MouseEvent& e);
    int findNearestSlot (juce::Point<int> editorPoint) const;
    void drawHandOverlay (juce::Graphics&, juce::Rectangle<float> imageArea,
                          const gr::HandSnapshot&, juce::Colour colour);
    void removeSelectedPlugin();

    GestureRackAudioProcessor& processor;
    std::array<RackSlotButton, GestureRackAudioProcessor::slotCount> slotButtons;

    int slotDragSource = -1;
    int slotDragTarget = -1;
    bool slotDragging = false;
    juce::Point<int> slotDragOrigin;

    gr::ui::AnimatedTextButton loadButton { "PLUGINS" };
    gr::ui::AnimatedTextButton removeButton { "REMOVE" };
    gr::ui::AnimatedTextButton bypassButton { "ACTIVE" };
    gr::ui::AnimatedTextButton calibrateHandsButton { "CAL RIGHT" };
    gr::ui::AnimatedTextButton swapHandsButton { "SWAP L/R" };

    gr::ParameterInspector parameterInspector;
    gr::VisionFrameReader frameReader;
    gr::VisionCameraFrame cameraFrame;

    juce::Viewport pluginViewport;
    std::unique_ptr<EmbeddedEditorCanvas> embeddedCanvas;
    std::unique_ptr<GesturePanel> gesturePanel;
    uintptr_t displayedChildIdentity = 0;
    int displayedSlot = -1;
    juce::Point<int> lastNativeEditorSize;

    juce::Rectangle<int> pluginRailBounds;
    juce::Rectangle<int> pluginPanelBounds;
    juce::Rectangle<int> pluginViewportBounds;
    juce::Rectangle<int> cameraPanelBounds;
    juce::Rectangle<int> cameraPreviewBounds;
    juce::Rectangle<int> cameraInfoBounds;
    juce::Rectangle<int> footerPanelBounds;

    std::unique_ptr<PluginBrowserComponent> pluginBrowser;
    bool adaptiveResizeInProgress = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
