#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>
#include "GestureBinding.h"
#include "PluginProcessor.h"

namespace gr
{
class ParameterInspector final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit ParameterInspector (GestureRackAudioProcessor& processorToUse);
    ~ParameterInspector() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint)
    {
        if (gesture == ControlGesture::unknown || ! parameterList.getBounds().contains (localPoint))
            return false;

        const auto point = parameterList.getLocalPoint (this, localPoint);
        const auto row = parameterList.getRowContainingPosition (point.x, point.y);
        if (! juce::isPositiveAndBelow (row, static_cast<int> (parameters.size()))
            || ! parameters[static_cast<size_t> (row)].automatable)
            return false;

        juce::String error;
        const auto ok = processor.addParameterGestureMapping (
            parameters[static_cast<size_t> (row)].index, gesture, error);
        if (ok)
            refreshData (false);
        return ok;
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto point = event.getPosition();
        for (int i = 0; i < static_cast<int> (gestureSourceRects.size()); ++i)
            if (gestureSourceRects[static_cast<size_t> (i)].contains (point))
            {
                draggedGesture = gestureForSourceIndex (i);
                draggingGesture = draggedGesture != ControlGesture::unknown;
                dragPoint = point;
                repaint();
                return;
            }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! draggingGesture)
            return;
        dragPoint = event.getPosition();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! draggingGesture)
            return;

        const auto point = event.getPosition();
        juce::String error;
        if (activeDropRect.contains (point))
            processor.addSlotActionGestureMapping (draggedGesture, MappingMode::triggerSetActive, error);
        else if (bypassDropRect.contains (point))
            processor.addSlotActionGestureMapping (draggedGesture, MappingMode::triggerSetBypassed, error);
        else if (learnDropRect.contains (point))
        {
            if (processor.isParameterLearnArmed())
                processor.cancelParameterLearn();
            processor.beginParameterLearn (draggedGesture, error);
        }
        else if (parameterList.getBounds().contains (point))
        {
            const auto local = parameterList.getLocalPoint (this, point);
            const auto row = parameterList.getRowContainingPosition (local.x, local.y);
            if (juce::isPositiveAndBelow (row, static_cast<int> (parameters.size()))
                && parameters[static_cast<size_t> (row)].automatable)
                processor.addParameterGestureMapping (parameters[static_cast<size_t> (row)].index,
                                                      draggedGesture, error);
        }

        draggingGesture = false;
        draggedGesture = ControlGesture::unknown;
        repaint();
    }

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

    ControlGesture getSelectedGesture() const;
    static ControlGesture gestureForSourceIndex (int index)
    {
        switch (index)
        {
            case 0: return ControlGesture::openPalm;
            case 1: return ControlGesture::closedFist;
            case 2: return ControlGesture::victory;
            case 3: return ControlGesture::thumbUp;
            case 4: return ControlGesture::thumbDown;
            case 5: return ControlGesture::pointRight;
            case 6: return ControlGesture::pointLeft;
            default:return ControlGesture::unknown;
        }
    }
    void mapSelectedParameter();
    void addSlotAction (MappingMode mode);
    void beginLearn();
    juce::String describeBinding (const GestureBinding& binding) const;
    int getMappingCountForParameter (const ParameterDescriptor& descriptor) const;
    bool isParameterMappingResolved (const GestureBinding& binding) const;

    GestureRackAudioProcessor& processor;

    std::unique_ptr<ParameterListModel> parameterModel;
    std::unique_ptr<MappingListModel> mappingModel;
    juce::ListBox parameterList;
    juce::ListBox mappingList;

    juce::ComboBox gestureBox;
    juce::ToggleButton testEnableButton { "TEST HEIGHT" };
    juce::Slider heightSlider;
    juce::TextButton triggerButton { "TEST ENTER" };
    juce::TextButton mapActiveButton { "MAP ACTIVE" };
    juce::TextButton mapBypassButton { "MAP BYPASS" };
    juce::TextButton mapParameterButton { "MAP PARAM" };
    juce::TextButton learnButton { "LEARN PARAM" };

    juce::Label statusLabel;
    juce::Label learnStatusLabel;
    juce::Label selectedParameterLabel;

    juce::Slider minSlider;
    juce::Slider maxSlider;
    juce::Slider smoothingSlider;
    juce::Slider deadbandSlider;
    juce::ToggleButton invertButton { "INVERT" };
    juce::ToggleButton mappingEnabledButton { "ENABLED" };
    juce::TextButton applyMappingButton { "APPLY" };
    juce::TextButton removeMappingButton { "REMOVE MAP" };

    std::array<juce::Rectangle<int>, 7> gestureSourceRects {};
    juce::Rectangle<int> activeDropRect;
    juce::Rectangle<int> bypassDropRect;
    juce::Rectangle<int> learnDropRect;
    bool draggingGesture = false;
    ControlGesture draggedGesture = ControlGesture::unknown;
    juce::Point<int> dragPoint;

    std::vector<ParameterDescriptor> parameters;
    std::vector<GestureBinding> mappings;
    int selectedParameterRow = -1;
    int selectedMappingRow = -1;
    int lastSlot = -1;
    juce::String lastPluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterInspector)
};
}
