#include "ParameterInspector.h"
#include <algorithm>

namespace gr
{
class ParameterInspector::ParameterListModel final : public juce::ListBoxModel
{
public:
    explicit ParameterListModel (ParameterInspector& ownerToUse) : owner (ownerToUse) {}

    int getNumRows() override { return static_cast<int> (owner.parameters.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (owner.parameters.size())))
            return;

        const auto& parameter = owner.parameters[static_cast<size_t> (row)];
        const auto count = owner.getMappingCountForParameter (parameter);
        auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (6, 2);

        if (selected)
        {
            g.setColour (juce::Colour::fromRGB (58, 83, 128));
            g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        }

        g.setColour (parameter.automatable ? juce::Colour::fromRGB (232, 235, 240)
                                           : juce::Colour::fromRGB (135, 141, 153));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (parameter.name, bounds.withTrimmedRight (140), juce::Justification::centredLeft, 1);

        auto right = bounds.removeFromRight (134);
        g.setColour (juce::Colour::fromRGB (174, 181, 194));
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (parameter.displayValue.isNotEmpty()
                            ? parameter.displayValue
                            : juce::String (parameter.normalizedValue, 3),
                          right.removeFromLeft (88), juce::Justification::centredRight, 1);

        if (count > 0)
        {
            g.setColour (juce::Colour::fromRGB (102, 174, 255));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (count) + " MAP", right, juce::Justification::centredRight);
        }
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        owner.parameterSelectionChanged (lastRowSelected);
    }

private:
    ParameterInspector& owner;
};

class ParameterInspector::MappingListModel final : public juce::ListBoxModel
{
public:
    explicit MappingListModel (ParameterInspector& ownerToUse) : owner (ownerToUse) {}

    int getNumRows() override { return static_cast<int> (owner.mappings.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (owner.mappings.size())))
            return;

        const auto& binding = owner.mappings[static_cast<size_t> (row)];
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (6, 2);

        if (selected)
        {
            g.setColour (juce::Colour::fromRGB (58, 83, 128));
            g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        }

        const auto missing = binding.targetType == MappingTargetType::childParameter
                          && ! owner.isParameterMappingResolved (binding);
        g.setColour (missing ? juce::Colour::fromRGB (235, 120, 104)
                             : (binding.enabled ? juce::Colour::fromRGB (230, 233, 238)
                                                : juce::Colour::fromRGB (120, 126, 138)));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (owner.describeBinding (binding) + (missing ? "  [MISSING]" : ""),
                          bounds, juce::Justification::centredLeft, 1);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        owner.mappingSelectionChanged (lastRowSelected);
    }

private:
    ParameterInspector& owner;
};

ParameterInspector::ParameterInspector (GestureRackAudioProcessor& processorToUse)
    : processor (processorToUse),
      parameterModel (std::make_unique<ParameterListModel> (*this)),
      mappingModel (std::make_unique<MappingListModel> (*this)),
      parameterList ("Parameters", parameterModel.get()),
      mappingList ("Mappings", mappingModel.get())
{
    addAndMakeVisible (parameterList);
    addAndMakeVisible (mappingList);
    parameterList.setRowHeight (28);
    mappingList.setRowHeight (27);

    addAndMakeVisible (gestureBox);
    const std::array<ControlGesture, 7> gestures {
        ControlGesture::openPalm, ControlGesture::closedFist, ControlGesture::victory,
        ControlGesture::thumbUp, ControlGesture::thumbDown,
        ControlGesture::pointRight, ControlGesture::pointLeft
    };
    for (int i = 0; i < static_cast<int> (gestures.size()); ++i)
        gestureBox.addItem (controlGestureToString (gestures[static_cast<size_t> (i)]), i + 1);
    gestureBox.setSelectedId (3, juce::dontSendNotification);
    processor.setTestGesture (ControlGesture::victory);

    addAndMakeVisible (testEnableButton);
    addAndMakeVisible (heightSlider);
    addAndMakeVisible (triggerButton);
    addAndMakeVisible (mapActiveButton);
    addAndMakeVisible (mapBypassButton);
    addAndMakeVisible (mapParameterButton);
    addAndMakeVisible (learnButton);

    heightSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    heightSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
    heightSlider.setRange (0.0, 1.0, 0.001);
    heightSlider.setValue (0.5, juce::dontSendNotification);

    addAndMakeVisible (statusLabel);
    addAndMakeVisible (learnStatusLabel);
    addAndMakeVisible (selectedParameterLabel);
    for (auto* label : { &statusLabel, &learnStatusLabel, &selectedParameterLabel })
    {
        label->setColour (juce::Label::textColourId, juce::Colour::fromRGB (190, 196, 208));
        label->setFont (juce::FontOptions (11.0f));
    }

    auto setupNormalized = [] (juce::Slider& slider, double initial)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        slider.setRange (0.0, 1.0, 0.001);
        slider.setValue (initial, juce::dontSendNotification);
    };

    addAndMakeVisible (minSlider);
    addAndMakeVisible (maxSlider);
    addAndMakeVisible (smoothingSlider);
    addAndMakeVisible (deadbandSlider);
    addAndMakeVisible (invertButton);
    addAndMakeVisible (mappingEnabledButton);
    addAndMakeVisible (applyMappingButton);
    addAndMakeVisible (removeMappingButton);

    setupNormalized (minSlider, 0.0);
    setupNormalized (maxSlider, 1.0);
    smoothingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    smoothingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    smoothingSlider.setRange (0.0, 1000.0, 1.0);
    smoothingSlider.setValue (80.0, juce::dontSendNotification);
    smoothingSlider.setTextValueSuffix (" ms");
    deadbandSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    deadbandSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    deadbandSlider.setRange (0.0, 0.05, 0.001);
    deadbandSlider.setValue (0.008, juce::dontSendNotification);
    mappingEnabledButton.setToggleState (true, juce::dontSendNotification);

    gestureBox.onChange = [this]
    {
        processor.setTestGesture (getSelectedGesture());
    };
    testEnableButton.onClick = [this]
    {
        processor.setTestSignalEnabled (testEnableButton.getToggleState());
    };
    heightSlider.onValueChange = [this]
    {
        processor.setTestHeight (static_cast<float> (heightSlider.getValue()));
    };
    triggerButton.onClick = [this] { processor.triggerTestGestureEntered(); };
    mapActiveButton.onClick = [this] { addSlotAction (MappingMode::triggerSetActive); };
    mapBypassButton.onClick = [this] { addSlotAction (MappingMode::triggerSetBypassed); };
    mapParameterButton.onClick = [this] { mapSelectedParameter(); };
    learnButton.onClick = [this]
    {
        if (processor.isParameterLearnArmed())
            processor.cancelParameterLearn();
        else
            beginLearn();
    };
    applyMappingButton.onClick = [this] { applySelectedMappingControls(); };
    removeMappingButton.onClick = [this]
    {
        if (juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        {
            processor.removeGestureMapping (mappings[static_cast<size_t> (selectedMappingRow)].id);
            selectedMappingRow = -1;
            refreshData (true);
        }
    };

    refreshData (true);
    startTimerHz (20);
}

ParameterInspector::~ParameterInspector()
{
    stopTimer();
    parameterList.setModel (nullptr);
    mappingList.setModel (nullptr);
}

ControlGesture ParameterInspector::getSelectedGesture() const
{
    switch (gestureBox.getSelectedId())
    {
        case 1: return ControlGesture::openPalm;
        case 2: return ControlGesture::closedFist;
        case 3: return ControlGesture::victory;
        case 4: return ControlGesture::thumbUp;
        case 5: return ControlGesture::thumbDown;
        case 6: return ControlGesture::pointRight;
        case 7: return ControlGesture::pointLeft;
        default:return ControlGesture::unknown;
    }
}

void ParameterInspector::timerCallback()
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    const auto force = slot != lastSlot || pluginName != lastPluginName;
    refreshData (force);

    testEnableButton.setToggleState (processor.isTestSignalEnabled(), juce::dontSendNotification);
    heightSlider.setValue (processor.getTestHeight(), juce::dontSendNotification);
    learnButton.setButtonText (processor.isParameterLearnArmed() ? "CANCEL LEARN" : "LEARN PARAM");
    learnStatusLabel.setText (processor.getParameterLearnStatus(), juce::dontSendNotification);
    statusLabel.setText (processor.getMappingStatus(), juce::dontSendNotification);
}

void ParameterInspector::refreshData (bool forceRebuild)
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    auto newParameters = processor.getSlotParameters (slot);
    auto newMappings = processor.getSlotMappings (slot);

    const auto parameterCountChanged = newParameters.size() != parameters.size();
    const auto mappingCountChanged = newMappings.size() != mappings.size();
    parameters = std::move (newParameters);
    mappings = std::move (newMappings);

    if (forceRebuild || parameterCountChanged)
    {
        selectedParameterRow = -1;
        parameterList.deselectAllRows();
    }

    if (selectedMappingRow >= static_cast<int> (mappings.size()))
        selectedMappingRow = -1;

    parameterList.updateContent();
    parameterList.repaint();
    mappingList.updateContent();
    mappingList.repaint();

    if (forceRebuild || mappingCountChanged)
        updateControlEnablement();

    lastSlot = slot;
    lastPluginName = pluginName;
}

void ParameterInspector::parameterSelectionChanged (int row)
{
    selectedParameterRow = row;
    if (juce::isPositiveAndBelow (row, static_cast<int> (parameters.size())))
    {
        const auto& p = parameters[static_cast<size_t> (row)];
        selectedParameterLabel.setText (p.name + "  [" + p.stableId + "]", juce::dontSendNotification);
    }
    else
    {
        selectedParameterLabel.setText ("No parameter selected", juce::dontSendNotification);
    }
    updateControlEnablement();
}

void ParameterInspector::mappingSelectionChanged (int row)
{
    selectedMappingRow = row;
    loadSelectedMappingControls();
    updateControlEnablement();
}

void ParameterInspector::loadSelectedMappingControls()
{
    if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        return;

    const auto& binding = mappings[static_cast<size_t> (selectedMappingRow)];
    minSlider.setValue (binding.minValue, juce::dontSendNotification);
    maxSlider.setValue (binding.maxValue, juce::dontSendNotification);
    smoothingSlider.setValue (binding.smoothingMs, juce::dontSendNotification);
    deadbandSlider.setValue (binding.deadband, juce::dontSendNotification);
    invertButton.setToggleState (binding.inverted, juce::dontSendNotification);
    mappingEnabledButton.setToggleState (binding.enabled, juce::dontSendNotification);
}

void ParameterInspector::applySelectedMappingControls()
{
    if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        return;

    auto binding = mappings[static_cast<size_t> (selectedMappingRow)];
    auto minValue = static_cast<float> (minSlider.getValue());
    auto maxValue = static_cast<float> (maxSlider.getValue());
    if (minValue > maxValue)
        std::swap (minValue, maxValue);

    binding.minValue = minValue;
    binding.maxValue = maxValue;
    binding.smoothingMs = static_cast<float> (smoothingSlider.getValue());
    binding.deadband = static_cast<float> (deadbandSlider.getValue());
    binding.inverted = invertButton.getToggleState();
    binding.enabled = mappingEnabledButton.getToggleState();

    juce::String error;
    processor.updateGestureMapping (binding, error);
    refreshData (false);
}

void ParameterInspector::updateControlEnablement()
{
    const auto parameterSelected = juce::isPositiveAndBelow (selectedParameterRow,
                                                              static_cast<int> (parameters.size()));
    mapParameterButton.setEnabled (parameterSelected && processor.isSlotLoaded (processor.getSelectedSlot()));

    const auto mappingSelected = juce::isPositiveAndBelow (selectedMappingRow,
                                                            static_cast<int> (mappings.size()));
    const auto continuous = mappingSelected
                         && mappings[static_cast<size_t> (selectedMappingRow)].targetType
                                == MappingTargetType::childParameter;

    for (auto* slider : { &minSlider, &maxSlider, &smoothingSlider, &deadbandSlider })
        slider->setEnabled (continuous);
    invertButton.setEnabled (continuous);
    mappingEnabledButton.setEnabled (mappingSelected);
    applyMappingButton.setEnabled (mappingSelected);
    removeMappingButton.setEnabled (mappingSelected);
}

void ParameterInspector::mapSelectedParameter()
{
    if (! juce::isPositiveAndBelow (selectedParameterRow, static_cast<int> (parameters.size())))
        return;

    juce::String error;
    processor.addParameterGestureMapping (parameters[static_cast<size_t> (selectedParameterRow)].index,
                                          getSelectedGesture(), error);
    refreshData (false);
}

void ParameterInspector::addSlotAction (MappingMode mode)
{
    juce::String error;
    processor.addSlotActionGestureMapping (getSelectedGesture(), mode, error);
    refreshData (false);
}

void ParameterInspector::beginLearn()
{
    juce::String error;
    processor.beginParameterLearn (getSelectedGesture(), error);
}

int ParameterInspector::getMappingCountForParameter (const ParameterDescriptor& descriptor) const
{
    return static_cast<int> (std::count_if (mappings.begin(), mappings.end(), [&descriptor] (const GestureBinding& binding)
    {
        if (binding.targetType != MappingTargetType::childParameter)
            return false;
        if (binding.parameterStableId.isNotEmpty() && descriptor.stableId.isNotEmpty())
            return binding.parameterStableId == descriptor.stableId;
        return binding.parameterIndexFallback == descriptor.index && binding.parameterName == descriptor.name;
    }));
}

bool ParameterInspector::isParameterMappingResolved (const GestureBinding& binding) const
{
    if (binding.targetType != MappingTargetType::childParameter)
        return true;

    return std::any_of (parameters.begin(), parameters.end(), [&binding] (const ParameterDescriptor& descriptor)
    {
        if (binding.parameterStableId.isNotEmpty() && descriptor.stableId.isNotEmpty())
            return binding.parameterStableId == descriptor.stableId;
        return binding.parameterIndexFallback == descriptor.index && binding.parameterName == descriptor.name;
    });
}

juce::String ParameterInspector::describeBinding (const GestureBinding& binding) const
{
    auto target = binding.targetType == MappingTargetType::slotAction
        ? mappingModeToString (binding.mode)
        : binding.parameterName;

    auto text = controlGestureToString (binding.sourceGesture) + " -> " + target;
    if (binding.targetType == MappingTargetType::childParameter)
    {
        text += "  [" + juce::String (binding.minValue * 100.0f, 0) + "-"
             + juce::String (binding.maxValue * 100.0f, 0) + "%";
        if (binding.inverted)
            text += " INV";
        text += "]";
    }
    return text;
}

void ParameterInspector::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (18, 21, 27));
    g.setColour (juce::Colour::fromRGB (47, 53, 65));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);

    g.setColour (juce::Colour::fromRGB (223, 227, 234));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("PARAMETERS + GESTURE MAPPINGS", 14, 8, getWidth() - 28, 20,
                juce::Justification::centredLeft);

    g.setColour (juce::Colour::fromRGB (120, 127, 142));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("HOST-VISIBLE PARAMETERS", 14, 104, getWidth() * 3 / 5 - 20, 18,
                juce::Justification::centredLeft);
    g.drawText ("MAPPINGS", getWidth() * 3 / 5 + 6, 104, getWidth() * 2 / 5 - 20, 18,
                juce::Justification::centredLeft);
}

void ParameterInspector::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    bounds.removeFromTop (24);

    auto sourceRow = bounds.removeFromTop (32);
    gestureBox.setBounds (sourceRow.removeFromLeft (126));
    sourceRow.removeFromLeft (6);
    testEnableButton.setBounds (sourceRow.removeFromLeft (94));
    sourceRow.removeFromLeft (4);
    heightSlider.setBounds (sourceRow.removeFromLeft (150));
    sourceRow.removeFromLeft (4);
    triggerButton.setBounds (sourceRow.removeFromLeft (86));
    sourceRow.removeFromLeft (4);
    mapActiveButton.setBounds (sourceRow.removeFromLeft (92));
    sourceRow.removeFromLeft (4);
    mapBypassButton.setBounds (sourceRow.removeFromLeft (96));

    auto actionRow = bounds.removeFromTop (32);
    mapParameterButton.setBounds (actionRow.removeFromLeft (96));
    actionRow.removeFromLeft (5);
    learnButton.setBounds (actionRow.removeFromLeft (108));
    actionRow.removeFromLeft (8);
    selectedParameterLabel.setBounds (actionRow);

    bounds.removeFromTop (18);
    auto lists = bounds.removeFromTop (juce::jmax (150, bounds.getHeight() - 132));
    auto left = lists.removeFromLeft (lists.getWidth() * 3 / 5);
    parameterList.setBounds (left.reduced (0, 2));
    lists.removeFromLeft (6);
    mappingList.setBounds (lists.reduced (0, 2));

    auto edit = bounds.reduced (0, 4);
    auto first = edit.removeFromTop (28);
    minSlider.setBounds (first.removeFromLeft (150));
    first.removeFromLeft (4);
    maxSlider.setBounds (first.removeFromLeft (150));
    first.removeFromLeft (4);
    smoothingSlider.setBounds (first.removeFromLeft (170));
    first.removeFromLeft (4);
    deadbandSlider.setBounds (first.removeFromLeft (150));

    auto second = edit.removeFromTop (28);
    invertButton.setBounds (second.removeFromLeft (76));
    mappingEnabledButton.setBounds (second.removeFromLeft (84));
    applyMappingButton.setBounds (second.removeFromLeft (76));
    second.removeFromLeft (4);
    removeMappingButton.setBounds (second.removeFromLeft (100));
    statusLabel.setBounds (second);

    learnStatusLabel.setBounds (edit.removeFromTop (24));
}
}
