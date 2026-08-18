#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>
#include <vector>
#include "GestureBinding.h"
#include "PluginProcessor.h"
#include "UiTheme.h"

namespace gr
{
class SmoothListBox final : public juce::ListBox,
                            private juce::Timer
{
public:
    SmoothListBox (const juce::String& componentName = {}, juce::ListBoxModel* model = nullptr)
        : juce::ListBox (componentName, model) {}

    ~SmoothListBox() override { stopTimer(); }

    void setRowHeight (int newHeight)
    {
        const auto minimum = getName() == "Parameters" ? 46 : 32;
        const auto adjusted = juce::jmax (minimum, newHeight);
        juce::ListBox::setRowHeight (adjusted);
        getVerticalScrollBar().setSingleStepSize (juce::jmax (8.0, adjusted * 0.55));
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        auto& bar = getVerticalScrollBar();
        if (! bar.isVisible() || std::abs (wheel.deltaY) < 0.0001f)
        {
            juce::ListBox::mouseWheelMove (event, wheel);
            return;
        }

        if (! isTimerRunning()) targetStart = bar.getCurrentRangeStart();
        const auto travel = juce::jmax (24.0, static_cast<double> (getRowHeight()) * 4.4);
        targetStart -= static_cast<double> (wheel.deltaY) * travel;
        const auto minimum = bar.getMinimumRangeLimit();
        const auto maximum = juce::jmax (minimum, bar.getMaximumRangeLimit() - bar.getCurrentRangeSize());
        targetStart = juce::jlimit (minimum, maximum, targetStart);
        startTimerHz (60);
    }

private:
    void timerCallback() override
    {
        auto& bar = getVerticalScrollBar();
        const auto current = bar.getCurrentRangeStart();
        const auto difference = targetStart - current;
        if (std::abs (difference) < 0.35)
        {
            bar.setCurrentRangeStart (targetStart, juce::sendNotificationSync);
            stopTimer();
            return;
        }
        bar.setCurrentRangeStart (current + difference * 0.24, juce::sendNotificationSync);
    }

    double targetStart = 0.0;
};

class ThemeComboBox final : public juce::ComboBox
{
public:
    ThemeComboBox() { setLookAndFeel (&ui::themeLookAndFeel()); }
    ~ThemeComboBox() override { setLookAndFeel (nullptr); }
};

class ParameterInspector final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit ParameterInspector (GestureRackAudioProcessor& processorToUse);
    ~ParameterInspector() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    bool dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint);
    void setGestureDragPreview (ControlGesture gesture, juce::Point<int> localPoint);
    void clearGestureDragPreview();

    bool assignSlotActionGesture (ControlGesture gesture, MappingMode mode);

    bool undoLastMapping();
    bool redoLastMapping();
    bool canUndoMapping() const { return undoManager.canUndo(); }

private:
    class ParameterListModel;
    class MappingListModel;
    class MappingSnapshotAction;
    class CurvePreview;

    void timerCallback() override;
    void refreshData (bool forceRebuild);
    void parameterSelectionChanged (int row);
    void mappingSelectionChanged (int row);
    void loadSelectedMappingControls();
    void applySelectedMappingControls();
    void updateControlEnablement();
    void updateAdvancedVisibility();
    void updateCurvePreview();
    void setHoveredMapping (juce::String mappingId);

    void removeMappingAt (int mappingIndex, const juce::String& reason);
    int rangeMappingIndexForParameter (const ParameterDescriptor& descriptor) const;
    void updateRangeBindingLive (const juce::Uuid& id, float value, bool minimumHandle);
    void pushMappingSnapshot (int slotIndex,
                              std::vector<GestureBinding> before,
                              std::vector<GestureBinding> after,
                              const juce::String& transactionName);

    juce::String describeBinding (const GestureBinding& binding) const;
    bool isParameterMappingResolved (const GestureBinding& binding) const;
    const ParameterDescriptor* descriptorForBinding (const GestureBinding& binding) const;

    GestureRackAudioProcessor& processor;
    std::unique_ptr<ParameterListModel> parameterModel;
    std::unique_ptr<MappingListModel> mappingModel;
    SmoothListBox parameterList;
    SmoothListBox mappingList;

    ui::IconButton undoButton { ui::Icon::undo, "Undo mapping" };
    ui::IconButton moreButton { ui::Icon::more, "Mapping options" };

    ThemeComboBox behaviorBox;
    ThemeComboBox axisBox;
    ThemeComboBox curveTypeBox;
    juce::Slider minSlider;
    juce::Slider maxSlider;
    juce::Slider curveSlider;
    juce::Slider sensitivitySlider;
    juce::Slider smoothingSlider;
    juce::Slider deadbandSlider;
    juce::ToggleButton invertButton { "INVERT" };
    juce::ToggleButton mappingEnabledButton { "ON" };
    ui::AnimatedTextButton removeMappingButton { "REMOVE" };
    ui::AnimatedTextButton livePresetButton { "LIVE 25ms" };
    ui::AnimatedTextButton smoothPresetButton { "SMOOTH 80ms" };
    juce::Label statusLabel;
    std::unique_ptr<CurvePreview> curvePreview;

    std::vector<ParameterDescriptor> parameters;
    std::vector<GestureBinding> mappings;
    int selectedParameterRow = -1;
    int selectedMappingRow = -1;
    int gestureDropPreviewRow = -1;
    ControlGesture gestureDropPreview = ControlGesture::unknown;
    ControlGesture learnDropPreview = ControlGesture::unknown;
    juce::String hoveredMappingId;
    int lastSlot = -1;
    juce::String lastPluginName;
    bool advancedExpanded = false;
    bool loadingControls = false;

    juce::Rectangle<int> columnHeaderBounds;
    juce::Rectangle<int> learnDropBounds;

    juce::UndoManager undoManager { 30000, 64 };
    bool learnUndoTracking = false;
    bool learnWasArmed = false;
    int learnUndoSlot = -1;
    std::vector<GestureBinding> learnUndoBefore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterInspector)
};
}
