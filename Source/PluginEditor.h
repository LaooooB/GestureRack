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

    // Gesture Rack is intended to become the main workspace once opened.  This controller
    // remembers the largest hosted native editor seen during the current editor session and
    // grows the rack around it.  Switching to a smaller slot therefore never makes the whole
    // window jump smaller again.  It also protects a generous parameter workspace before
    // allowing an oversized native editor to consume the remaining vertical space.
    class WorkspaceSizingController final : private juce::Timer
    {
    public:
        explicit WorkspaceSizingController (GestureRackAudioProcessorEditor& ownerToUse)
            : owner (ownerToUse)
        {
            // Disable the old per-slot shrink/grow path. This controller owns adaptive sizing.
            owner.adaptiveResizeInProgress = true;
            startTimerHz (20);
        }

        ~WorkspaceSizingController() override { stopTimer(); }

    private:
        void timerCallback() override
        {
            const auto native = owner.lastNativeEditorSize;
            if (native.x > 0) largestNative.x = juce::jmax (largestNative.x, native.x);
            if (native.y > 0) largestNative.y = juce::jmax (largestNative.y, native.y);

            auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto* display = displays.getDisplayForRect (owner.getScreenBounds());
            if (display == nullptr) display = displays.getPrimaryDisplay();

            int maxWidth = 3200;
            int maxHeight = 2100;
            if (display != nullptr)
            {
                const auto user = display->userBounds.toNearestInt();
                maxWidth = juce::jmax (1120, juce::roundToInt (user.getWidth() * 0.985f));
                maxHeight = juce::jmax (720, juce::roundToInt (user.getHeight() * 0.965f));
            }

            // Keep the approved left rail / centre editor+parameters / right camera layout.
            // The centre column is deliberately roomy because parameter mapping is a primary
            // workflow, not an auxiliary inspector.
            const auto centerWidth = largestNative.x > 0 ? juce::jmax (980, largestNative.x + 30) : 1040;
            const auto nativePanelHeight = largestNative.y > 0 ? juce::jmax (300, largestNative.y + 66) : 430;
            constexpr int preferredParameterHeight = 410;
            constexpr int fixedWidth = 18 * 2 + 168 + 12 + 12 + 326;
            constexpr int fixedHeight = 18 * 2 + 52 + 12 + 12 + 48;
            const auto workspaceHeight = juce::jmax (820, nativePanelHeight + 12 + preferredParameterHeight);

            auto desiredWidth = juce::jmax (1660, fixedWidth + centerWidth);
            auto desiredHeight = juce::jmax (1040, fixedHeight + workspaceHeight);
            desiredWidth = juce::jlimit (1120, maxWidth, desiredWidth);
            desiredHeight = juce::jlimit (720, maxHeight, desiredHeight);

            owner.setResizeLimits (1120, 720, maxWidth, maxHeight);

            // Sticky growth: do not shrink when selecting a smaller plugin, and do not undo
            // a manual enlargement made by the user.
            const auto nextWidth = juce::jmax (owner.getWidth(), desiredWidth);
            const auto nextHeight = juce::jmax (owner.getHeight(), desiredHeight);
            if (nextWidth != owner.getWidth() || nextHeight != owner.getHeight())
                owner.setSize (nextWidth, nextHeight);

            protectParameterWorkspace();
        }

        void protectParameterWorkspace()
        {
            constexpr int preferredParameterHeight = 410;
            constexpr int minimumPluginPanelHeight = 250;

            auto parameterBounds = owner.parameterInspector.getBounds();
            if (parameterBounds.isEmpty() || parameterBounds.getHeight() >= preferredParameterHeight)
                return;

            auto pluginBounds = owner.pluginPanelBounds;
            if (pluginBounds.isEmpty() || pluginBounds.getHeight() <= minimumPluginPanelHeight)
                return;

            const auto wanted = preferredParameterHeight - parameterBounds.getHeight();
            const auto available = pluginBounds.getHeight() - minimumPluginPanelHeight;
            const auto shift = juce::jmin (wanted, available);
            if (shift <= 0) return;

            // Move the boundary between PLUGIN and PARAMETERS only. Other packages retain
            // their positions, so the established layout remains stable.
            pluginBounds.setHeight (pluginBounds.getHeight() - shift);
            owner.pluginPanelBounds = pluginBounds;

            parameterBounds.setY (parameterBounds.getY() - shift);
            parameterBounds.setHeight (parameterBounds.getHeight() + shift);
            owner.parameterInspector.setBounds (parameterBounds);

            auto pluginContent = pluginBounds.reduced (12);
            auto pluginHeader = pluginContent.removeFromTop (34);
            owner.removeButton.setBounds (pluginHeader.removeFromRight (72));
            pluginHeader.removeFromRight (6);
            owner.bypassButton.setBounds (pluginHeader.removeFromRight (78));
            pluginContent.removeFromTop (4);
            owner.pluginViewportBounds = pluginContent;
            owner.pluginViewport.setBounds (pluginContent);
            owner.updateViewportScrollbars();
            owner.repaint();
        }

        GestureRackAudioProcessorEditor& owner;
        juce::Point<int> largestNative;
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
    WorkspaceSizingController workspaceSizingController { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
