#include "ParameterInspector.h"
#include <algorithm>

namespace gr
{
namespace
{
const juce::Colour kBg        { 27, 30, 36 };
const juce::Colour kPanel     { 23, 26, 31 };
const juce::Colour kRaised    { 35, 39, 46 };
const juce::Colour kBorder    { 55, 61, 71 };
const juce::Colour kTitle     { 232, 235, 240 };
const juce::Colour kSecondary { 139, 148, 162 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kBlue      { 86, 156, 235 };
const juce::Colour kGreen     { 93, 190, 126 };
const juce::Colour kRed       { 225, 94, 94 };

constexpr int parameterBadgeAreaWidth = 180;

bool mappingTargetsParameter (const GestureBinding& binding, const ParameterDescriptor& descriptor)
{
    if (binding.targetType != MappingTargetType::childParameter)
        return false;
    if (binding.parameterStableId.isNotEmpty() && descriptor.stableId.isNotEmpty())
        return binding.parameterStableId == descriptor.stableId;
    return binding.parameterIndexFallback == descriptor.index && binding.parameterName == descriptor.name;
}

std::vector<int> mappingIndicesForParameter (const std::vector<GestureBinding>& mappings,
                                             const ParameterDescriptor& descriptor)
{
    std::vector<int> result;
    result.reserve (7);
    for (int i = 0; i < static_cast<int> (mappings.size()); ++i)
        if (mappingTargetsParameter (mappings[static_cast<size_t> (i)], descriptor))
            result.push_back (i);
    return result;
}

std::vector<juce::Rectangle<int>> layoutGestureBadges (juce::Rectangle<int> area, int count)
{
    std::vector<juce::Rectangle<int>> result;
    if (count <= 0 || area.isEmpty())
        return result;

    area = area.reduced (1, 4);
    constexpr int gap = 3;
    const auto totalGap = gap * (count - 1);
    const auto chipWidth = juce::jmax (1, (area.getWidth() - totalGap) / count);
    const auto totalWidth = chipWidth * count + totalGap;
    auto x = area.getRight() - totalWidth;

    result.reserve (static_cast<size_t> (count));
    for (int i = 0; i < count; ++i)
    {
        result.emplace_back (x, area.getY(), chipWidth, area.getHeight());
        x += chipWidth + gap;
    }
    return result;
}

int modeToComboId (MappingMode mode) { return static_cast<int> (mode) + 1; }
MappingMode comboIdToMode (int id) { return static_cast<MappingMode> (juce::jmax (1, id) - 1); }
}

class ParameterInspector::ParameterListModel final : public juce::ListBoxModel
{
public:
    explicit ParameterListModel (ParameterInspector& ownerToUse) : owner (ownerToUse) {}
    int getNumRows() override { return static_cast<int> (owner.parameters.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (owner.parameters.size()))) return;
        const auto& parameter = owner.parameters[static_cast<size_t> (row)];
        auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (8, 2);
        const auto isDropTarget = row == owner.gestureDropPreviewRow
                               && owner.gestureDropPreview != ControlGesture::unknown;

        if (isDropTarget)
        {
            g.setColour ((parameter.automatable ? kGreen : kRed).withAlpha (0.14f));
            g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
            g.setColour (parameter.automatable ? kGreen : kRed);
            g.drawRoundedRectangle (bounds.toFloat(), 6.0f, 1.5f);
        }
        else if (selected)
        {
            g.setColour (kBlue.withAlpha (0.14f));
            g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        }
        else if ((row & 1) != 0)
        {
            g.setColour (kRaised.withAlpha (0.28f));
            g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        }

        auto badgeArea = bounds.removeFromRight (parameterBadgeAreaWidth);
        auto typeArea = bounds.removeFromRight (70);
        auto valueArea = bounds.removeFromRight (106);

        g.setColour (parameter.automatable ? kTitle : kSecondary);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawFittedText (parameter.name, bounds, juce::Justification::centredLeft, 1);

        g.setColour (kSecondary);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText (parameter.displayValue.isNotEmpty() ? parameter.displayValue
                                                               : juce::String (parameter.normalizedValue, 3),
                          valueArea, juce::Justification::centredRight, 1);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (parameterKindToString (parameter.kind), typeArea, juce::Justification::centredRight);

        const auto mappingIndices = mappingIndicesForParameter (owner.mappings, parameter);
        if (isDropTarget)
        {
            g.setColour (parameter.automatable ? kGreen : kRed);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawFittedText (parameter.automatable
                                ? controlGestureToShortLabel (owner.gestureDropPreview) + "  ADD TARGET"
                                : juce::String ("LOCKED"),
                              badgeArea, juce::Justification::centredRight, 1);
        }
        else if (! mappingIndices.empty())
        {
            const auto badgeRects = layoutGestureBadges (badgeArea, static_cast<int> (mappingIndices.size()));
            for (int i = 0; i < static_cast<int> (mappingIndices.size()); ++i)
            {
                const auto mappingIndex = mappingIndices[static_cast<size_t> (i)];
                if (! juce::isPositiveAndBelow (mappingIndex, static_cast<int> (owner.mappings.size())))
                    continue;
                const auto& binding = owner.mappings[static_cast<size_t> (mappingIndex)];
                const auto rect = badgeRects[static_cast<size_t> (i)];
                const auto accent = binding.enabled ? kAccent : kSecondary;

                g.setColour (accent.withAlpha (binding.enabled ? 0.14f : 0.08f));
                g.fillRoundedRectangle (rect.toFloat(), 5.0f);
                g.setColour (accent.withAlpha (0.72f));
                g.drawRoundedRectangle (rect.toFloat(), 5.0f, 1.0f);
                g.setColour (accent);
                g.setFont (juce::FontOptions (8.7f, juce::Font::bold));
                g.drawFittedText (controlGestureToShortLabel (binding.sourceGesture), rect.reduced (2, 0),
                                  juce::Justification::centred, 1);
            }
        }
        else if (parameter.automatable)
        {
            g.setColour (kBlue);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("+ DROP", badgeArea, juce::Justification::centredRight);
        }
    }

    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        auto* overlay = dynamic_cast<RowOverlay*> (existing);
        if (overlay == nullptr)
        {
            delete existing;
            overlay = new RowOverlay (*this);
        }
        overlay->setRow (row);
        return overlay;
    }

    void selectedRowsChanged (int row) override { owner.parameterSelectionChanged (row); }

private:
    class RowOverlay final : public juce::Component
    {
    public:
        explicit RowOverlay (ParameterListModel& modelToUse) : model (modelToUse)
        {
            setOpaque (false);
        }

        void setRow (int newRow) noexcept { row = newRow; }

        bool hitTest (int x, int y) override
        {
            return model.findMappingAt (row, { x, y }, getWidth(), getHeight()) >= 0;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            model.handleBadgeMouseDown (row, e, getWidth(), getHeight());
        }

    private:
        ParameterListModel& model;
        int row = -1;
    };

    int findMappingAt (int row, juce::Point<int> point, int width, int height) const
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (owner.parameters.size())))
            return -1;

        const auto& parameter = owner.parameters[static_cast<size_t> (row)];
        const auto mappingIndices = mappingIndicesForParameter (owner.mappings, parameter);
        if (mappingIndices.empty())
            return -1;

        auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (8, 2);
        const auto badgeArea = bounds.removeFromRight (parameterBadgeAreaWidth);
        const auto badgeRects = layoutGestureBadges (badgeArea, static_cast<int> (mappingIndices.size()));
        for (int i = 0; i < static_cast<int> (badgeRects.size()); ++i)
            if (badgeRects[static_cast<size_t> (i)].contains (point))
                return mappingIndices[static_cast<size_t> (i)];
        return -1;
    }

    void handleBadgeMouseDown (int row, const juce::MouseEvent& e, int width, int height)
    {
        const auto mappingIndex = findMappingAt (row, e.getPosition(), width, height);
        if (! juce::isPositiveAndBelow (mappingIndex, static_cast<int> (owner.mappings.size())))
            return;

        owner.selectedParameterRow = row;
        owner.parameterList.selectRow (row, false, true);

        const auto binding = owner.mappings[static_cast<size_t> (mappingIndex)];
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
        {
            const auto parameterName = juce::isPositiveAndBelow (row, static_cast<int> (owner.parameters.size()))
                                     ? owner.parameters[static_cast<size_t> (row)].name
                                     : binding.parameterName;
            const auto gestureName = controlGestureToShortLabel (binding.sourceGesture);
            if (owner.processor.removeGestureMapping (binding.id))
            {
                owner.selectedMappingRow = -1;
                owner.refreshData (true);
                owner.statusLabel.setText (gestureName + " REMOVED FROM " + parameterName,
                                           juce::dontSendNotification);
            }
            return;
        }

        owner.selectedMappingRow = mappingIndex;
        if (owner.advancedExpanded)
            owner.mappingList.selectRow (mappingIndex, false, true);
        owner.loadSelectedMappingControls();
    }

    ParameterInspector& owner;
};

class ParameterInspector::MappingListModel final : public juce::ListBoxModel
{
public:
    explicit MappingListModel (ParameterInspector& ownerToUse) : owner (ownerToUse) {}
    int getNumRows() override { return static_cast<int> (owner.mappings.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (owner.mappings.size()))) return;
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
        g.setFont (juce::FontOptions (10.2f, juce::Font::bold));
        g.drawFittedText (owner.describeBinding (binding) + (missing ? "  [?]" : ""),
                          bounds, juce::Justification::centredLeft, 1);
    }
    void selectedRowsChanged (int row) override { owner.mappingSelectionChanged (row); }
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
    for (auto* list : { &parameterList, &mappingList })
    {
        addAndMakeVisible (*list);
        list->setColour (juce::ListBox::backgroundColourId, kPanel);
        list->setColour (juce::ListBox::outlineColourId, kBorder);
        list->setOutlineThickness (1);
        list->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, kBorder.brighter (0.22f));
        list->getVerticalScrollBar().setColour (juce::ScrollBar::backgroundColourId, kPanel);
    }
    parameterList.setRowHeight (32);
    mappingList.setRowHeight (29);

    addAndMakeVisible (helpLabel);
    helpLabel.setText ("DROP GESTURE TO ADD TARGET  /  RIGHT-CLICK BADGE TO REMOVE", juce::dontSendNotification);
    helpLabel.setColour (juce::Label::textColourId, kSecondary);
    helpLabel.setColour (juce::Label::backgroundColourId, kRaised);
    helpLabel.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    helpLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, kSecondary);
    statusLabel.setFont (juce::FontOptions (9.3f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (advancedButton);
    advancedButton.setColour (juce::TextButton::buttonColourId, kRaised);
    advancedButton.setColour (juce::TextButton::buttonOnColourId, kBlue.withAlpha (0.30f));
    advancedButton.setColour (juce::TextButton::textColourOffId, kTitle);
    advancedButton.setColour (juce::TextButton::textColourOnId, kTitle);
    advancedButton.onClick = [this]
    {
        advancedExpanded = ! advancedExpanded;
        updateAdvancedVisibility();
        resized();
        repaint();
    };

    addAndMakeVisible (behaviorBox);
    behaviorBox.addItem ("CONTINUOUS", modeToComboId (MappingMode::absoluteHeight));
    behaviorBox.addItem ("TOGGLE", modeToComboId (MappingMode::toggleParameter));
    behaviorBox.addItem ("MOMENTARY", modeToComboId (MappingMode::momentaryParameter));
    behaviorBox.addItem ("CYCLE", modeToComboId (MappingMode::cycleParameter));
    behaviorBox.addItem ("STEP +", modeToComboId (MappingMode::stepUpParameter));
    behaviorBox.addItem ("STEP -", modeToComboId (MappingMode::stepDownParameter));
    behaviorBox.addItem ("TRIGGER", modeToComboId (MappingMode::triggerParameter));
    behaviorBox.setColour (juce::ComboBox::backgroundColourId, kPanel);
    behaviorBox.setColour (juce::ComboBox::textColourId, kTitle);
    behaviorBox.setColour (juce::ComboBox::outlineColourId, kBorder);

    auto setupSlider = [] (juce::Slider& slider, double min, double max, double step, double initial)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        slider.setRange (min, max, step);
        slider.setValue (initial, juce::dontSendNotification);
        slider.setColour (juce::Slider::textBoxTextColourId, kTitle);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, kPanel);
        slider.setColour (juce::Slider::textBoxOutlineColourId, kBorder);
        slider.setColour (juce::Slider::thumbColourId, kBlue);
        slider.setColour (juce::Slider::trackColourId, kBlue.withAlpha (0.42f));
        slider.setColour (juce::Slider::backgroundColourId, kRaised);
    };

    for (auto* slider : { &minSlider, &maxSlider, &curveSlider, &smoothingSlider, &deadbandSlider })
        addAndMakeVisible (*slider);
    setupSlider (minSlider, 0.0, 1.0, 0.001, 0.0);
    setupSlider (maxSlider, 0.0, 1.0, 0.001, 1.0);
    setupSlider (curveSlider, -1.0, 1.0, 0.01, 0.0);
    setupSlider (smoothingSlider, 0.0, 1000.0, 1.0, 25.0);
    setupSlider (deadbandSlider, 0.0, 0.05, 0.001, 0.008);
    smoothingSlider.setTextValueSuffix (" ms");

    for (auto* toggle : { &invertButton, &mappingEnabledButton })
    {
        addAndMakeVisible (*toggle);
        toggle->setColour (juce::ToggleButton::textColourId, kTitle);
        toggle->setColour (juce::ToggleButton::tickColourId, kBlue);
    }
    mappingEnabledButton.setToggleState (true, juce::dontSendNotification);

    for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton })
    {
        addAndMakeVisible (*button);
        button->setColour (juce::TextButton::buttonColourId, kRaised);
        button->setColour (juce::TextButton::buttonOnColourId, kBlue.withAlpha (0.30f));
        button->setColour (juce::TextButton::textColourOffId, kTitle);
        button->setColour (juce::TextButton::textColourOnId, kTitle);
    }

    auto autoApply = [this]
    {
        if (! loadingControls) applySelectedMappingControls();
    };
    behaviorBox.onChange = autoApply;
    minSlider.onValueChange = autoApply;
    maxSlider.onValueChange = autoApply;
    curveSlider.onValueChange = autoApply;
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
    livePresetButton.onClick = [this] { smoothingSlider.setValue (25.0, juce::sendNotificationSync); };
    smoothPresetButton.onClick = [this] { smoothingSlider.setValue (80.0, juce::sendNotificationSync); };

    updateAdvancedVisibility();
    refreshData (true);
    startTimerHz (20);
}

ParameterInspector::~ParameterInspector()
{
    stopTimer();
    parameterList.setModel (nullptr);
    mappingList.setModel (nullptr);
}

void ParameterInspector::setGestureDragPreview (ControlGesture gesture, juce::Point<int> localPoint)
{
    auto row = -1;
    if (gesture != ControlGesture::unknown && parameterList.getBounds().contains (localPoint))
    {
        const auto point = parameterList.getLocalPoint (this, localPoint);
        row = parameterList.getRowContainingPosition (point.x, point.y);
        if (! juce::isPositiveAndBelow (row, static_cast<int> (parameters.size()))) row = -1;
    }
    if (row == gestureDropPreviewRow && gesture == gestureDropPreview) return;
    gestureDropPreviewRow = row;
    gestureDropPreview = row >= 0 ? gesture : ControlGesture::unknown;
    parameterList.repaint();
}

void ParameterInspector::clearGestureDragPreview()
{
    if (gestureDropPreviewRow < 0 && gestureDropPreview == ControlGesture::unknown) return;
    gestureDropPreviewRow = -1;
    gestureDropPreview = ControlGesture::unknown;
    parameterList.repaint();
}

bool ParameterInspector::dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint)
{
    clearGestureDragPreview();
    if (gesture == ControlGesture::unknown || ! parameterList.getBounds().contains (localPoint)) return false;
    const auto point = parameterList.getLocalPoint (this, localPoint);
    const auto row = parameterList.getRowContainingPosition (point.x, point.y);
    if (! juce::isPositiveAndBelow (row, static_cast<int> (parameters.size()))) return false;
    const auto& parameter = parameters[static_cast<size_t> (row)];
    if (! parameter.automatable)
    {
        statusLabel.setText ("PARAMETER READ ONLY", juce::dontSendNotification);
        return false;
    }

    juce::String error;
    if (! processor.addParameterGestureMapping (parameter.index, gesture, error))
    {
        statusLabel.setText (error.isNotEmpty() ? error : "ASSIGN FAILED", juce::dontSendNotification);
        return false;
    }

    selectedParameterRow = row;
    refreshData (true);
    parameterList.selectRow (row, false, true);
    for (int i = 0; i < static_cast<int> (mappings.size()); ++i)
        if (mappings[static_cast<size_t> (i)].sourceGesture == gesture
            && mappingTargetsParameter (mappings[static_cast<size_t> (i)], parameter))
        {
            selectedMappingRow = i;
            if (advancedExpanded) mappingList.selectRow (i, false, true);
            break;
        }
    loadSelectedMappingControls();
    statusLabel.setText (controlGestureToShortLabel (gesture) + " -> " + parameter.name,
                         juce::dontSendNotification);
    repaint();
    return true;
}

void ParameterInspector::timerCallback()
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    refreshData (slot != lastSlot || pluginName != lastPluginName);
    repaint();
}

void ParameterInspector::refreshData (bool forceRebuild)
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    const auto oldMappingId = juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()))
                            ? mappings[static_cast<size_t> (selectedMappingRow)].id.toString() : juce::String();

    parameters = processor.getSlotParameters (slot);
    mappings = processor.getSlotMappings (slot);
    lastSlot = slot;
    lastPluginName = pluginName;

    if (forceRebuild)
    {
        parameterList.updateContent();
        mappingList.updateContent();
    }
    else
    {
        parameterList.repaint();
        mappingList.repaint();
    }

    if (oldMappingId.isNotEmpty())
    {
        selectedMappingRow = -1;
        for (int i = 0; i < static_cast<int> (mappings.size()); ++i)
            if (mappings[static_cast<size_t> (i)].id.toString() == oldMappingId)
            { selectedMappingRow = i; break; }
    }
    else if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        selectedMappingRow = -1;

    if (forceRebuild && selectedMappingRow >= 0 && advancedExpanded)
        mappingList.selectRow (selectedMappingRow, false, true);
    statusLabel.setText (processor.getMappingStatus(), juce::dontSendNotification);
    updateControlEnablement();
}

void ParameterInspector::parameterSelectionChanged (int row)
{
    selectedParameterRow = juce::isPositiveAndBelow (row, static_cast<int> (parameters.size())) ? row : -1;
}

void ParameterInspector::mappingSelectionChanged (int row)
{
    selectedMappingRow = juce::isPositiveAndBelow (row, static_cast<int> (mappings.size())) ? row : -1;
    loadSelectedMappingControls();
}

const ParameterDescriptor* ParameterInspector::descriptorForBinding (const GestureBinding& binding) const
{
    for (const auto& descriptor : parameters)
        if (mappingTargetsParameter (binding, descriptor)) return &descriptor;
    return nullptr;
}

void ParameterInspector::loadSelectedMappingControls()
{
    loadingControls = true;
    if (juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
    {
        const auto& binding = mappings[static_cast<size_t> (selectedMappingRow)];
        behaviorBox.setSelectedId (modeToComboId (binding.mode), juce::dontSendNotification);
        minSlider.setValue (binding.minValue, juce::dontSendNotification);
        maxSlider.setValue (binding.maxValue, juce::dontSendNotification);
        curveSlider.setValue (binding.curve, juce::dontSendNotification);
        smoothingSlider.setValue (binding.smoothingMs, juce::dontSendNotification);
        deadbandSlider.setValue (binding.deadband, juce::dontSendNotification);
        invertButton.setToggleState (binding.inverted, juce::dontSendNotification);
        mappingEnabledButton.setToggleState (binding.enabled, juce::dontSendNotification);
    }
    loadingControls = false;
    updateControlEnablement();
    repaint();
}

void ParameterInspector::applySelectedMappingControls()
{
    if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()))) return;
    auto binding = mappings[static_cast<size_t> (selectedMappingRow)];
    binding.enabled = mappingEnabledButton.getToggleState();

    if (binding.targetType == MappingTargetType::childParameter)
    {
        auto minValue = static_cast<float> (minSlider.getValue());
        auto maxValue = static_cast<float> (maxSlider.getValue());
        if (minValue > maxValue) std::swap (minValue, maxValue);
        binding.mode = comboIdToMode (behaviorBox.getSelectedId());
        binding.minValue = minValue;
        binding.maxValue = maxValue;
        binding.curve = static_cast<float> (curveSlider.getValue());
        binding.smoothingMs = static_cast<float> (smoothingSlider.getValue());
        binding.deadband = static_cast<float> (deadbandSlider.getValue());
        binding.inverted = invertButton.getToggleState();
    }

    juce::String error;
    processor.updateGestureMapping (binding, error);
    mappings[static_cast<size_t> (selectedMappingRow)] = binding;
    statusLabel.setText (error.isEmpty() ? "MAPPING UPDATED" : error, juce::dontSendNotification);
    updateControlEnablement();
    repaint();
}

void ParameterInspector::updateControlEnablement()
{
    const auto mappingSelected = juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()));
    const auto child = mappingSelected && mappings[static_cast<size_t> (selectedMappingRow)].targetType
                                      == MappingTargetType::childParameter;
    const auto continuous = child && mappings[static_cast<size_t> (selectedMappingRow)].mode == MappingMode::absoluteHeight;
    behaviorBox.setEnabled (child);
    minSlider.setEnabled (child);
    maxSlider.setEnabled (child);
    curveSlider.setEnabled (continuous);
    smoothingSlider.setEnabled (continuous);
    deadbandSlider.setEnabled (continuous);
    invertButton.setEnabled (continuous);
    livePresetButton.setEnabled (continuous);
    smoothPresetButton.setEnabled (continuous);
    mappingEnabledButton.setEnabled (mappingSelected);
    removeMappingButton.setEnabled (mappingSelected);
}

void ParameterInspector::updateAdvancedVisibility()
{
    mappingList.setVisible (advancedExpanded);
    behaviorBox.setVisible (advancedExpanded);
    for (auto* slider : { &minSlider, &maxSlider, &curveSlider, &smoothingSlider, &deadbandSlider })
        slider->setVisible (advancedExpanded);
    for (auto* toggle : { &invertButton, &mappingEnabledButton }) toggle->setVisible (advancedExpanded);
    for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton }) button->setVisible (advancedExpanded);
    advancedButton.setButtonText (advancedExpanded ? "BASIC" : "ADVANCED");
}

juce::String ParameterInspector::gestureBadgesForParameter (const ParameterDescriptor& descriptor) const
{
    juce::String badges;
    for (const auto& binding : mappings)
    {
        if (! mappingTargetsParameter (binding, descriptor)) continue;
        if (badges.isNotEmpty()) badges += "  |  ";
        badges += controlGestureToShortLabel (binding.sourceGesture);
    }
    return badges;
}

bool ParameterInspector::isParameterMappingResolved (const GestureBinding& binding) const
{
    if (binding.targetType != MappingTargetType::childParameter) return true;
    return descriptorForBinding (binding) != nullptr;
}

juce::String ParameterInspector::describeBinding (const GestureBinding& binding) const
{
    const auto target = binding.targetType == MappingTargetType::slotAction
        ? mappingModeToString (binding.mode) : binding.parameterName;
    auto text = controlGestureToShortLabel (binding.sourceGesture) + " -> " + target;
    if (binding.targetType == MappingTargetType::childParameter)
    {
        text += "  [" + mappingModeToString (binding.mode) + "]";
        if (binding.mode == MappingMode::absoluteHeight)
        {
            text += "  " + juce::String (binding.minValue * 100.0f, 0) + "-"
                 + juce::String (binding.maxValue * 100.0f, 0) + "%";
            if (std::abs (binding.curve) > 0.01f) text += "  C" + juce::String (binding.curve, 2);
        }
        if (binding.inverted) text += "  INV";
    }
    if (! binding.enabled) text += "  OFF";
    return text;
}

void ParameterInspector::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kBorder);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.setColour (kTitle);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("PARAMETERS", 14, 8, getWidth() - 180, 20, juce::Justification::centredLeft);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    auto meter = juce::Rectangle<int> (juce::jmax (14, getWidth() - 150), 8,
                                       juce::jmin (136, getWidth() - 28), 20);
    const auto rightPresent = snapshot.right.present;
    const auto height = rightPresent ? juce::jlimit (0.0f, 1.0f, snapshot.right.height) : 0.0f;
    g.setColour (kRaised); g.fillRoundedRectangle (meter.toFloat(), 5.0f);
    if (rightPresent)
    {
        auto fill = meter.toFloat().reduced (1.0f);
        fill.setWidth (juce::jmax (2.0f, fill.getWidth() * height));
        g.setColour (kGreen.withAlpha (0.26f)); g.fillRoundedRectangle (fill, 4.0f);
    }
    g.setColour (rightPresent ? kTitle : kSecondary);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (rightPresent ? "R " + juce::String (height * 100.0f, 0) + "%" : juce::String ("R --"),
                meter, juce::Justification::centred);

    if (advancedExpanded)
    {
        g.setColour (kSecondary);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("ASSIGNED", mappingList.getX(), mappingList.getY() - 17,
                    mappingList.getWidth(), 15, juce::Justification::centredLeft);

        const auto labelY = behaviorBox.getY() - 15;
        g.drawText ("BEHAVIOR", behaviorBox.getX(), labelY, behaviorBox.getWidth(), 14, juce::Justification::centredLeft);
        g.drawText ("MIN", minSlider.getX(), labelY, minSlider.getWidth(), 14, juce::Justification::centredLeft);
        g.drawText ("MAX", maxSlider.getX(), labelY, maxSlider.getWidth(), 14, juce::Justification::centredLeft);
        g.drawText ("CURVE", curveSlider.getX(), labelY, curveSlider.getWidth(), 14, juce::Justification::centredLeft);
        g.drawText ("SMOOTH", smoothingSlider.getX(), labelY, smoothingSlider.getWidth(), 14, juce::Justification::centredLeft);
        g.drawText ("DEAD", deadbandSlider.getX(), labelY, deadbandSlider.getWidth(), 14, juce::Justification::centredLeft);

        if (curveSlider.isEnabled())
        {
            auto graph = juce::Rectangle<float> (static_cast<float> (curveSlider.getX()),
                                                  static_cast<float> (curveSlider.getBottom() + 4),
                                                  static_cast<float> (curveSlider.getWidth()), 22.0f);
            g.setColour (kPanel); g.fillRoundedRectangle (graph, 4.0f);
            juce::Path path;
            for (int i = 0; i <= 24; ++i)
            {
                const auto x = static_cast<float> (i) / 24.0f;
                const auto y = applyMappingCurve (x, static_cast<float> (curveSlider.getValue()));
                const juce::Point<float> p { graph.getX() + x * graph.getWidth(),
                                             graph.getBottom() - y * graph.getHeight() };
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
            }
            g.setColour (kAccent); g.strokePath (path, juce::PathStrokeType (1.5f));
        }
    }
}

void ParameterInspector::resized()
{
    auto bounds = getLocalBounds().reduced (14);
    bounds.removeFromTop (24);
    helpLabel.setBounds (bounds.removeFromTop (26).reduced (0, 2));
    bounds.removeFromTop (10);

    auto footer = bounds.removeFromBottom (30);
    advancedButton.setBounds (footer.removeFromRight (98));
    footer.removeFromRight (8);
    statusLabel.setBounds (footer);
    bounds.removeFromBottom (8);

    if (! advancedExpanded)
    {
        parameterList.setBounds (bounds);
        mappingList.setBounds (juce::Rectangle<int>());
        behaviorBox.setBounds (juce::Rectangle<int>());
        for (auto* slider : { &minSlider, &maxSlider, &curveSlider, &smoothingSlider, &deadbandSlider }) slider->setBounds (juce::Rectangle<int>());
        for (auto* toggle : { &invertButton, &mappingEnabledButton }) toggle->setBounds (juce::Rectangle<int>());
        for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton }) button->setBounds (juce::Rectangle<int>());
        return;
    }

    auto advanced = bounds.removeFromBottom (142);
    bounds.removeFromBottom (8);
    auto lists = bounds;
    auto left = lists.removeFromLeft (lists.getWidth() * 64 / 100);
    parameterList.setBounds (left);
    lists.removeFromLeft (8);
    mappingList.setBounds (lists);

    advanced.removeFromTop (18);
    auto row = advanced.removeFromTop (34);
    const auto gap = 5;
    const auto widths = juce::jmax (70, (row.getWidth() - gap * 5) / 6);
    behaviorBox.setBounds (row.removeFromLeft (widths)); row.removeFromLeft (gap);
    minSlider.setBounds (row.removeFromLeft (widths)); row.removeFromLeft (gap);
    maxSlider.setBounds (row.removeFromLeft (widths)); row.removeFromLeft (gap);
    curveSlider.setBounds (row.removeFromLeft (widths)); row.removeFromLeft (gap);
    smoothingSlider.setBounds (row.removeFromLeft (widths)); row.removeFromLeft (gap);
    deadbandSlider.setBounds (row);

    advanced.removeFromTop (28);
    auto actions = advanced.removeFromTop (30);
    livePresetButton.setBounds (actions.removeFromLeft (86)); actions.removeFromLeft (5);
    smoothPresetButton.setBounds (actions.removeFromLeft (100)); actions.removeFromLeft (8);
    invertButton.setBounds (actions.removeFromLeft (78)); actions.removeFromLeft (5);
    mappingEnabledButton.setBounds (actions.removeFromLeft (52)); actions.removeFromLeft (8);
    removeMappingButton.setBounds (actions.removeFromLeft (82));
}
}
