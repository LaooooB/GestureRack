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
const juce::Colour kSecondary { 130, 136, 148 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kAccentText{ 60, 42, 12 };
const juce::Colour kBlue      { 80, 140, 220 };
const juce::Colour kGreen     { 92, 180, 120 };
const juce::Colour kRed       { 215, 80, 80 };
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
        const auto count = owner.getMappingCountForParameter (parameter);
        auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (6, 2);

        if (selected)
        {
            g.setColour (kBlue.withAlpha (0.16f));
            g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        }

        g.setColour (parameter.automatable ? kTitle : kSecondary);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (parameter.name, bounds.withTrimmedRight (120), juce::Justification::centredLeft, 1);

        auto right = bounds.removeFromRight (114);
        g.setColour (kSecondary);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (parameter.displayValue.isNotEmpty()
                            ? parameter.displayValue
                            : juce::String (parameter.normalizedValue, 3),
                          right.removeFromLeft (80), juce::Justification::centredRight, 1);

        if (count > 0)
        {
            g.setColour (kAccent);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (count), right, juce::Justification::centredRight);
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
            g.setColour (kBlue.withAlpha (0.16f));
            g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        }

        const auto missing = binding.targetType == MappingTargetType::childParameter
                          && ! owner.isParameterMappingResolved (binding);
        g.setColour (missing ? kRed
                             : (binding.enabled ? kTitle : kSecondary));
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
      mappingList ("Mappings", mappingModel.get())
{
    addAndMakeVisible (parameterList);
    addAndMakeVisible (mappingList);
    parameterList.setRowHeight (28);
    mappingList.setRowHeight (27);
    parameterList.setColour (juce::ListBox::backgroundColourId, kBg);
    mappingList.setColour (juce::ListBox::backgroundColourId, kBg);
    parameterList.setColour (juce::ListBox::outlineColourId, kBorder);
    mappingList.setColour (juce::ListBox::outlineColourId, kBorder);

    addAndMakeVisible (gestureBox);
    const std::array<ControlGesture, 7> gestures {
        ControlGesture::openPalm, ControlGesture::closedFist, ControlGesture::victory,
        ControlGesture::thumbUp, ControlGesture::thumbDown,
        ControlGesture::pointRight, ControlGesture::pointLeft
    };
    for (int i = 0; i < static_cast<int> (gestures.size()); ++i)
        gestureBox.addItem (controlGestureToEmoji (gestures[static_cast<size_t> (i)]) + " "
                            + controlGestureToString (gestures[static_cast<size_t> (i)]), i + 1);
    gestureBox.setSelectedId (3, juce::dontSendNotification);
    processor.setTestGesture (ControlGesture::victory);

    addAndMakeVisible (testEnableButton);
    addAndMakeVisible (heightSlider);
    addAndMakeVisible (triggerButton);
    addAndMakeVisible (learnButton);

    heightSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    heightSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    heightSlider.setRange (0.0, 1.0, 0.001);
    heightSlider.setValue (0.5, juce::dontSendNotification);

    addAndMakeVisible (statusLabel);
    addAndMakeVisible (learnStatusLabel);
    addAndMakeVisible (selectedParameterLabel);
    for (auto* label : { &statusLabel, &learnStatusLabel, &selectedParameterLabel })
    {
        label->setColour (juce::Label::textColourId, kSecondary);
        label->setFont (juce::FontOptions (11.0f));
    }

    auto setupNormalized = [] (juce::Slider& slider, double initial)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        slider.setRange (0.0, 1.0, 0.001);
        slider.setValue (initial, juce::dontSendNotification);
    };

    addAndMakeVisible (minSlider);
    addAndMakeVisible (maxSlider);
    addAndMakeVisible (smoothingSlider);
    addAndMakeVisible (deadbandSlider);
    addAndMakeVisible (invertButton);
    addAndMakeVisible (mappingEnabledButton);
    addAndMakeVisible (removeMappingButton);
    addAndMakeVisible (livePresetButton);
    addAndMakeVisible (smoothPresetButton);

    setupNormalized (minSlider, 0.0);
    setupNormalized (maxSlider, 1.0);
    smoothingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    smoothingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    smoothingSlider.setRange (0.0, 1000.0, 1.0);
    smoothingSlider.setValue (25.0, juce::dontSendNotification);
    smoothingSlider.setTextValueSuffix (" ms");
    deadbandSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    deadbandSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    deadbandSlider.setRange (0.0, 0.05, 0.001);
    deadbandSlider.setValue (0.008, juce::dontSendNotification);
    mappingEnabledButton.setToggleState (true, juce::dontSendNotification);

    // auto-apply: any mapping control change writes through immediately.
    auto autoApply = [this] { applySelectedMappingControls(); };
    minSlider.onValueChange = autoApply;
    maxSlider.onValueChange = autoApply;
    smoothingSlider.onValueChange = autoApply;
    deadbandSlider.onValueChange = autoApply;
    invertButton.onClick = autoApply;
    mappingEnabledButton.onClick = autoApply;

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
    learnButton.onClick = [this]
    {
        if (processor.isParameterLearnArmed())
            processor.cancelParameterLearn();
        else
            beginLearn();
    };
    removeMappingButton.onClick = [this]
    {
        if (juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        {
            processor.removeGestureMapping (mappings[static_cast<size_t> (selectedMappingRow)].id);
            selectedMappingRow = -1;
            refreshData (true);
        }
    };
    // Smoothing presets set the slider value; onValueChange auto-applies the
    // new tau to the selected mapping. Live = 25 ms (snappy, the new default);
    // Smooth = 80 ms (the old gentle default, kept as a one-tap option).
    livePresetButton.onClick = [this] { smoothingSlider.setValue (25.0, juce::sendNotificationSync); };
    smoothPresetButton.onClick = [this] { smoothingSlider.setValue (80.0, juce::sendNotificationSync); };

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
    learnButton.setButtonText (processor.isParameterLearnArmed() ? "CANCEL" : "LEARN");
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
        selectedParameterLabel.setText (p.name, juce::dontSendNotification);
    }
    else
    {
        selectedParameterLabel.setText ("", juce::dontSendNotification);
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

void ParameterInspector::mapSelectedParameter()
{
    if (! juce::isPositiveAndBelow (selectedParameterRow, static_cast<int> (parameters.size())))
        return;

    juce::String error;
    processor.addParameterGestureMapping (parameters[static_cast<size_t> (selectedParameterRow)].index,
                                          getSelectedGesture(), error);
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

    auto text = controlGestureToEmoji (binding.sourceGesture) + " " + target;
    if (binding.targetType == MappingTargetType::childParameter)
    {
        text += "  [" + juce::String (binding.minValue * 100.0f, 0) + "-"
             + juce::String (binding.maxValue * 100.0f, 0) + "%";
        if (binding.inverted)
            text += " \xE2\x86\x94"; // ↕
        if (! binding.enabled)
            text += " off";
        text += "]";
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
    g.drawText ("PARAMETERS", 14, 8, getWidth() - 28, 20,
                juce::Justification::centredLeft);

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PARAMS", 14, 100, getWidth() * 3 / 5 - 20, 18,
                juce::Justification::centredLeft);
    g.drawText ("MAPPINGS", getWidth() * 3 / 5 + 6, 100, getWidth() * 2 / 5 - 20, 18,
                juce::Justification::centredLeft);

    // gesture source chips (draggable) — rects are positioned in resized().
    const std::array<ControlGesture, 7> gestures {
        ControlGesture::openPalm, ControlGesture::closedFist, ControlGesture::victory,
        ControlGesture::thumbUp, ControlGesture::thumbDown,
        ControlGesture::pointRight, ControlGesture::pointLeft
    };
    for (int i = 0; i < 7; ++i)
    {
        const auto chip = gestureSourceRects[static_cast<size_t> (i)];
        const auto isDragged = draggingGesture && draggedGesture == gestures[static_cast<size_t> (i)];
        g.setColour (isDragged ? kAccent.withAlpha (0.25f) : juce::Colour (255, 255, 255));
        g.fillRoundedRectangle (chip.toFloat(), 6.0f);
        g.setColour (isDragged ? kAccent : kBorder);
        g.drawRoundedRectangle (chip.toFloat(), 6.0f, 1.0f);
        g.setColour (kTitle);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (controlGestureToEmoji (gestures[static_cast<size_t> (i)]), chip,
                    juce::Justification::centred);
    }

    // drop target zones (read-only draw; positioned in resized()).
    const auto drawDrop = [&] (juce::Rectangle<int> rect, const juce::String& text, juce::Colour accent)
    {
        const auto hot = draggingGesture && rect.contains (dragPoint);
        g.setColour (hot ? accent.withAlpha (0.18f) : juce::Colour (255, 255, 255));
        g.fillRoundedRectangle (rect.toFloat(), 8.0f);
        g.setColour (hot ? accent : kBorder);
        g.drawRoundedRectangle (rect.toFloat(), 8.0f, 1.0f);
        g.setColour (hot ? accent : kSecondary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, rect, juce::Justification::centred);
    };
    drawDrop (activeDropRect, "\xE2\x96\xB6", kGreen);   // ▶
    drawDrop (bypassDropRect, "\xE2\x8F\xB8", kRed);     // ⏸
    drawDrop (learnDropRect,
              processor.isParameterLearnArmed() ? "\xE2\x9A\x99 ..." : "\xE2\x9A\x99", kBlue); // ⚙
}

void ParameterInspector::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    bounds.removeFromTop (24);

    // gesture picker + test row (compact)
    auto sourceRow = bounds.removeFromTop (32);
    gestureBox.setBounds (sourceRow.removeFromLeft (110));
    sourceRow.removeFromLeft (6);
    testEnableButton.setBounds (sourceRow.removeFromLeft (64));
    sourceRow.removeFromLeft (4);
    heightSlider.setBounds (sourceRow.removeFromLeft (130));
    sourceRow.removeFromLeft (4);
    triggerButton.setBounds (sourceRow.removeFromLeft (78));
    sourceRow.removeFromLeft (8);
    learnButton.setBounds (sourceRow.removeFromLeft (78));

    bounds.removeFromTop (18);
    auto lists = bounds.removeFromTop (juce::jmax (120, bounds.getHeight() - 180));
    auto left = lists.removeFromLeft (lists.getWidth() * 3 / 5);
    parameterList.setBounds (left.reduced (0, 2));
    lists.removeFromLeft (6);
    mappingList.setBounds (lists.reduced (0, 2));

    auto edit = bounds.reduced (0, 4);

    // gesture source chips row + drop targets, positioned for paint() to draw
    // and mouse handlers to hit-test.
    auto chipRow = edit.removeFromTop (30);
    const auto gap = 5;
    const auto chipW = juce::jmax (1, (chipRow.getWidth() - gap * 6) / 7);
    for (int i = 0; i < 7; ++i)
    {
        gestureSourceRects[static_cast<size_t> (i)] = chipRow.removeFromLeft (chipW);
        chipRow.removeFromLeft (gap);
    }
    edit.removeFromTop (6);

    auto first = edit.removeFromTop (28);
    minSlider.setBounds (first.removeFromLeft (130));
    first.removeFromLeft (4);
    maxSlider.setBounds (first.removeFromLeft (130));
    first.removeFromLeft (4);
    smoothingSlider.setBounds (first.removeFromLeft (150));
    first.removeFromLeft (4);
    deadbandSlider.setBounds (first.removeFromLeft (130));

    auto second = edit.removeFromTop (28);
    invertButton.setBounds (second.removeFromLeft (64));
    second.removeFromLeft (4);
    mappingEnabledButton.setBounds (second.removeFromLeft (64));
    second.removeFromLeft (8);
    removeMappingButton.setBounds (second.removeFromLeft (84));
    second.removeFromLeft (8);
    livePresetButton.setBounds (second.removeFromLeft (54));
    second.removeFromLeft (4);
    smoothPresetButton.setBounds (second.removeFromLeft (70));
    second.removeFromLeft (8);
    statusLabel.setBounds (second);

    learnStatusLabel.setBounds (edit.removeFromTop (24));

    // Drop target zones occupy the remaining bottom band: three equal cells
    // the user can drop a gesture chip onto (enable / bypass / learn).
    auto dropBand = edit;
    const auto dGap = 8;
    const auto dW = juce::jmax (1, (dropBand.getWidth() - dGap * 2) / 3);
    activeDropRect = dropBand.removeFromLeft (dW);
    dropBand.removeFromLeft (dGap);
    bypassDropRect = dropBand.removeFromLeft (dW);
    dropBand.removeFromLeft (dGap);
    learnDropRect = dropBand;
}

}
