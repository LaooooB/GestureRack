#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "PluginProcessor.h"
#include "ParameterInspector.h"
#include "PluginBrowser.h"
#include "VisionFrameReader.h"
#include "UiTheme.h"
#include "UiMetrics.h"

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
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;

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
    void showPresetMenu();
    void saveUserPreset();
    void loadUserPreset();
    void showPluginMoreMenu();
    void setUiScale (float newScale);
    void syncSlotButtons();
    void updateSlotButtons();
    void layoutPluginChain();
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
    std::vector<std::unique_ptr<RackSlotButton>> slotButtons;
    RackSlotButton addSlotButton;

    int slotDragSource = -1;
    int slotDragTarget = -1;
    bool slotDragging = false;
    juce::Point<int> slotDragOrigin;

    gr::ui::IconButton railSearchButton { gr::ui::Icon::search, "Search plugins" };

    gr::ui::IconButton removeButton { gr::ui::Icon::trash, "Remove plugin" };
    gr::ui::AnimatedTextButton bypassButton { "ACTIVE" };
    gr::ui::IconButton pluginMoreButton { gr::ui::Icon::more, "Plugin options" };

    gr::ui::AnimatedTextButton swapHandsButton { "SWAP L/R" };
    gr::ui::AnimatedTextButton faceMosaicButton { "FACE MOSAIC" };
    gr::ui::AnimatedTextButton presetButton { "PRESET" };

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
    juce::Rectangle<int> pluginRailListBounds;
    int chainScrollOffset = 0;
    juce::Rectangle<int> pluginPanelBounds;
    juce::Rectangle<int> pluginViewportBounds;
    juce::Rectangle<int> cameraPanelBounds;
    juce::Rectangle<int> cameraPreviewBounds;
    juce::Rectangle<int> cameraTelemetryBounds;

    class SlotActionScopeOverlay final : public juce::Component,
                                         private juce::Timer
    {
    public:
        explicit SlotActionScopeOverlay (GestureRackAudioProcessorEditor& ownerToUse)
            : owner (ownerToUse)
        {
            setOpaque (false);
            owner.addAndMakeVisible (*this);
            owner.addMouseListener (this, true);
            startTimerHz (60);
        }

        ~SlotActionScopeOverlay() override
        {
            owner.removeMouseListener (this);
            stopTimer();
        }

        bool hitTest (int x, int y) override
        {
            if (owner.pluginBrowser != nullptr && owner.pluginBrowser->isVisible())
                return false;
            updateGeometry();
            const juce::Point<int> point { x, y };
            return activeScopeRect.contains (point)
                || bypassScopeRect.contains (point)
                || activeGestureRect.contains (point)
                || bypassGestureRect.contains (point);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            updateGeometry();
            const auto point = e.getEventRelativeTo (&owner).getPosition();

            if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
            {
                if (activeGestureRect.contains (point))
                    removePrimary (gr::MappingMode::triggerSetActive);
                else if (bypassGestureRect.contains (point))
                    removePrimary (gr::MappingMode::triggerSetBypassed);
                return;
            }

            if (activeScopeRect.contains (point))
                togglePrimaryScope (gr::MappingMode::triggerSetActive);
            else if (bypassScopeRect.contains (point))
                togglePrimaryScope (gr::MappingMode::triggerSetBypassed);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! e.mods.isLeftButtonDown()) return;
            updateGeometry();
            updateDropScopeFromPoint (e.getEventRelativeTo (&owner).getPosition());
        }

        void paint (juce::Graphics& g) override
        {
            if (owner.pluginBrowser != nullptr && owner.pluginBrowser->isVisible())
                return;
            updateGeometry();
            if (activeTargetRect.isEmpty() || bypassTargetRect.isEmpty())
                return;

            const auto mouse = getMouseXYRelative();
            const auto leftDown = juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown();
            const auto activeDrag = leftDown && activeTargetRect.contains (mouse);
            const auto bypassDrag = leftDown && bypassTargetRect.contains (mouse);

            if (activeDrag)
                drawSplitTarget (g, activeTargetRect, mouse.x);
            else
                drawActionRow (g, activeRowRect,
                               gr::MappingMode::triggerSetActive,
                               "ACTIVE", false,
                               activeGestureRect, activeScopeRect);

            if (bypassDrag)
                drawSplitTarget (g, bypassTargetRect, mouse.x);
            else
                drawActionRow (g, bypassRowRect,
                               gr::MappingMode::triggerSetBypassed,
                               "BYPASS", true,
                               bypassGestureRect, bypassScopeRect);
        }

    private:
        std::optional<gr::GestureBinding> primaryBinding (gr::MappingMode mode) const
        {
            std::optional<gr::GestureBinding> result;
            for (const auto& binding : owner.processor.getSlotMappings (owner.processor.getSelectedSlot()))
                if (binding.targetType == gr::MappingTargetType::slotAction
                    && binding.mode == mode)
                    result = binding;
            return result;
        }

        int bindingCount (gr::MappingMode mode) const
        {
            auto count = 0;
            for (const auto& binding : owner.processor.getSlotMappings (owner.processor.getSelectedSlot()))
                if (binding.targetType == gr::MappingTargetType::slotAction
                    && binding.mode == mode)
                    ++count;
            return count;
        }

        static juce::Colour scopeColour (gr::BindingScope scope)
        {
            return scope == gr::BindingScope::global ? gr::ui::tertiary : gr::ui::accent;
        }

        void togglePrimaryScope (gr::MappingMode mode)
        {
            auto binding = primaryBinding (mode);
            if (! binding.has_value()) return;
            binding->scope = binding->scope == gr::BindingScope::global
                ? gr::BindingScope::selected
                : gr::BindingScope::global;
            juce::String error;
            owner.processor.updateGestureMapping (*binding, error);
            repaint();
        }

        void removePrimary (gr::MappingMode mode)
        {
            if (const auto binding = primaryBinding (mode); binding.has_value())
                owner.processor.removeGestureMapping (binding->id);
            repaint();
        }

        void updateDropScopeFromPoint (juce::Point<int> point)
        {
            if (activeTargetRect.contains (point))
                owner.processor.setNextSlotActionBindingScope (
                    point.x < activeTargetRect.getCentreX()
                        ? gr::BindingScope::selected : gr::BindingScope::global);
            else if (bypassTargetRect.contains (point))
                owner.processor.setNextSlotActionBindingScope (
                    point.x < bypassTargetRect.getCentreX()
                        ? gr::BindingScope::selected : gr::BindingScope::global);
        }

        void drawSplitTarget (juce::Graphics& g,
                              juce::Rectangle<int> rect,
                              int mouseX)
        {
            auto left = rect;
            auto right = left.removeFromRight (left.getWidth() / 2);
            const auto leftHot = left.contains (mouseX, rect.getCentreY());
            const auto selectedScope = mouseX < rect.getCentreX()
                ? gr::BindingScope::selected
                : gr::BindingScope::global;
            owner.processor.setNextSlotActionBindingScope (selectedScope);

            const auto drawHalf = [&] (juce::Rectangle<int> area,
                                       gr::BindingScope scope,
                                       const juce::String& label,
                                       bool hot)
            {
                const auto colour = scopeColour (scope);
                g.setColour (hot ? gr::ui::blend (gr::ui::controlHigh, colour, 0.12f)
                                 : gr::ui::control);
                g.fillRoundedRectangle (area.toFloat(), gr::ui::metrics::controlRadius);
                g.setColour (hot ? colour : gr::ui::border);
                g.drawRoundedRectangle (area.toFloat().reduced (0.5f),
                                        gr::ui::metrics::controlRadius, 0.9f);
                g.setColour (hot ? colour : gr::ui::textMuted);
                g.setFont (gr::ui::metaFont());
                g.drawFittedText (label, area.reduced (3, 0),
                                  juce::Justification::centred, 1);
            };

            drawHalf (left, gr::BindingScope::selected, "SELECTED", leftHot);
            drawHalf (right, gr::BindingScope::global, "GLOBAL", ! leftHot);
        }

        void drawActionRow (juce::Graphics& g,
                            juce::Rectangle<int> rect,
                            gr::MappingMode mode,
                            const juce::String& actionLabel,
                            bool compact,
                            juce::Rectangle<int>& gestureRectOut,
                            juce::Rectangle<int>& scopeRectOut)
        {
            if (rect.isEmpty()) return;

            g.setColour (gr::ui::control.withAlpha (0.98f));
            g.fillRoundedRectangle (rect.toFloat(), gr::ui::metrics::controlRadius);
            g.setColour (gr::ui::border);
            g.drawRoundedRectangle (rect.toFloat().reduced (0.5f),
                                    gr::ui::metrics::controlRadius, 0.8f);

            auto inner = rect.reduced (5, 4);
            const auto actionWidth = compact ? 44 : 58;
            const auto scopeWidth = compact ? 62 : 78;
            auto action = inner.removeFromRight (juce::jmin (actionWidth, inner.getWidth()));
            if (inner.getWidth() > 3) inner.removeFromRight (3);
            scopeRectOut = inner.removeFromRight (juce::jmin (scopeWidth, inner.getWidth()));
            if (inner.getWidth() > 3) inner.removeFromRight (3);
            gestureRectOut = inner;

            const auto binding = primaryBinding (mode);
            const auto count = bindingCount (mode);

            g.setFont (gr::ui::metaFont());
            g.setColour (gr::ui::textMuted);
            g.drawFittedText (actionLabel, action,
                              juce::Justification::centred, 1);

            if (! binding.has_value())
            {
                g.setColour (gr::ui::workspace);
                g.fillRoundedRectangle (gestureRectOut.toFloat(), 4.0f);
                g.setColour (gr::ui::border);
                g.drawRoundedRectangle (gestureRectOut.toFloat().reduced (0.5f), 4.0f, 0.8f);
                g.setColour (gr::ui::textMuted);
                g.setFont (gr::ui::metaFont());
                g.drawFittedText ("DROP", gestureRectOut.reduced (3, 0),
                                  juce::Justification::centred, 1);
                scopeRectOut = {};
                return;
            }

            auto gestureText = gr::controlGestureToShortLabel (binding->sourceGesture).toUpperCase();
            if (count > 1) gestureText += " +" + juce::String (count - 1);

            g.setColour (gr::ui::workspace);
            g.fillRoundedRectangle (gestureRectOut.toFloat(), 4.0f);
            g.setColour (gr::ui::border.withAlpha (0.78f));
            g.drawRoundedRectangle (gestureRectOut.toFloat().reduced (0.5f), 4.0f, 0.8f);
            g.setColour (binding->enabled ? gr::ui::text : gr::ui::textMuted);
            g.setFont (gr::ui::metaFont());
            g.drawFittedText (gestureText, gestureRectOut.reduced (3, 0),
                              juce::Justification::centred, 1);

            const auto colour = scopeColour (binding->scope);
            g.setColour (gr::ui::blend (gr::ui::workspace, colour, 0.08f));
            g.fillRoundedRectangle (scopeRectOut.toFloat(), 4.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (scopeRectOut.toFloat().reduced (0.5f), 4.0f, 0.8f);
            g.setColour (colour);
            g.setFont (gr::ui::metaFont());
            g.drawFittedText (gr::bindingScopeToString (binding->scope).toUpperCase(),
                              scopeRectOut.reduced (3, 0),
                              juce::Justification::centred, 1);
        }

        void updateGeometry()
        {
            if (getBounds() != owner.getLocalBounds())
                setBounds (owner.getLocalBounds());

            activeTargetRect = owner.bypassButton.getBounds();
            activeRowRect = activeTargetRect;
            if (! activeTargetRect.isEmpty() && ! owner.pluginPanelBounds.isEmpty())
            {
                const auto rowWidth = juce::jmin (250, owner.pluginPanelBounds.getWidth() - 28);
                const auto left = juce::jmax (owner.pluginPanelBounds.getX() + 14,
                                              activeTargetRect.getRight() - rowWidth);
                activeRowRect = { left, activeTargetRect.getY(),
                                  activeTargetRect.getRight() - left,
                                  activeTargetRect.getHeight() };
            }

            bypassTargetRect = {};
            bypassRowRect = {};
            if (! owner.cameraPanelBounds.isEmpty() && ! owner.parameterInspector.getBounds().isEmpty())
            {
                const juce::Rectangle<int> gestureBounds {
                    owner.cameraPanelBounds.getX(),
                    owner.parameterInspector.getY(),
                    owner.cameraPanelBounds.getWidth(),
                    owner.parameterInspector.getHeight()
                };
                auto content = gestureBounds.reduced (14);
                content.removeFromTop (28);
                content.removeFromTop (8);
                auto bottom = content.removeFromBottom (56);
                content.removeFromBottom (10);
                constexpr int gap = 10;
                bypassTargetRect = bottom.removeFromLeft ((bottom.getWidth() - gap) / 2);
                bypassRowRect = bypassTargetRect;
            }
        }

        void timerCallback() override
        {
            updateGeometry();
            const auto mouse = getMouseXYRelative();
            const auto leftDown = juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown();
            if (leftDown)
                updateDropScopeFromPoint (mouse);
            toFront (false);
            repaint();
        }

        GestureRackAudioProcessorEditor& owner;
        juce::Rectangle<int> activeTargetRect;
        juce::Rectangle<int> bypassTargetRect;
        juce::Rectangle<int> activeRowRect;
        juce::Rectangle<int> bypassRowRect;
        juce::Rectangle<int> activeGestureRect;
        juce::Rectangle<int> bypassGestureRect;
        juce::Rectangle<int> activeScopeRect;
        juce::Rectangle<int> bypassScopeRect;
    };

    std::unique_ptr<PluginBrowserComponent> pluginBrowser;
    std::unique_ptr<juce::FileChooser> presetFileChooser;
    bool adaptiveResizeInProgress = false;
    float uiScale = 1.0f;
    SlotActionScopeOverlay slotActionScopeOverlay { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
