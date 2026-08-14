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

    std::vector<ParameterDescriptor> parameters;
    std::vector<GestureBinding> mappings;
    int selectedParameterRow = -1;
    int selectedMappingRow = -1;
    int lastSlot = -1;
    juce::String lastPluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterInspector)
};
}
