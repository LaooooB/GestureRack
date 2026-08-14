#pragma once

#include <JuceHeader.h>
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

    // Primary product interaction: drag a gesture from the single gesture palette
    // in PluginEditor and drop it directly on an automatable parameter row.
    bool dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint);

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

    juce::String describeBinding (const GestureBinding& binding) const;
    juce::String gestureBadgesForParameter (const ParameterDescriptor& descriptor) const;
    int getMappingCountForParameter (const ParameterDescriptor& descriptor) const;
    bool isParameterMappingResolved (const GestureBinding& binding) const;

    GestureRackAudioProcessor& processor;

    std::unique_ptr<ParameterListModel> parameterModel;
    std::unique_ptr<MappingListModel> mappingModel;
    juce::ListBox parameterList;
    juce::ListBox mappingList;

    juce::Label helpLabel;
    juce::Label statusLabel;

    juce::Slider minSlider;
    juce::Slider maxSlider;
    juce::Slider smoothingSlider;
    juce::Slider deadbandSlider;
    juce::ToggleButton invertButton { "INVERT" };
    juce::ToggleButton mappingEnabledButton { "ON" };
    juce::TextButton removeMappingButton { "REMOVE" };
    juce::TextButton livePresetButton { "LIVE 25ms" };
    juce::TextButton smoothPresetButton { "SMOOTH 80ms" };

    std::vector<ParameterDescriptor> parameters;
    std::vector<GestureBinding> mappings;
    int selectedParameterRow = -1;
    int selectedMappingRow = -1;
    int lastSlot = -1;
    juce::String lastPluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterInspector)
};
}
