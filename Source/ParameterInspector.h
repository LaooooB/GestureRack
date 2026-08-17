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
// JUCE's default ScrollBar deliberately quantises small wheel deltas to at least one
// full single-step. That feels rigid in a dense parameter list. This ListBox keeps
// high-resolution wheel deltas and eases the viewport toward the requested position.
class SmoothListBox final : public juce::ListBox,
                            private juce::Timer
{
public:
    SmoothListBox (const juce::String& componentName = {}, juce::ListBoxModel* model = nullptr)
        : juce::ListBox (componentName, model)
    {
    }

    ~SmoothListBox() override { stopTimer(); }

    // Parameter rows are intentionally large touch/drop targets. The Advanced assignment
    // list remains compact enough to show several mappings at once.
    void setRowHeight (int newHeight)
    {
        const auto minimum = getName() == "Parameters" ? 50 : 32;
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

        if (! isTimerRunning())
            targetStart = bar.getCurrentRangeStart();

        const auto travel = juce::jmax (24.0, static_cast<double> (getRowHeight()) * 4.6);
        targetStart -= static_cast<double> (wheel.deltaY) * travel;

        const auto minimum = bar.getMinimumRangeLimit();
        const auto maximum = juce::jmax (minimum,
                                         bar.getMaximumRangeLimit() - bar.getCurrentRangeSize());
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

        // A quick response with a longer ease-out keeps wheel and trackpad scrolling
        // precise without the old row-by-row stepping sensation.
        bar.setCurrentRangeStart (current + difference * 0.24, juce::sendNotificationSync);
    }

    double targetStart = 0.0;
};

class ParameterInspector final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit ParameterInspector (GestureRackAudioProcessor& processorToUse);
    ~ParameterInspector() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Add a clear, large mapping affordance on empty automatable rows. The actual drop
    // target remains the entire parameter row, so the visual affordance is forgiving
    // rather than forcing the user to hit a tiny badge.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (parameterList.getWidth() <= 0 || parameterList.getHeight() <= 0) return;

        const auto laneWidth = juce::jlimit (190, 232, parameterList.getWidth() / 4);
        for (int row = 0; row < static_cast<int> (parameters.size()); ++row)
        {
            const auto& descriptor = parameters[static_cast<size_t> (row)];
            if (! descriptor.automatable) continue;

            bool alreadyMapped = false;
            for (const auto& binding : mappings)
            {
                if (binding.targetType != MappingTargetType::childParameter) continue;
                const auto stableMatch = binding.parameterStableId.isNotEmpty()
                                      && descriptor.stableId.isNotEmpty()
                                      && binding.parameterStableId == descriptor.stableId;
                const auto fallbackMatch = binding.parameterIndexFallback == descriptor.index
                                        && binding.parameterName == descriptor.name;
                if (stableMatch || fallbackMatch)
                {
                    alreadyMapped = true;
                    break;
                }
            }
            if (alreadyMapped) continue;

            auto* rowComponent = parameterList.getComponentForRowNumber (row);
            if (rowComponent == nullptr) continue;
            const auto topLeft = getLocalPoint (rowComponent, { 0, 0 });
            auto lane = juce::Rectangle<int> (parameterList.getRight() - laneWidth - 8,
                                               topLeft.y + 6,
                                               laneWidth,
                                               juce::jmax (20, rowComponent->getHeight() - 12));
            lane = lane.getIntersection (parameterList.getBounds().reduced (2));
            if (lane.isEmpty()) continue;

            const auto hot = row == gestureDropPreviewRow
                          && gestureDropPreview != ControlGesture::unknown;
            g.setColour (hot ? ui::accent.withAlpha (0.09f) : ui::surfaceHigh.withAlpha (0.92f));
            g.fillRoundedRectangle (lane.toFloat(), 7.0f);

            juce::Path outline;
            outline.addRoundedRectangle (lane.toFloat().reduced (0.5f), 7.0f);
            const float dashPattern[] { 5.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
                .createDashedStroke (dashed, outline, dashPattern, 2);
            g.setColour (hot ? ui::accent : ui::border.withAlpha (0.82f));
            g.fillPath (dashed);

            g.setColour (hot ? ui::accent : ui::textMuted.withAlpha (0.82f));
            g.setFont (ui::font (10.2f, juce::Font::bold));
            const auto label = hot && gestureDropPreview != ControlGesture::unknown
                             ? controlGestureToShortLabel (gestureDropPreview) + "  +  ADD"
                             : juce::String ("+  DROP GESTURE");
            g.drawFittedText (label, lane.reduced (8, 0), juce::Justification::centred, 1);
        }
    }

    bool dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint);
    void setGestureDragPreview (ControlGesture gesture, juce::Point<int> localPoint);
    void clearGestureDragPreview();

private:
    class ParameterListModel;
    class MappingListModel;

    void timerCallback() override;
    void refreshData (bool forceRebuild);
    void parameterSelectionChanged (int row);
    void mappingSelectionChanged (int row);
    void loadSelectedMappingControls();
    void applySelectedMappingControls();
    void updateControlEnablement();
    void updateAdvancedVisibility();
    void setHoveredMapping (juce::String mappingId);

    juce::String describeBinding (const GestureBinding& binding) const;
    juce::String gestureBadgesForParameter (const ParameterDescriptor& descriptor) const;
    bool isParameterMappingResolved (const GestureBinding& binding) const;
    const ParameterDescriptor* descriptorForBinding (const GestureBinding& binding) const;

    GestureRackAudioProcessor& processor;
    std::unique_ptr<ParameterListModel> parameterModel;
    std::unique_ptr<MappingListModel> mappingModel;
    SmoothListBox parameterList;
    SmoothListBox mappingList;

    juce::Label helpLabel;
    juce::Label statusLabel;
    ui::AnimatedTextButton advancedButton { "ADVANCED" };

    juce::ComboBox behaviorBox;
    juce::Slider minSlider;
    juce::Slider maxSlider;
    juce::Slider curveSlider;
    juce::Slider smoothingSlider;
    juce::Slider deadbandSlider;
    juce::ToggleButton invertButton { "INVERT" };
    juce::ToggleButton mappingEnabledButton { "ON" };
    ui::AnimatedTextButton removeMappingButton { "REMOVE" };
    ui::AnimatedTextButton livePresetButton { "LIVE 25ms" };
    ui::AnimatedTextButton smoothPresetButton { "SMOOTH 80ms" };

    std::vector<ParameterDescriptor> parameters;
    std::vector<GestureBinding> mappings;
    int selectedParameterRow = -1;
    int selectedMappingRow = -1;
    int gestureDropPreviewRow = -1;
    ControlGesture gestureDropPreview = ControlGesture::unknown;
    int lastSlot = -1;
    juce::String lastPluginName;
    juce::String hoveredMappingId;
    float badgeHoverAmount = 0.0f;
    bool advancedExpanded = false;
    bool loadingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterInspector)
};
}
