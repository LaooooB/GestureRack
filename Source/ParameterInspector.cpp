#include "ParameterInspector.h"
#include <algorithm>
#include <cmath>

namespace gr
{
namespace
{
namespace ui = gr::ui;

bool mappingTargetsParameter (const GestureBinding& binding, const ParameterDescriptor& descriptor)
{
    if (binding.targetType != MappingTargetType::childParameter) return false;
    if (binding.parameterStableId.isNotEmpty() && descriptor.stableId.isNotEmpty())
        return binding.parameterStableId == descriptor.stableId;
    return binding.parameterIndexFallback == descriptor.index && binding.parameterName == descriptor.name;
}

std::vector<int> mappingIndicesForParameter (const std::vector<GestureBinding>& mappings,
                                             const ParameterDescriptor& descriptor)
{
    std::vector<int> result;
    result.reserve (8);
    for (int i = 0; i < static_cast<int> (mappings.size()); ++i)
        if (mappingTargetsParameter (mappings[static_cast<size_t> (i)], descriptor)) result.push_back (i);
    return result;
}

ui::Icon iconForParameterKind (ParameterKind kind)
{
    switch (kind)
    {
        case ParameterKind::toggle:     return ui::Icon::toggle;
        case ParameterKind::choice:     return ui::Icon::choice;
        case ParameterKind::stepped:    return ui::Icon::stepped;
        case ParameterKind::continuous: return ui::Icon::continuous;
        case ParameterKind::readOnly:   return ui::Icon::bypass;
    }
    return ui::Icon::sliders;
}

juce::String gestureUiLabel (ControlGesture gesture)
{
    switch (gesture)
    {
        case ControlGesture::openPalm:   return "Palm";
        case ControlGesture::closedFist: return "Fist";
        case ControlGesture::victory:    return "Victory";
        case ControlGesture::thumbUp:    return "Thumb Up";
        case ControlGesture::thumbDown:  return "Down";
        case ControlGesture::thumbLeft:  return "Left";
        case ControlGesture::thumbRight: return "Right";
        default:                         return "?";
    }
}

ui::Icon iconForGesture (ControlGesture gesture)
{
    switch (gesture)
    {
        case ControlGesture::openPalm:   return ui::Icon::palm;
        case ControlGesture::closedFist: return ui::Icon::fist;
        case ControlGesture::victory:    return ui::Icon::victory;
        case ControlGesture::thumbUp:    return ui::Icon::thumbUp;
        case ControlGesture::thumbDown:  return ui::Icon::arrowDown;
        case ControlGesture::thumbLeft:  return ui::Icon::arrowLeft;
        case ControlGesture::thumbRight: return ui::Icon::arrowRight;
        default:                         return ui::Icon::palm;
    }
}

struct RowColumns
{
    juce::Rectangle<int> parameter;
    juce::Rectangle<int> range;
    juce::Rectangle<int> gesture;
};

RowColumns layoutRowColumns (int width, int height)
{
    auto inner = juce::Rectangle<int> (0, 0, width, height).reduced (9, 3);
    const auto parameterWidth = juce::jlimit (190, 300, juce::roundToInt (inner.getWidth() * 0.31f));
    const auto gestureWidth = juce::jlimit (190, 310, juce::roundToInt (inner.getWidth() * 0.30f));
    RowColumns result;
    result.parameter = inner.removeFromLeft (juce::jmin (parameterWidth, inner.getWidth()));
    inner.removeFromLeft (8);
    result.gesture = inner.removeFromRight (juce::jmin (gestureWidth, inner.getWidth()));
    inner.removeFromRight (8);
    result.range = inner;
    return result;
}

struct RangeGeometry
{
    juce::Rectangle<int> leftText;
    juce::Rectangle<int> track;
    juce::Rectangle<int> rightText;
};

RangeGeometry layoutRangeGeometry (juce::Rectangle<int> bounds)
{
    RangeGeometry result;
    const auto textWidth = juce::jlimit (46, 74, bounds.getWidth() / 4);
    result.leftText = bounds.removeFromLeft (textWidth);
    result.rightText = bounds.removeFromRight (textWidth);
    bounds.reduce (8, 0);
    result.track = bounds.withSizeKeepingCentre (bounds.getWidth(), 14);
    return result;
}

struct PillGeometry
{
    juce::Rectangle<int> rect;
    juce::Rectangle<int> removeRect;
    int mappingIndex = -1;
    int overflow = 0;
};

std::vector<PillGeometry> layoutPills (juce::Rectangle<int> area, const std::vector<int>& mappingIndices)
{
    std::vector<PillGeometry> result;
    if (mappingIndices.empty() || area.isEmpty()) return result;
    area.reduce (1, 6);
    constexpr int gap = 5;
    constexpr int minimumPill = 72;
    const auto capacity = juce::jmax (1, (area.getWidth() + gap) / (minimumPill + gap));
    const auto actualCount = static_cast<int> (mappingIndices.size());
    const auto visibleCount = juce::jmin (actualCount, capacity);
    const auto needsOverflow = actualCount > visibleCount;
    const auto slots = needsOverflow ? juce::jmax (1, visibleCount) : visibleCount;
    const auto pillWidth = juce::jmax (48, (area.getWidth() - gap * (slots - 1)) / slots);

    for (int i = 0; i < slots; ++i)
    {
        PillGeometry pill;
        pill.rect = area.removeFromLeft (pillWidth);
        if (i + 1 < slots) area.removeFromLeft (gap);
        if (needsOverflow && i == slots - 1)
        {
            pill.overflow = actualCount - (slots - 1);
        }
        else
        {
            pill.mappingIndex = mappingIndices[static_cast<size_t> (i)];
            auto remove = pill.rect;
            pill.removeRect = remove.removeFromRight (24).reduced (4, 3);
        }
        result.push_back (pill);
    }
    return result;
}

bool bindingStateEqual (const GestureBinding& a, const GestureBinding& b)
{
    return a.id == b.id
        && a.slotIndex == b.slotIndex
        && a.sourceGesture == b.sourceGesture
        && a.targetType == b.targetType
        && a.mode == b.mode
        && a.pluginIdentifier == b.pluginIdentifier
        && a.parameterStableId == b.parameterStableId
        && a.parameterIndexFallback == b.parameterIndexFallback
        && a.parameterName == b.parameterName
        && std::abs (a.minValue - b.minValue) < 0.00001f
        && std::abs (a.maxValue - b.maxValue) < 0.00001f
        && std::abs (a.smoothingMs - b.smoothingMs) < 0.0001f
        && std::abs (a.deadband - b.deadband) < 0.00001f
        && a.sourceAxis == b.sourceAxis
        && a.curveType == b.curveType
        && std::abs (a.curve - b.curve) < 0.00001f
        && std::abs (a.sensitivity - b.sensitivity) < 0.00001f
        && a.inverted == b.inverted
        && a.enabled == b.enabled;
}

bool mappingSnapshotsDiffer (const std::vector<GestureBinding>& a,
                             const std::vector<GestureBinding>& b)
{
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i)
        if (! bindingStateEqual (a[i], b[i])) return true;
    return false;
}

int modeToComboId (MappingMode mode) { return static_cast<int> (mode) + 1; }
MappingMode comboIdToMode (int id) { return static_cast<MappingMode> (juce::jmax (1, id) - 1); }
int axisToComboId (MappingAxis axis) { return static_cast<int> (axis) + 1; }
MappingAxis comboIdToAxis (int id) { return static_cast<MappingAxis> (juce::jlimit (1, 2, id) - 1); }
int curveToComboId (MappingCurveType curve) { return static_cast<int> (curve) + 1; }
MappingCurveType comboIdToCurve (int id)
{
    return static_cast<MappingCurveType> (juce::jlimit (1, 6, id) - 1);
}
}

class ParameterInspector::MappingSnapshotAction final : public juce::UndoableAction
{
public:
    MappingSnapshotAction (GestureRackAudioProcessor& processorToUse,
                           int slotToUse,
                           std::vector<GestureBinding> beforeToUse,
                           std::vector<GestureBinding> afterToUse)
        : processor (processorToUse), slot (slotToUse),
          before (std::move (beforeToUse)), after (std::move (afterToUse)) {}

    bool perform() override
    {
        if (firstPerform)
        {
            firstPerform = false;
            return true;
        }
        processor.replaceSlotMappingsForUi (slot, after);
        return true;
    }

    bool undo() override
    {
        processor.replaceSlotMappingsForUi (slot, before);
        return true;
    }

    int getSizeInUnits() override
    {
        return juce::jmax (1, static_cast<int> (before.size() + after.size()));
    }

private:
    GestureRackAudioProcessor& processor;
    int slot = 0;
    std::vector<GestureBinding> before;
    std::vector<GestureBinding> after;
    bool firstPerform = true;
};

class ParameterInspector::CurvePreview final : public juce::Component
{
public:
    CurvePreview() { setInterceptsMouseClicks (false, false); }

    void setResponse (MappingAxis newAxis, MappingCurveType newType, float newAmount,
                      float newSensitivity, bool newInverted, bool newEnabled)
    {
        axis = newAxis;
        type = newType;
        amount = juce::jlimit (0.0f, 1.0f, newAmount);
        sensitivity = juce::jlimit (0.25f, 8.0f, newSensitivity);
        inverted = newInverted;
        hasMapping = newEnabled;
        repaint();
    }

    void clearResponse()
    {
        hasMapping = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ui::panelLow);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (ui::border.withAlpha (0.78f));
        g.drawRoundedRectangle (bounds, 7.0f, 0.8f);

        auto inner = getLocalBounds().reduced (10);
        auto header = inner.removeFromTop (18);
        g.setFont (ui::metaFont());
        g.setColour (hasMapping ? ui::text : ui::textMuted);
        g.drawFittedText (hasMapping
                            ? (axis == MappingAxis::horizontal ? "X RESPONSE" : "Y RESPONSE")
                            : "RESPONSE",
                          header.removeFromLeft (juce::jmax (60, header.getWidth() / 2)),
                          juce::Justification::centredLeft, 1);
        if (hasMapping)
        {
            g.setColour (ui::accent);
            g.drawFittedText (juce::String (sensitivity, 2) + "x", header,
                              juce::Justification::centredRight, 1);
        }

        auto graph = inner.toFloat().reduced (2.0f, 5.0f);
        if (graph.getWidth() < 10.0f || graph.getHeight() < 10.0f) return;

        g.setColour (ui::border.withAlpha (0.30f));
        g.drawLine (graph.getX(), graph.getCentreY(), graph.getRight(), graph.getCentreY(), 0.7f);
        g.drawLine (graph.getCentreX(), graph.getY(), graph.getCentreX(), graph.getBottom(), 0.7f);
        g.setColour (ui::textMuted.withAlpha (0.20f));
        g.drawLine (graph.getX(), graph.getBottom(), graph.getRight(), graph.getY(), 0.7f);

        if (! hasMapping) return;

        juce::Path path;
        constexpr int samples = 72;
        for (int i = 0; i < samples; ++i)
        {
            const auto input = static_cast<float> (i) / static_cast<float> (samples - 1);
            auto source = inverted ? 1.0f - input : input;
            source = applyMotionSensitivity (source, sensitivity);
            const auto output = applyMappingCurve (source, type, amount);
            const auto x = juce::jmap (input, 0.0f, 1.0f, graph.getX(), graph.getRight());
            const auto y = juce::jmap (output, 0.0f, 1.0f, graph.getBottom(), graph.getY());
            if (i == 0) path.startNewSubPath (x, y);
            else path.lineTo (x, y);
        }

        g.setColour (ui::accent.withAlpha (0.94f));
        g.strokePath (path, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        auto midpoint = 0.5f;
        auto midSource = inverted ? 1.0f - midpoint : midpoint;
        midSource = applyMotionSensitivity (midSource, sensitivity);
        const auto midOutput = applyMappingCurve (midSource, type, amount);
        const auto mx = juce::jmap (midpoint, 0.0f, 1.0f, graph.getX(), graph.getRight());
        const auto my = juce::jmap (midOutput, 0.0f, 1.0f, graph.getBottom(), graph.getY());
        g.setColour (ui::accent);
        g.fillEllipse (mx - 3.0f, my - 3.0f, 6.0f, 6.0f);
    }

private:
    MappingAxis axis = MappingAxis::vertical;
    MappingCurveType type = MappingCurveType::linear;
    float amount = 1.0f;
    float sensitivity = 1.0f;
    bool inverted = false;
    bool hasMapping = false;
};

class ParameterInspector::ParameterListModel final : public juce::ListBoxModel
{
public:
    explicit ParameterListModel (ParameterInspector& ownerToUse) : owner (ownerToUse) {}
    int getNumRows() override { return static_cast<int> (owner.parameters.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (owner.parameters.size()))) return;
        const auto& parameter = owner.parameters[static_cast<size_t> (row)];
        const auto columns = layoutRowColumns (width, height);
        const auto full = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);
        const auto isDropTarget = row == owner.gestureDropPreviewRow
                               && owner.gestureDropPreview != ControlGesture::unknown;

        if ((row & 1) != 0)
        {
            g.setColour (ui::surfaceHigh.withAlpha (0.20f));
            g.fillRect (full);
        }
        if (selected || isDropTarget)
        {
            g.setColour (ui::accent.withAlpha (isDropTarget ? 0.065f : 0.035f));
            g.fillRoundedRectangle (full.toFloat().reduced (2.0f), 6.0f);
        }

        g.setColour (ui::border.withAlpha (0.56f));
        g.drawHorizontalLine (height - 1, 8.0f, static_cast<float> (width - 8));

        auto parameterArea = columns.parameter;
        auto iconArea = parameterArea.removeFromLeft (26).withSizeKeepingCentre (18, 18);
        ui::drawIcon (g, iconForParameterKind (parameter.kind), iconArea.toFloat(),
                      parameter.automatable ? ui::accent : ui::textMuted.withAlpha (0.52f), 1.45f);
        parameterArea.removeFromLeft (4);
        g.setColour (parameter.automatable ? ui::text : ui::textMuted.withAlpha (0.58f));
        g.setFont (ui::rowFont());
        g.drawFittedText (parameter.name, parameterArea, juce::Justification::centredLeft, 1);

        const auto rangeMapping = owner.rangeMappingIndexForParameter (parameter);
        float minValue = 0.0f;
        float maxValue = 1.0f;
        bool rangeEnabled = false;
        if (juce::isPositiveAndBelow (rangeMapping, static_cast<int> (owner.mappings.size())))
        {
            const auto& binding = owner.mappings[static_cast<size_t> (rangeMapping)];
            minValue = binding.minValue;
            maxValue = binding.maxValue;
            rangeEnabled = binding.targetType == MappingTargetType::childParameter;
        }

        auto range = layoutRangeGeometry (columns.range);
        auto leftText = owner.processor.getParameterTextForUi (owner.processor.getSelectedSlot(), parameter.index, minValue);
        auto rightText = owner.processor.getParameterTextForUi (owner.processor.getSelectedSlot(), parameter.index, maxValue);
        g.setColour (rangeEnabled ? ui::textMuted : ui::textMuted.withAlpha (0.50f));
        g.setFont (ui::metaFont());
        g.drawFittedText (leftText, range.leftText, juce::Justification::centredRight, 1);
        g.drawFittedText (rightText, range.rightText, juce::Justification::centredLeft, 1);

        const auto trackY = static_cast<float> (range.track.getCentreY());
        const auto trackLeft = static_cast<float> (range.track.getX());
        const auto trackRight = static_cast<float> (range.track.getRight());
        const auto minX = juce::jmap (minValue, 0.0f, 1.0f, trackLeft, trackRight);
        const auto maxX = juce::jmap (maxValue, 0.0f, 1.0f, trackLeft, trackRight);
        g.setColour (ui::border);
        g.drawLine (trackLeft, trackY, trackRight, trackY, 1.2f);
        if (rangeEnabled)
        {
            g.setColour (ui::accent.withAlpha (0.70f));
            g.drawLine (minX, trackY, maxX, trackY, 1.5f);
            g.setColour (ui::accent);
            g.fillEllipse (minX - 4.0f, trackY - 4.0f, 8.0f, 8.0f);
            g.fillEllipse (maxX - 4.0f, trackY - 4.0f, 8.0f, 8.0f);
        }
        else
        {
            g.setColour (ui::gray.withAlpha (0.42f));
            g.fillEllipse (trackLeft - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
            g.fillEllipse (trackRight - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
        }

        const auto mappingIndices = mappingIndicesForParameter (owner.mappings, parameter);
        const auto pills = layoutPills (columns.gesture, mappingIndices);
        for (const auto& pill : pills)
        {
            auto drawRect = pill.rect.toFloat().reduced (0.5f);
            if (pill.overflow > 0)
            {
                g.setColour (ui::surfaceHigh);
                g.fillRoundedRectangle (drawRect, 6.0f);
                g.setColour (ui::border);
                g.drawRoundedRectangle (drawRect, 6.0f, 1.0f);
                g.setColour (ui::textMuted);
                g.setFont (ui::controlFont());
                g.drawText ("+" + juce::String (pill.overflow), pill.rect, juce::Justification::centred);
                continue;
            }
            if (! juce::isPositiveAndBelow (pill.mappingIndex, static_cast<int> (owner.mappings.size()))) continue;
            const auto& binding = owner.mappings[static_cast<size_t> (pill.mappingIndex)];
            const auto selectedMapping = pill.mappingIndex == owner.selectedMappingRow;
            const auto hovered = binding.id.toString() == owner.hoveredMappingId;
            g.setColour (selectedMapping ? ui::accent.withAlpha (0.055f) : ui::control);
            g.fillRoundedRectangle (drawRect, 6.0f);
            g.setColour (selectedMapping || hovered ? ui::accent : ui::accent.withAlpha (0.66f));
            g.drawRoundedRectangle (drawRect, 6.0f, 1.0f);
            auto label = pill.rect;
            auto remove = label.removeFromRight (24);
            auto gestureIcon = label.removeFromLeft (22).withSizeKeepingCentre (14, 14);
            ui::drawIcon (g, iconForGesture (binding.sourceGesture), gestureIcon.toFloat(),
                          binding.enabled ? ui::accent : ui::textMuted, 1.3f);
            g.setColour (binding.enabled ? ui::text : ui::textMuted);
            g.setFont (ui::controlFont());
            g.drawFittedText (gestureUiLabel (binding.sourceGesture), label.reduced (2, 0),
                              juce::Justification::centredLeft, 1);
            ui::drawIcon (g, ui::Icon::cross, remove.withSizeKeepingCentre (10, 10).toFloat(),
                          hovered ? ui::accent : ui::textMuted, 1.35f);
        }

        if (mappingIndices.empty() && isDropTarget)
        {
            auto target = columns.gesture.reduced (2, 6).toFloat();
            g.setColour (ui::accent.withAlpha (0.055f));
            g.fillRoundedRectangle (target, 6.0f);
            ui::drawDashedRoundedRect (g, target.reduced (0.5f), 6.0f, ui::accent, 1.0f);
            g.setColour (ui::accent);
            g.setFont (ui::controlFont());
            g.drawText (gestureUiLabel (owner.gestureDropPreview) + "  +  ADD",
                        target.toNearestInt(), juce::Justification::centred);
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
        explicit RowOverlay (ParameterListModel& modelToUse) : model (modelToUse) { setOpaque (false); }
        void setRow (int newRow) noexcept { row = newRow; }

        bool hitTest (int x, int y) override
        {
            if (! juce::isPositiveAndBelow (row, static_cast<int> (model.owner.parameters.size()))) return false;
            const auto point = juce::Point<int> (x, y);
            if (findPillAt (point).mappingIndex >= 0) return true;
            const auto& descriptor = model.owner.parameters[static_cast<size_t> (row)];
            const auto mappingIndex = model.owner.rangeMappingIndexForParameter (descriptor);
            if (! juce::isPositiveAndBelow (mappingIndex, static_cast<int> (model.owner.mappings.size()))) return false;
            return layoutRangeGeometry (layoutRowColumns (getWidth(), getHeight()).range).track.expanded (8, 8).contains (point);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto pill = findPillAt (e.getPosition());
            if (juce::isPositiveAndBelow (pill.mappingIndex, static_cast<int> (model.owner.mappings.size())))
                model.owner.setHoveredMapping (model.owner.mappings[static_cast<size_t> (pill.mappingIndex)].id.toString());
            else
                model.owner.setHoveredMapping ({});
        }

        void mouseExit (const juce::MouseEvent&) override { model.owner.setHoveredMapping ({}); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! juce::isPositiveAndBelow (row, static_cast<int> (model.owner.parameters.size()))) return;
            const auto pill = findPillAt (e.getPosition());
            if (juce::isPositiveAndBelow (pill.mappingIndex, static_cast<int> (model.owner.mappings.size())))
            {
                if (e.mods.isRightButtonDown() || e.mods.isPopupMenu() || pill.removeRect.contains (e.getPosition()))
                {
                    model.owner.removeMappingAt (pill.mappingIndex, "MAPPING REMOVED");
                    return;
                }
                model.owner.selectedParameterRow = row;
                model.owner.parameterList.selectRow (row, false, true);
                model.owner.selectedMappingRow = pill.mappingIndex;
                if (model.owner.advancedExpanded) model.owner.mappingList.selectRow (pill.mappingIndex, false, true);
                model.owner.loadSelectedMappingControls();
                model.owner.repaint();
                return;
            }

            const auto& descriptor = model.owner.parameters[static_cast<size_t> (row)];
            const auto mappingIndex = model.owner.rangeMappingIndexForParameter (descriptor);
            if (! juce::isPositiveAndBelow (mappingIndex, static_cast<int> (model.owner.mappings.size()))) return;
            const auto range = layoutRangeGeometry (layoutRowColumns (getWidth(), getHeight()).range);
            if (! range.track.expanded (8, 8).contains (e.getPosition())) return;

            model.owner.selectedParameterRow = row;
            model.owner.parameterList.selectRow (row, false, true);
            model.owner.selectedMappingRow = mappingIndex;
            if (model.owner.advancedExpanded) model.owner.mappingList.selectRow (mappingIndex, false, true);
            model.owner.loadSelectedMappingControls();

            const auto& binding = model.owner.mappings[static_cast<size_t> (mappingIndex)];
            rangeBindingId = binding.id;
            rangeBefore = model.owner.processor.getSlotMappings (model.owner.processor.getSelectedSlot());
            const auto trackLeft = static_cast<float> (range.track.getX());
            const auto trackRight = static_cast<float> (range.track.getRight());
            const auto minX = juce::jmap (binding.minValue, 0.0f, 1.0f, trackLeft, trackRight);
            const auto maxX = juce::jmap (binding.maxValue, 0.0f, 1.0f, trackLeft, trackRight);
            dragMinimum = std::abs (static_cast<float> (e.x) - minX) <= std::abs (static_cast<float> (e.x) - maxX);
            rangeDragging = true;
            dragRangeTo (e.x);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (rangeDragging) dragRangeTo (e.x);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (! rangeDragging) return;
            rangeDragging = false;
            const auto slot = model.owner.processor.getSelectedSlot();
            const auto after = model.owner.processor.getSlotMappings (slot);
            model.owner.pushMappingSnapshot (slot, std::move (rangeBefore), after, "Adjust mapping range");
            rangeBefore.clear();
            rangeBindingId = {};
        }

    private:
        PillGeometry findPillAt (juce::Point<int> point) const
        {
            if (! juce::isPositiveAndBelow (row, static_cast<int> (model.owner.parameters.size()))) return {};
            const auto& descriptor = model.owner.parameters[static_cast<size_t> (row)];
            const auto indices = mappingIndicesForParameter (model.owner.mappings, descriptor);
            for (const auto& pill : layoutPills (layoutRowColumns (getWidth(), getHeight()).gesture, indices))
                if (pill.mappingIndex >= 0 && pill.rect.contains (point)) return pill;
            return {};
        }

        void dragRangeTo (int x)
        {
            const auto range = layoutRangeGeometry (layoutRowColumns (getWidth(), getHeight()).range);
            const auto normalized = juce::jlimit (0.0f, 1.0f,
                juce::jmap (static_cast<float> (x),
                            static_cast<float> (range.track.getX()),
                            static_cast<float> (range.track.getRight()), 0.0f, 1.0f));
            model.owner.updateRangeBindingLive (rangeBindingId, normalized, dragMinimum);
        }

        ParameterListModel& model;
        int row = -1;
        bool rangeDragging = false;
        bool dragMinimum = true;
        juce::Uuid rangeBindingId;
        std::vector<GestureBinding> rangeBefore;
    };

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
            g.setColour (ui::accent.withAlpha (0.05f));
            g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
            g.setColour (ui::accent.withAlpha (0.55f));
            g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
        }
        const auto missing = binding.targetType == MappingTargetType::childParameter && ! owner.isParameterMappingResolved (binding);
        g.setColour (missing ? ui::accent : (binding.enabled ? ui::text : ui::textMuted));
        g.setFont (ui::controlFont());
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
    setWantsKeyboardFocus (true);

    for (auto* list : { &parameterList, &mappingList })
    {
        addAndMakeVisible (*list);
        list->setColour (juce::ListBox::backgroundColourId, ui::workspace);
        list->setColour (juce::ListBox::outlineColourId, ui::border);
        list->setOutlineThickness (1);
        list->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, ui::gray.withAlpha (0.45f));
        list->getVerticalScrollBar().setColour (juce::ScrollBar::backgroundColourId, ui::workspace);
    }
    parameterList.setRowHeight (48);
    mappingList.setRowHeight (32);

    addAndMakeVisible (undoButton);
    addAndMakeVisible (moreButton);
    undoButton.onClick = [this] { undoLastMapping(); };
    moreButton.onClick = [this]
    {
        advancedExpanded = ! advancedExpanded;
        updateAdvancedVisibility();
        resized();
        repaint();
    };

    const auto configureCombo = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, ui::control);
        box.setColour (juce::ComboBox::textColourId, ui::text);
        box.setColour (juce::ComboBox::outlineColourId, ui::border);
        box.setColour (juce::ComboBox::arrowColourId, ui::textMuted);
    };

    for (auto* box : { &behaviorBox, &axisBox, &curveTypeBox })
    {
        addAndMakeVisible (*box);
        configureCombo (*box);
    }

    behaviorBox.addItem ("CONTINUOUS", modeToComboId (MappingMode::absoluteHeight));
    behaviorBox.addItem ("TOGGLE", modeToComboId (MappingMode::toggleParameter));
    behaviorBox.addItem ("MOMENTARY", modeToComboId (MappingMode::momentaryParameter));
    behaviorBox.addItem ("CYCLE", modeToComboId (MappingMode::cycleParameter));
    behaviorBox.addItem ("STEP +", modeToComboId (MappingMode::stepUpParameter));
    behaviorBox.addItem ("STEP -", modeToComboId (MappingMode::stepDownParameter));
    behaviorBox.addItem ("TRIGGER", modeToComboId (MappingMode::triggerParameter));

    axisBox.addItem ("VERTICAL Y", axisToComboId (MappingAxis::vertical));
    axisBox.addItem ("HORIZONTAL X", axisToComboId (MappingAxis::horizontal));

    curveTypeBox.addItem ("LINEAR", curveToComboId (MappingCurveType::linear));
    curveTypeBox.addItem ("EASE IN", curveToComboId (MappingCurveType::easeIn));
    curveTypeBox.addItem ("EASE OUT", curveToComboId (MappingCurveType::easeOut));
    curveTypeBox.addItem ("S CURVE", curveToComboId (MappingCurveType::sCurve));
    curveTypeBox.addItem ("EXPONENTIAL", curveToComboId (MappingCurveType::exponential));
    curveTypeBox.addItem ("LOGARITHMIC", curveToComboId (MappingCurveType::logarithmic));

    auto configureSlider = [] (juce::Slider& slider, double min, double max, double step)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        slider.setRange (min, max, step);
        slider.setColour (juce::Slider::backgroundColourId, ui::control);
        slider.setColour (juce::Slider::trackColourId, ui::accent.withAlpha (0.62f));
        slider.setColour (juce::Slider::thumbColourId, ui::accent);
        slider.setColour (juce::Slider::textBoxTextColourId, ui::text);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    };
    configureSlider (minSlider, 0.0, 1.0, 0.001);
    configureSlider (maxSlider, 0.0, 1.0, 0.001);
    configureSlider (curveSlider, 0.0, 1.0, 0.01);
    configureSlider (sensitivitySlider, 0.25, 8.0, 0.05);
    configureSlider (smoothingSlider, 0.0, 5000.0, 1.0);
    configureSlider (deadbandSlider, 0.0, 0.25, 0.001);
    sensitivitySlider.setTextValueSuffix (" x");
    smoothingSlider.setTextValueSuffix (" ms");

    for (auto* slider : { &minSlider, &maxSlider, &curveSlider, &sensitivitySlider, &smoothingSlider, &deadbandSlider })
        addAndMakeVisible (*slider);
    for (auto* toggle : { &invertButton, &mappingEnabledButton })
    {
        addAndMakeVisible (*toggle);
        toggle->setColour (juce::ToggleButton::textColourId, ui::text);
        toggle->setColour (juce::ToggleButton::tickColourId, ui::accent);
        toggle->setColour (juce::ToggleButton::tickDisabledColourId, ui::textMuted);
    }
    for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton })
        addAndMakeVisible (*button);

    curvePreview = std::make_unique<CurvePreview>();
    addAndMakeVisible (*curvePreview);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, ui::textMuted);
    statusLabel.setFont (ui::metaFont());
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    auto autoApply = [this]
    {
        if (! loadingControls) applySelectedMappingControls();
    };
    behaviorBox.onChange = autoApply;
    axisBox.onChange = autoApply;
    curveTypeBox.onChange = autoApply;
    minSlider.onValueChange = autoApply;
    maxSlider.onValueChange = autoApply;
    curveSlider.onValueChange = autoApply;
    sensitivitySlider.onValueChange = autoApply;
    smoothingSlider.onValueChange = autoApply;
    deadbandSlider.onValueChange = autoApply;
    invertButton.onClick = autoApply;
    mappingEnabledButton.onClick = autoApply;

    removeMappingButton.onClick = [this]
    {
        if (juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
            removeMappingAt (selectedMappingRow, "MAPPING REMOVED");
    };
    livePresetButton.onClick = [this] { smoothingSlider.setValue (25.0, juce::sendNotificationSync); };
    smoothPresetButton.onClick = [this] { smoothingSlider.setValue (80.0, juce::sendNotificationSync); };

    updateAdvancedVisibility();
    refreshData (true);
    startTimerHz (30);
}

ParameterInspector::~ParameterInspector()
{
    stopTimer();
    parameterList.setModel (nullptr);
    mappingList.setModel (nullptr);
}

void ParameterInspector::setHoveredMapping (juce::String mappingId)
{
    if (hoveredMappingId == mappingId) return;
    hoveredMappingId = std::move (mappingId);
    parameterList.repaint();
}

void ParameterInspector::setGestureDragPreview (ControlGesture gesture, juce::Point<int> localPoint)
{
    auto nextRow = -1;
    auto nextLearn = ControlGesture::unknown;
    if (gesture != ControlGesture::unknown)
    {
        if (learnDropBounds.contains (localPoint))
            nextLearn = gesture;
        else if (parameterList.getBounds().contains (localPoint))
        {
            const auto point = parameterList.getLocalPoint (this, localPoint);
            nextRow = parameterList.getRowContainingPosition (point.x, point.y);
            if (! juce::isPositiveAndBelow (nextRow, static_cast<int> (parameters.size()))) nextRow = -1;
        }
    }
    if (nextRow == gestureDropPreviewRow && nextLearn == learnDropPreview
        && (nextRow < 0 || gesture == gestureDropPreview)) return;
    gestureDropPreviewRow = nextRow;
    gestureDropPreview = nextRow >= 0 ? gesture : ControlGesture::unknown;
    learnDropPreview = nextLearn;
    parameterList.repaint();
    repaint();
}

void ParameterInspector::clearGestureDragPreview()
{
    if (gestureDropPreviewRow < 0 && gestureDropPreview == ControlGesture::unknown
        && learnDropPreview == ControlGesture::unknown) return;
    gestureDropPreviewRow = -1;
    gestureDropPreview = ControlGesture::unknown;
    learnDropPreview = ControlGesture::unknown;
    parameterList.repaint();
    repaint();
}

bool ParameterInspector::dropGestureAt (ControlGesture gesture, juce::Point<int> localPoint)
{
    clearGestureDragPreview();
    if (gesture == ControlGesture::unknown) return false;

    if (learnDropBounds.contains (localPoint))
    {
        if (processor.isParameterLearnArmed()) processor.cancelParameterLearn();
        juce::String error;
        const auto slot = processor.getSelectedSlot();
        auto before = processor.getSlotMappings (slot);
        if (! processor.beginParameterLearn (gesture, error))
        {
            statusLabel.setText (error.isNotEmpty() ? error : "LEARN FAILED", juce::dontSendNotification);
            return false;
        }
        learnUndoTracking = true;
        learnWasArmed = true;
        learnUndoSlot = slot;
        learnUndoBefore = std::move (before);
        repaint();
        return true;
    }

    if (! parameterList.getBounds().contains (localPoint)) return false;
    const auto point = parameterList.getLocalPoint (this, localPoint);
    const auto row = parameterList.getRowContainingPosition (point.x, point.y);
    if (! juce::isPositiveAndBelow (row, static_cast<int> (parameters.size()))) return false;
    const auto& parameter = parameters[static_cast<size_t> (row)];
    if (! parameter.automatable)
    {
        statusLabel.setText ("PARAMETER READ ONLY", juce::dontSendNotification);
        return false;
    }

    const auto slot = processor.getSelectedSlot();
    const auto before = processor.getSlotMappings (slot);
    juce::String error;
    if (! processor.addParameterGestureMapping (parameter.index, gesture, error))
    {
        statusLabel.setText (error.isNotEmpty() ? error : "ASSIGN FAILED", juce::dontSendNotification);
        return false;
    }

    const auto after = processor.getSlotMappings (slot);
    pushMappingSnapshot (slot, before, after, "Assign gesture");
    selectedParameterRow = row;
    refreshData (true);
    parameterList.selectRow (row, false, true);
    for (int i = static_cast<int> (mappings.size()) - 1; i >= 0; --i)
        if (mappings[static_cast<size_t> (i)].sourceGesture == gesture
            && mappingTargetsParameter (mappings[static_cast<size_t> (i)], parameter))
        {
            selectedMappingRow = i;
            if (advancedExpanded) mappingList.selectRow (i, false, true);
            break;
        }
    loadSelectedMappingControls();
    statusLabel.setText (gestureUiLabel (gesture) + "  ->  " + parameter.name, juce::dontSendNotification);
    repaint();
    return true;
}

bool ParameterInspector::assignSlotActionGesture (ControlGesture gesture, MappingMode mode)
{
    if (gesture == ControlGesture::unknown) return false;
    const auto slot = processor.getSelectedSlot();
    const auto before = processor.getSlotMappings (slot);
    juce::String error;
    if (! processor.addSlotActionGestureMapping (gesture, mode, error))
    {
        statusLabel.setText (error.isNotEmpty() ? error : "ASSIGN FAILED", juce::dontSendNotification);
        return false;
    }
    const auto after = processor.getSlotMappings (slot);
    pushMappingSnapshot (slot, before, after,
                         mode == MappingMode::triggerSetActive ? "Assign ACTIVE gesture" : "Assign BYPASS gesture");
    refreshData (true);
    return true;
}

void ParameterInspector::removeMappingAt (int mappingIndex, const juce::String& reason)
{
    if (! juce::isPositiveAndBelow (mappingIndex, static_cast<int> (mappings.size()))) return;
    const auto slot = processor.getSelectedSlot();
    const auto before = processor.getSlotMappings (slot);
    const auto id = mappings[static_cast<size_t> (mappingIndex)].id;
    if (! processor.removeGestureMapping (id)) return;
    const auto after = processor.getSlotMappings (slot);
    pushMappingSnapshot (slot, before, after, reason);
    selectedMappingRow = -1;
    setHoveredMapping ({});
    refreshData (true);
}

int ParameterInspector::rangeMappingIndexForParameter (const ParameterDescriptor& descriptor) const
{
    if (juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()))
        && mappingTargetsParameter (mappings[static_cast<size_t> (selectedMappingRow)], descriptor))
        return selectedMappingRow;
    for (int i = 0; i < static_cast<int> (mappings.size()); ++i)
        if (mappingTargetsParameter (mappings[static_cast<size_t> (i)], descriptor)) return i;
    return -1;
}

void ParameterInspector::updateRangeBindingLive (const juce::Uuid& id, float value, bool minimumHandle)
{
    for (int i = 0; i < static_cast<int> (mappings.size()); ++i)
    {
        auto binding = mappings[static_cast<size_t> (i)];
        if (binding.id != id || binding.targetType != MappingTargetType::childParameter) continue;
        if (minimumHandle)
            binding.minValue = juce::jlimit (0.0f, binding.maxValue, value);
        else
            binding.maxValue = juce::jlimit (binding.minValue, 1.0f, value);
        juce::String error;
        if (processor.updateGestureMapping (binding, error))
        {
            mappings[static_cast<size_t> (i)] = binding;
            selectedMappingRow = i;
            parameterList.repaint();
            if (advancedExpanded) loadSelectedMappingControls();
        }
        else if (error.isNotEmpty())
            statusLabel.setText (error, juce::dontSendNotification);
        return;
    }
}

void ParameterInspector::pushMappingSnapshot (int slotIndex,
                                              std::vector<GestureBinding> before,
                                              std::vector<GestureBinding> after,
                                              const juce::String& transactionName)
{
    if (! mappingSnapshotsDiffer (before, after)) return;
    undoManager.beginNewTransaction (transactionName);
    undoManager.perform (new MappingSnapshotAction (processor, slotIndex, std::move (before), std::move (after)));
    undoButton.setEnabled (undoManager.canUndo());
}

bool ParameterInspector::undoLastMapping()
{
    if (! undoManager.canUndo()) return false;
    const auto ok = undoManager.undo();
    refreshData (true);
    undoButton.setEnabled (undoManager.canUndo());
    repaint();
    return ok;
}

bool ParameterInspector::redoLastMapping()
{
    if (! undoManager.canRedo()) return false;
    const auto ok = undoManager.redo();
    refreshData (true);
    undoButton.setEnabled (undoManager.canUndo());
    repaint();
    return ok;
}

void ParameterInspector::timerCallback()
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    refreshData (slot != lastSlot || pluginName != lastPluginName);

    const auto armed = processor.isParameterLearnArmed();
    if (learnUndoTracking && learnWasArmed && ! armed)
    {
        const auto after = processor.getSlotMappings (learnUndoSlot);
        pushMappingSnapshot (learnUndoSlot, std::move (learnUndoBefore), after, "Learn parameter mapping");
        learnUndoBefore.clear();
        learnUndoTracking = false;
        learnUndoSlot = -1;
    }
    learnWasArmed = armed;

    undoButton.setEnabled (undoManager.canUndo());
    parameterList.repaint();
    repaint();
}

void ParameterInspector::refreshData (bool forceRebuild)
{
    const auto slot = processor.getSelectedSlot();
    const auto pluginName = processor.getSlotPluginName (slot);
    const auto slotChanged = slot != lastSlot || pluginName != lastPluginName;
    const auto oldMappingId = juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()))
                            ? mappings[static_cast<size_t> (selectedMappingRow)].id.toString() : juce::String();

    if (slotChanged)
    {
        undoManager.clearUndoHistory();
        learnUndoTracking = false;
        learnUndoBefore.clear();
        learnUndoSlot = -1;
    }

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
            {
                selectedMappingRow = i;
                break;
            }
    }
    else if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
        selectedMappingRow = -1;

    if (forceRebuild && selectedMappingRow >= 0 && advancedExpanded)
        mappingList.selectRow (selectedMappingRow, false, true);
    statusLabel.setText (processor.getMappingStatus(), juce::dontSendNotification);
    updateControlEnablement();
    updateCurvePreview();
}

void ParameterInspector::parameterSelectionChanged (int row)
{
    selectedParameterRow = juce::isPositiveAndBelow (row, static_cast<int> (parameters.size())) ? row : -1;
}

void ParameterInspector::mappingSelectionChanged (int row)
{
    selectedMappingRow = juce::isPositiveAndBelow (row, static_cast<int> (mappings.size())) ? row : -1;
    loadSelectedMappingControls();
    parameterList.repaint();
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
        axisBox.setSelectedId (axisToComboId (binding.sourceAxis), juce::dontSendNotification);
        curveTypeBox.setSelectedId (curveToComboId (binding.curveType), juce::dontSendNotification);
        minSlider.setValue (binding.minValue, juce::dontSendNotification);
        maxSlider.setValue (binding.maxValue, juce::dontSendNotification);
        curveSlider.setValue (binding.curve, juce::dontSendNotification);
        sensitivitySlider.setValue (binding.sensitivity, juce::dontSendNotification);
        smoothingSlider.setValue (binding.smoothingMs, juce::dontSendNotification);
        deadbandSlider.setValue (binding.deadband, juce::dontSendNotification);
        invertButton.setToggleState (binding.inverted, juce::dontSendNotification);
        mappingEnabledButton.setToggleState (binding.enabled, juce::dontSendNotification);
    }
    loadingControls = false;
    updateControlEnablement();
    updateCurvePreview();
    repaint();
}

void ParameterInspector::applySelectedMappingControls()
{
    if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()))) return;
    const auto slot = processor.getSelectedSlot();
    const auto before = processor.getSlotMappings (slot);
    auto binding = mappings[static_cast<size_t> (selectedMappingRow)];
    binding.enabled = mappingEnabledButton.getToggleState();

    if (binding.targetType == MappingTargetType::childParameter)
    {
        auto minValue = static_cast<float> (minSlider.getValue());
        auto maxValue = static_cast<float> (maxSlider.getValue());
        if (minValue > maxValue) std::swap (minValue, maxValue);
        binding.mode = comboIdToMode (behaviorBox.getSelectedId());
        binding.sourceAxis = comboIdToAxis (axisBox.getSelectedId());
        binding.curveType = comboIdToCurve (curveTypeBox.getSelectedId());
        binding.minValue = minValue;
        binding.maxValue = maxValue;
        binding.curve = static_cast<float> (curveSlider.getValue());
        binding.sensitivity = static_cast<float> (sensitivitySlider.getValue());
        binding.smoothingMs = static_cast<float> (smoothingSlider.getValue());
        binding.deadband = static_cast<float> (deadbandSlider.getValue());
        binding.inverted = invertButton.getToggleState();
    }

    juce::String error;
    if (! processor.updateGestureMapping (binding, error))
    {
        statusLabel.setText (error.isNotEmpty() ? error : "UPDATE FAILED", juce::dontSendNotification);
        return;
    }
    mappings[static_cast<size_t> (selectedMappingRow)] = binding;
    const auto after = processor.getSlotMappings (slot);
    pushMappingSnapshot (slot, before, after, "Edit mapping");
    statusLabel.setText ("MAPPING UPDATED", juce::dontSendNotification);
    updateControlEnablement();
    updateCurvePreview();
    parameterList.repaint();
    mappingList.repaint();
    repaint();
}

void ParameterInspector::updateCurvePreview()
{
    if (curvePreview == nullptr) return;
    if (! juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size())))
    {
        curvePreview->clearResponse();
        return;
    }
    const auto& binding = mappings[static_cast<size_t> (selectedMappingRow)];
    const auto continuous = binding.targetType == MappingTargetType::childParameter
                         && binding.mode == MappingMode::absoluteHeight;
    curvePreview->setResponse (binding.sourceAxis, binding.curveType, binding.curve,
                               binding.sensitivity, binding.inverted, continuous && binding.enabled);
}

void ParameterInspector::updateControlEnablement()
{
    const auto mappingSelected = juce::isPositiveAndBelow (selectedMappingRow, static_cast<int> (mappings.size()));
    const auto child = mappingSelected && mappings[static_cast<size_t> (selectedMappingRow)].targetType == MappingTargetType::childParameter;
    const auto continuous = child && mappings[static_cast<size_t> (selectedMappingRow)].mode == MappingMode::absoluteHeight;
    behaviorBox.setEnabled (child);
    axisBox.setEnabled (continuous);
    curveTypeBox.setEnabled (continuous);
    minSlider.setEnabled (child);
    maxSlider.setEnabled (child);
    curveSlider.setEnabled (continuous);
    sensitivitySlider.setEnabled (continuous);
    smoothingSlider.setEnabled (continuous);
    deadbandSlider.setEnabled (continuous);
    invertButton.setEnabled (continuous);
    livePresetButton.setEnabled (continuous);
    smoothPresetButton.setEnabled (continuous);
    mappingEnabledButton.setEnabled (mappingSelected);
    removeMappingButton.setEnabled (mappingSelected);
    if (curvePreview != nullptr) curvePreview->setEnabled (continuous);
}

void ParameterInspector::updateAdvancedVisibility()
{
    mappingList.setVisible (advancedExpanded);
    for (auto* box : { &behaviorBox, &axisBox, &curveTypeBox }) box->setVisible (advancedExpanded);
    for (auto* slider : { &minSlider, &maxSlider, &curveSlider, &sensitivitySlider, &smoothingSlider, &deadbandSlider })
        slider->setVisible (advancedExpanded);
    for (auto* toggle : { &invertButton, &mappingEnabledButton }) toggle->setVisible (advancedExpanded);
    for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton }) button->setVisible (advancedExpanded);
    statusLabel.setVisible (advancedExpanded);
    if (curvePreview != nullptr) curvePreview->setVisible (advancedExpanded);
    moreButton.setToggleState (advancedExpanded, juce::dontSendNotification);
}

bool ParameterInspector::isParameterMappingResolved (const GestureBinding& binding) const
{
    if (binding.targetType != MappingTargetType::childParameter) return true;
    return descriptorForBinding (binding) != nullptr;
}

juce::String ParameterInspector::describeBinding (const GestureBinding& binding) const
{
    const auto target = binding.targetType == MappingTargetType::slotAction ? mappingModeToString (binding.mode) : binding.parameterName;
    auto result = gestureUiLabel (binding.sourceGesture) + "  ->  " + target;
    if (binding.targetType == MappingTargetType::childParameter)
    {
        result += "  [" + mappingModeToString (binding.mode) + "]";
        if (binding.mode == MappingMode::absoluteHeight)
        {
            result += "  " + juce::String (binding.minValue * 100.0f, 0) + "-" + juce::String (binding.maxValue * 100.0f, 0) + "%";
            result += binding.sourceAxis == MappingAxis::horizontal ? "  X" : "  Y";
            result += "  " + mappingCurveTypeToString (binding.curveType).toUpperCase();
            if (std::abs (binding.sensitivity - 1.0f) > 0.001f)
                result += "  " + juce::String (binding.sensitivity, 2) + "x";
        }
        if (binding.inverted) result += "  INV";
    }
    if (! binding.enabled) result += "  OFF";
    return result;
}

void ParameterInspector::paint (juce::Graphics& g)
{
    ui::drawPanel (g, getLocalBounds().toFloat(), true);
    auto header = getLocalBounds().reduced (14).removeFromTop (26);
    auto title = header;
    title.removeFromRight (86);
    ui::drawIcon (g, ui::Icon::sliders, title.removeFromLeft (22).withSizeKeepingCentre (18, 18).toFloat(), ui::text, 1.45f);
    title.removeFromLeft (5);
    ui::drawSectionTitle (g, "PARAMETERS", title);

    if (! columnHeaderBounds.isEmpty())
    {
        const auto columns = layoutRowColumns (parameterList.getWidth(), columnHeaderBounds.getHeight());
        auto offset = juce::Point<int> (parameterList.getX(), columnHeaderBounds.getY());
        g.setColour (ui::textMuted);
        g.setFont (ui::metaFont());
        g.drawText ("PARAMETER", columns.parameter.translated (offset.x, 0).withY (columnHeaderBounds.getY()), juce::Justification::centredLeft);
        g.drawText ("RANGE", columns.range.translated (offset.x, 0).withY (columnHeaderBounds.getY()), juce::Justification::centredLeft);
        g.drawText ("GESTURE", columns.gesture.translated (offset.x, 0).withY (columnHeaderBounds.getY()), juce::Justification::centredLeft);
    }

    if (! learnDropBounds.isEmpty())
    {
        const auto armed = processor.isParameterLearnArmed();
        const auto preview = learnDropPreview != ControlGesture::unknown;
        const auto active = armed || preview;
        auto rect = learnDropBounds.toFloat().reduced (0.5f);
        g.setColour (active ? ui::accent.withAlpha (0.045f) : ui::workspace);
        g.fillRoundedRectangle (rect, 8.0f);
        ui::drawDashedRoundedRect (g, rect, 8.0f, active ? ui::accent : ui::border, 1.0f, 5.0f, 4.0f);
        auto iconArea = learnDropBounds.withSizeKeepingCentre (42, 42).translated (0, -24);
        ui::drawIcon (g, ui::Icon::palm, iconArea.toFloat(), active ? ui::accent : ui::textMuted, 1.65f);
        g.setFont (ui::controlFont());
        g.setColour (active ? ui::accent : ui::textMuted);
        juce::String line1 = "DROP GESTURE";
        juce::String line2;
        if (armed)
        {
            line1 = "MOVE A PLUGIN CONTROL";
            line2 = processor.getParameterLearnStatus();
        }
        else if (preview)
            line2 = gestureUiLabel (learnDropPreview);
        auto textArea = learnDropBounds.reduced (10, 12);
        textArea.setY (learnDropBounds.getCentreY() + 10);
        textArea.setHeight (44);
        g.drawFittedText (line1, textArea.removeFromTop (20), juce::Justification::centred, 1);
        if (line2.isNotEmpty())
        {
            g.setColour (ui::textMuted);
            g.setFont (ui::metaFont());
            g.drawFittedText (line2, textArea, juce::Justification::centred, 2);
        }
    }

    if (advancedExpanded)
    {
        g.setColour (ui::textMuted);
        g.setFont (ui::metaFont());
        if (! mappingList.getBounds().isEmpty())
            g.drawText ("ASSIGNED", mappingList.getX(), mappingList.getY() - 16,
                        mappingList.getWidth(), 14, juce::Justification::centredLeft);

        const auto drawControlLabel = [&] (const juce::String& label, const juce::Component& component)
        {
            if (component.getBounds().isEmpty()) return;
            g.drawText (label, component.getX(), component.getY() - 15,
                        component.getWidth(), 14, juce::Justification::centredLeft);
        };
        drawControlLabel ("BEHAVIOR", behaviorBox);
        drawControlLabel ("AXIS", axisBox);
        drawControlLabel ("CURVE", curveTypeBox);
        drawControlLabel ("SENSITIVITY", sensitivitySlider);
        drawControlLabel ("MIN", minSlider);
        drawControlLabel ("MAX", maxSlider);
        drawControlLabel ("AMOUNT", curveSlider);
        drawControlLabel ("SMOOTH", smoothingSlider);
        drawControlLabel ("JITTER", deadbandSlider);
    }
}

void ParameterInspector::resized()
{
    auto bounds = getLocalBounds().reduced (14);
    auto header = bounds.removeFromTop (26);
    moreButton.setBounds (header.removeFromRight (34));
    header.removeFromRight (6);
    undoButton.setBounds (header.removeFromRight (34));
    bounds.removeFromTop (6);

    columnHeaderBounds = bounds.removeFromTop (20);
    bounds.removeFromTop (3);

    juce::Rectangle<int> advancedArea;
    if (advancedExpanded)
    {
        advancedArea = bounds.removeFromBottom (226);
        bounds.removeFromBottom (10);
    }

    const auto dropWidth = juce::jlimit (168, 224, juce::roundToInt (static_cast<float> (bounds.getWidth()) * 0.19f));
    learnDropBounds = bounds.removeFromRight (dropWidth);
    bounds.removeFromRight (12);
    parameterList.setBounds (bounds);

    if (! advancedExpanded)
    {
        mappingList.setBounds ({});
        behaviorBox.setBounds ({});
        axisBox.setBounds ({});
        curveTypeBox.setBounds ({});
        for (auto* slider : { &minSlider, &maxSlider, &curveSlider, &sensitivitySlider, &smoothingSlider, &deadbandSlider })
            slider->setBounds ({});
        for (auto* toggle : { &invertButton, &mappingEnabledButton }) toggle->setBounds ({});
        for (auto* button : { &removeMappingButton, &livePresetButton, &smoothPresetButton }) button->setBounds ({});
        if (curvePreview != nullptr) curvePreview->setBounds ({});
        statusLabel.setBounds ({});
        return;
    }

    auto left = advancedArea.removeFromLeft (juce::jmax (235, advancedArea.getWidth() * 30 / 100));
    auto status = left.removeFromBottom (18);
    statusLabel.setBounds (status);
    mappingList.setBounds (left.withTrimmedTop (18).withTrimmedBottom (4));

    advancedArea.removeFromLeft (10);
    advancedArea.removeFromTop (18);

    const auto previewWidth = juce::jlimit (118, 150, advancedArea.getWidth() / 4);
    auto previewColumn = advancedArea.removeFromRight (previewWidth);
    if (curvePreview != nullptr)
        curvePreview->setBounds (previewColumn.removeFromTop (132));
    advancedArea.removeFromRight (8);

    constexpr int gap = 6;
    auto row1 = advancedArea.removeFromTop (52);
    const auto row1Width = juce::jmax (70, (row1.getWidth() - gap * 3) / 4);
    behaviorBox.setBounds (row1.removeFromLeft (row1Width)); row1.removeFromLeft (gap);
    axisBox.setBounds (row1.removeFromLeft (row1Width)); row1.removeFromLeft (gap);
    curveTypeBox.setBounds (row1.removeFromLeft (row1Width)); row1.removeFromLeft (gap);
    sensitivitySlider.setBounds (row1);

    advancedArea.removeFromTop (20);
    auto row2 = advancedArea.removeFromTop (52);
    const auto row2Width = juce::jmax (62, (row2.getWidth() - gap * 4) / 5);
    minSlider.setBounds (row2.removeFromLeft (row2Width)); row2.removeFromLeft (gap);
    maxSlider.setBounds (row2.removeFromLeft (row2Width)); row2.removeFromLeft (gap);
    curveSlider.setBounds (row2.removeFromLeft (row2Width)); row2.removeFromLeft (gap);
    smoothingSlider.setBounds (row2.removeFromLeft (row2Width)); row2.removeFromLeft (gap);
    deadbandSlider.setBounds (row2);

    advancedArea.removeFromTop (16);
    auto actions = advancedArea.removeFromTop (30);
    livePresetButton.setBounds (actions.removeFromLeft (86)); actions.removeFromLeft (5);
    smoothPresetButton.setBounds (actions.removeFromLeft (100)); actions.removeFromLeft (8);
    invertButton.setBounds (actions.removeFromLeft (76)); actions.removeFromLeft (5);
    mappingEnabledButton.setBounds (actions.removeFromLeft (52)); actions.removeFromLeft (8);
    removeMappingButton.setBounds (actions.removeFromLeft (82));
}

void ParameterInspector::mouseDown (const juce::MouseEvent& e)
{
    if (! learnDropBounds.contains (e.getPosition())) return;
    if (processor.isParameterLearnArmed())
    {
        processor.cancelParameterLearn();
        learnUndoTracking = false;
        learnUndoBefore.clear();
        learnUndoSlot = -1;
        repaint();
    }
}
}
