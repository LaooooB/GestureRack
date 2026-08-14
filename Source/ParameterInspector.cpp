#include "ParameterInspector.h"
#include <algorithm>

namespace gr
{
namespace
{
const juce::Colour kBg        { 247, 248, 250 };
const juce::Colour kBorder    { 205, 210, 218 };
const juce::Colour kRecessed  { 235, 238, 242 };
const juce::Colour kTitle     { 45, 48, 56 };
const juce::Colour kSecondary { 112, 120, 134 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kBlue      { 80, 140, 220 };
const juce::Colour kGreen     { 92, 180, 120 };
const juce::Colour kRed       { 215, 80, 80 };

bool mappingTargetsParameter (const GestureBinding& binding, const ParameterDescriptor& descriptor)
{
    if (binding.targetType != MappingTargetType::childParameter)
        return false;
    if (binding.parameterStableId.isNotEmpty() && descriptor.stableId.isNotEmpty())
        return binding.parameterStableId == descriptor.stableId;
    return binding.parameterIndexFallback == descriptor.index && binding.parameterName == descriptor.name;
}
}

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
        auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (8, 2);

        if (selected)
        {
            g.setColour (kBlue.withAlpha (0.14f));
            g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        }

        const auto badges = owner.gestureBadgesForParameter (parameter);
        auto badgeArea = bounds.removeFromRight (96);
        auto valueArea = bounds.removeFromRight (104);

        g.setColour (parameter.automatable ? kTitle : kSecondary);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (parameter.name, bounds, juce::Justification::centredLeft, 1);

        g.setColour (kSecondary);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (parameter.displayValue.isNotEmpty()
                            ? parameter.displayValue
                            : juce::String (parameter.normalizedValue, 3),
                          valueArea, juce::Justification::centredRight, 1);

        if (badges.isNotEmpty())
        {
            g.setColour (kAccent);
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText (badges, badgeArea, juce::Justification::centredRight, 1);
        }
        else if (parameter.automatable)
        {
            g.setColour (kSecondary.withAlpha (0.72f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("DROP", badgeArea, juce::Justification::centredRight);
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
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (7, 2);

        if (selected)
        {
            g.setColour (kBlue.withAlpha (0.14f));
            g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        }

        const auto missing = binding.targetType == MappingTargetType::childParameter
                          && ! owner.isParameterMappingResolved (binding);
        g.setColour (missing ? kRed : (binding.enabled ? kTitle : kSecondary));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (owner.describeBinding (binding) + (missing ? "  [?]" : ""),
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
      mappingList ("Assigned", mappingModel.get())
{
    addAndMakeVisible (parameterList);
    addAndMakeVisible (mappingList);
    parameterList.setRowHeight (30);
    mappingList.setRowHeight (29);
    parameterList.setColour (juce::ListBox::backgroundColourId, juce::Colour (255, 255, 255));
    mappingList.setColour (juce::ListBox::backgroundColourId, juce::Colour (255, 255, 255));
    parameterList.setColour (juce::ListBox::outlineColourId, kBorder);
    mappingList.setColour (juce::ListBox::outlineColourId, kBorder);

    addAndMakeVisible (helpLabel);
    helpLabel.setText ("1  DRAG A GESTURE FROM THE LEFT   \xE2\x86\x92   2  DROP IT ON A PARAMETER",
                       juce::dontSendNotification);
    helpLabel.setColour (juce::Label::textColourId, kTitle);
    helpLabel.setColour (juce::Label::backgroundColourId, kBlue.withAlpha (0.08f));
    helpLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    helpLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, kSecondary);
    statusLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    auto setupSlider = [] (juce::Slider& slider, double min, double max,
                           double step, double initial)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        slider.setRange (min, max, step);
        slider.setValue (initial, juce::dontSendNotification);
        slider.setColour (juce::Slider::textBoxTextColourId, kTitle);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (255, 255, 255));
        slider.setColour (juce::Slider::textBoxOutlineColourId, kBorder);
        slider.setColour (juce::Slider::thumbColourId, kBlue);
        slider.setColour (juce::Slider::trackColourId, kBlue.withAlpha (0.42f));
        slider.setColour (juce::Slider::backgroundColourId, kRecessed);
    };

    addAndMakeVisible (minSlider);
    addAndMakeVisible (maxSlider);
    addAndMakeVisible (smoothingSlider);
    addAndMakeVisible (deadbandSlider);
    setupSlider (minSlider, 0.0, 1.0, 0.001, 0.0);
    setupSlider (maxSlider, 0.0, 1.0, 0.001, 1.0);
    setupSlider (smoothingSlider, 0.0, 1000.0, 1.0, 25.0);
    setupSlider (deadbandSlider, 0.0, 0.05, 0.001, 0.008);
    smoothingSlider.setTextValueSuffix (" ms");

    addAndMakeVisible (invertButton);
    addAndMakeVisible (mappingEnabledButton);
    addAndMakeVisible (removeMappingButton);
    addAndMakeVisible (livePresetButton);
    addAndMakeVisible (smoothPresetButton);

    invertButton.setColour (juce::ToggleButton::textColourId, kTitle);
    mappingEnabledButton.setColour (juce::ToggleButton::textColourId, kTitle);
    mappingEnabledButton.setToggleState (true, juce::dontSendNotification);
    for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton })
    {
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (255, 255, 255));
        button->setColour (juce::TextButton::textColourOffId, kTitle);
        button->setColour (juce::TextButton::textColourOnId, kTitle);
    }

    auto autoApply = [this] { applySelectedMappingControls(); };
    minSlider.onValueChange = autoApply;
    maxSlider.onValueChange = autoApply;
    smoothingSlider.onValueChange = autoApply;
    deadbandSlider.onValueChange = autoApply;
    invertButton.onClick = autoApply;
    mappingEnabledButton.onClick = autoApply;

    removeMappingButton.onClick = [this]
    {
        if (juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        {
            processor.removeGestureMapping (mappings[static_cast<size_t> (selectedMappingRow)].id);
            selectedMappingRow = -1;
            refreshData (true);
        }
    };
    livePresetButton.onClick = [this]
    {
        smoothingSlider.setValue (25.0, juce::sendNotificationSync);
    };
    smoothPresetButton.onClick = [this]
    {
        smoothingSlider.setValue (80.0, juce::sendNotificationSync);
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

bool ParameterInspector::dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint)
{
    if (gesture == ControlGesture::unknown || ! parameterList.getBounds().contains (localPoint))
        return false;

    const auto point = parameterList.getLocalPoint (this, localPoint);
    const auto row = parameterList.getRowContainingPosition (point.x, point.y);
    if (! juce::isPositiveAndBelow (row, static_cast<int> (parameters.size())))
        return false;

    const auto& parameter = parameters[static_cast<size_t> (row)];
    if (! parameter.automatable)
    {
        statusLabel.setText ("THIS PARAMETER IS NOT AUTOMATABLE", juce::dontSendNotification);
        return false;
    }

    juce::String error;
    const auto ok = processor.addParameterGestureMapping (parameter.index, gesture, error);
    if (! ok)
    {
        statusLabel.setText (error.isNotEmpty() ? error : "ASSIGNMENT FAILED", juce::dontSendNotification);
        return false;
    }

    selectedParameterRow = row;
    refreshData (false);
    parameterList.selectRow (row, false, true);
    if (! mappings.empty())
    {
        selectedMappingRow = static_cast<int> (mappings.size()) - 1;
        mappingList.selectRow (selectedMappingRow, false, true);
        loadSelectedMappingControls();
    }
    statusLabel.setText (controlGestureToEmoji (gesture) + "  \xE2\x86\x92  " + parameter.name + "  ASSIGNED",
                         juce::dontSendNotification);
    repaint();
    return true;
}

void ParameterInspector::timerCallback()
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    const auto force = slot != lastSlot || pluginName != lastPluginName;
    refreshData (force);
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

    if (forceRebuild)
        statusLabel.setText (parameters.empty() ? "LOAD A PLUGIN TO SEE ITS PARAMETERS"
                                                : "DRAG A GESTURE ONTO ANY PARAMETER ROW",
                             juce::dontSendNotification);

    lastSlot = slot;
    lastPluginName = pluginName;
}

void ParameterInspector::parameterSelectionChanged (int row)
{
    selectedParameterRow = row;
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
    if (binding.targetType != MappingTargetType::childParameter)
        return;

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
    const auto mappingSelected = juce::isPositiveAndBelow (selectedMappingRow,
                                                            static_cast<int> (mappings.size()));
    const auto continuous = mappingSelected
                         && mappings[static_cast<size_t> (selectedMappingRow)].targetType
                                == MappingTargetType::childParameter;

    for (auto* slider : { &minSlider, &maxSlider, &smoothingSlider, &deadbandSlider })
        slider->setEnabled (continuous);
    invertButton.setEnabled (continuous);
    livePresetButton.setEnabled (continuous);
    smoothPresetButton.setEnabled (continuous);
    mappingEnabledButton.setEnabled (mappingSelected);
    removeMappingButton.setEnabled (mappingSelected);
}

int ParameterInspector::getMappingCountForParameter (const ParameterDescriptor& descriptor) const
{
    return static_cast<int> (std::count_if (mappings.begin(), mappings.end(), [&descriptor] (const GestureBinding& binding)
    {
        return mappingTargetsParameter (binding, descriptor);
    }));
}

juce::String ParameterInspector::gestureBadgesForParameter (const ParameterDescriptor& descriptor) const
{
    juce::String badges;
    for (const auto& binding : mappings)
    {
        if (! mappingTargetsParameter (binding, descriptor))
            continue;
        if (badges.isNotEmpty())
            badges += "  ";
        badges += controlGestureToEmoji (binding.sourceGesture);
    }
    return badges;
}

bool ParameterInspector::isParameterMappingResolved (const GestureBinding& binding) const
{
    if (binding.targetType != MappingTargetType::childParameter)
        return true;

    return std::any_of (parameters.begin(), parameters.end(), [&binding] (const ParameterDescriptor& descriptor)
    {
        return mappingTargetsParameter (binding, descriptor);
    });
}

juce::String ParameterInspector::describeBinding (const GestureBinding& binding) const
{
    const auto target = binding.targetType == MappingTargetType::slotAction
        ? mappingModeToString (binding.mode)
        : binding.parameterName;

    auto text = controlGestureToEmoji (binding.sourceGesture) + "  " + target;
    if (binding.targetType == MappingTargetType::childParameter)
    {
        text += "  " + juce::String (binding.minValue * 100.0f, 0) + "-"
             + juce::String (binding.maxValue * 100.0f, 0) + "%";
        if (binding.inverted)
            text += "  INV";
        if (! binding.enabled)
            text += "  OFF";
    }
    return text;
}

void ParameterInspector::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kBorder);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("PARAMETER ASSIGNMENT", 14, 8, getWidth() - 28, 20,
                juce::Justification::centredLeft);

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PARAMETERS  —  DROP GESTURE HERE",
                parameterList.getX(), parameterList.getY() - 18,
                parameterList.getWidth(), 16, juce::Justification::centredLeft);
    g.drawText ("ASSIGNED",
                mappingList.getX(), mappingList.getY() - 18,
                mappingList.getWidth(), 16, juce::Justification::centredLeft);

    if (minSlider.isVisible())
    {
        const auto labelY = minSlider.getY() - 16;
        g.drawText ("MIN", minSlider.getX(), labelY, minSlider.getWidth(), 14,
                    juce::Justification::centredLeft);
        g.drawText ("MAX", maxSlider.getX(), labelY, maxSlider.getWidth(), 14,
                    juce::Justification::centredLeft);
        g.drawText ("SMOOTH", smoothingSlider.getX(), labelY, smoothingSlider.getWidth(), 14,
                    juce::Justification::centredLeft);
        g.drawText ("DEADBAND", deadbandSlider.getX(), labelY, deadbandSlider.getWidth(), 14,
                    juce::Justification::centredLeft);
    }
}

void ParameterInspector::resized()
{
    auto bounds = getLocalBounds().reduced (14);
    bounds.removeFromTop (24);

    helpLabel.setBounds (bounds.removeFromTop (32).reduced (0, 2));
    bounds.removeFromTop (24);

    auto advanced = bounds.removeFromBottom (112);
    bounds.removeFromBottom (8);

    auto lists = bounds;
    auto left = lists.removeFromLeft (lists.getWidth() * 64 / 100);
    parameterList.setBounds (left);
    lists.removeFromLeft (8);
    mappingList.setBounds (lists);

    advanced.removeFromTop (18);
    auto sliderRow = advanced.removeFromTop (34);
    const auto sliderGap = 6;
    const auto sliderWidth = juce::jmax (1, (sliderRow.getWidth() - sliderGap * 3) / 4);
    minSlider.setBounds (sliderRow.removeFromLeft (sliderWidth));
    sliderRow.removeFromLeft (sliderGap);
    maxSlider.setBounds (sliderRow.removeFromLeft (sliderWidth));
    sliderRow.removeFromLeft (sliderGap);
    smoothingSlider.setBounds (sliderRow.removeFromLeft (sliderWidth));
    sliderRow.removeFromLeft (sliderGap);
    deadbandSlider.setBounds (sliderRow);

    advanced.removeFromTop (6);
    auto actionRow = advanced.removeFromTop (30);
    livePresetButton.setBounds (actionRow.removeFromLeft (86));
    actionRow.removeFromLeft (5);
    smoothPresetButton.setBounds (actionRow.removeFromLeft (100));
    actionRow.removeFromLeft (8);
    invertButton.setBounds (actionRow.removeFromLeft (78));
    actionRow.removeFromLeft (5);
    mappingEnabledButton.setBounds (actionRow.removeFromLeft (52));
    actionRow.removeFromLeft (8);
    removeMappingButton.setBounds (actionRow.removeFromLeft (82));
    actionRow.removeFromLeft (10);
    statusLabel.setBounds (actionRow);
}

}
