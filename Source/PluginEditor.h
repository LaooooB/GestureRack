#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "ParameterInspector.h"
#include "VisionFrameReader.h"
#include "UiTheme.h"
#include "UiMetrics.h"

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
    bool keyPressed (const juce::KeyPress&) override;

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

    class RackSlotButton final : public juce::Button,
                                 private juce::Timer
    {
    public:
        RackSlotButton();
        ~RackSlotButton() override { stopTimer(); }
        std::function<void()> bodyClick;
        std::function<void()> powerClick;
        std::function<void (const juce::MouseEvent&)> dragDown, dragMove, dragUp;

        void setSlotVisualState (bool loadedToUse, bool bypassedToUse, juce::String pluginNameToUse);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void resized() override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;

        juce::Rectangle<int> powerRect;
        bool loaded = false;
        bool bypassed = false;
        bool bodyPressed = false;
        bool powerPressed = false;
        juce::String pluginName;
        float hoverAmount = 0.0f;
        float hoverTarget = 0.0f;
    };

    void timerCallback() override;
    void showPluginBrowserForSlot (int slotIndex);
    void showPluginBrowser();
    void hidePluginBrowser();
    void showSettingsMenu();
    void showMainMenu();
    void showPluginMoreMenu();
    void setUiScale (float newScale);
    void updateSlotButtons();
    void updateEmbeddedEditor();
    void updateViewportScrollbars();
    void adaptEditorToNativeSize (bool force = false);
    void refreshCameraDisplay();
    void handleSlotMouseDown (int slotIndex, const juce::MouseEvent& e);
    void handleSlotMouseDrag (const juce::MouseEvent& e);
    void handleSlotMouseUp (const juce::MouseEvent& e);
    int findNearestSlot (juce::Point<int> editorPoint) const;
    void drawHandOverlay (juce::Graphics&, juce::Rectangle<float> imageArea,
                          const gr::HandSnapshot&, juce::Colour colour);
    void drawCameraReticle (juce::Graphics&, juce::Rectangle<float> imageArea);
    void drawCameraMotionTelemetry (juce::Graphics&, const gr::DualHandVisionSnapshot&);
    void removeSelectedPlugin();

    GestureRackAudioProcessor& processor;
    std::array<RackSlotButton, GestureRackAudioProcessor::slotCount> slotButtons;

    int slotDragSource = -1;
    int slotDragTarget = -1;
    bool slotDragging = false;
    juce::Point<int> slotDragOrigin;

    gr::ui::IconButton railSearchButton { gr::ui::Icon::search, "Search plugins" };

    gr::ui::IconButton removeButton { gr::ui::Icon::trash, "Remove plugin" };
    gr::ui::AnimatedTextButton bypassButton { "ACTIVE" };
    gr::ui::IconButton pluginMoreButton { gr::ui::Icon::more, "Plugin options" };

    gr::ui::AnimatedTextButton swapHandsButton { "SWAP L/R" };

    gr::ui::IconButton settingsButton { gr::ui::Icon::settings, "Settings" };
    gr::ui::IconButton menuButton { gr::ui::Icon::menu, "Menu" };

    gr::ParameterInspector parameterInspector;
    gr::VisionFrameReader frameReader;
    gr::VisionCameraFrame cameraFrame;
    juce::Image cameraDisplayImage;
    uint64_t cameraDisplaySequence = 0;

    juce::Viewport pluginViewport;
    std::unique_ptr<EmbeddedEditorCanvas> embeddedCanvas;
    std::unique_ptr<GesturePanel> gesturePanel;
    uintptr_t displayedChildIdentity = 0;
    int displayedSlot = -1;
    juce::Point<int> lastNativeEditorSize;
    juce::Point<int> largestNativeEditorSize;

    juce::Rectangle<int> topBarBounds;
    juce::Rectangle<int> pluginRailBounds;
    juce::Rectangle<int> pluginPanelBounds;
    juce::Rectangle<int> pluginViewportBounds;
    juce::Rectangle<int> cameraPanelBounds;
    juce::Rectangle<int> cameraPreviewBounds;
    juce::Rectangle<int> cameraTelemetryBounds;

    std::unique_ptr<PluginBrowserComponent> pluginBrowser;
    bool adaptiveResizeInProgress = false;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};