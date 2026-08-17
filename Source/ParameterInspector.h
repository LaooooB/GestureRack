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

    juce::String describeBinding (const GestureBinding& binding) const;
    juce::String gestureBadgesForParameter (const ParameterDescriptor& descriptor) const;
    bool isParameterMappingResolved (const GestureBinding& binding) const;
    const ParameterDescriptor* descriptorForBinding (const GestureBinding& binding) const;

    GestureRackAudioProcessor& processor;
    std::unique_ptr<ParameterListModel> parameterModel;
    std::unique_ptr<MappingListModel> mappingModel;
    juce::ListBox parameterList;
    juce::ListBox mappingList;

    juce::Label helpLabel;
    juce::Label statusLabel;
    juce::TextButton advancedButton { "ADVANCED" };

    juce::ComboBox behaviorBox;
    juce::Slider minSlider;
    juce::Slider maxSlider;
    juce::Slider curveSlider;
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
    int gestureDropPreviewRow = -1;
    ControlGesture gestureDropPreview = ControlGesture::unknown;
    int lastSlot = -1;
    juce::String lastPluginName;
    bool advancedExpanded = false;
    bool loadingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterInspector)
};
}
