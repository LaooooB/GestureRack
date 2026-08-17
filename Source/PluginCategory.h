#pragma once

#include <JuceHeader.h>
#include <array>

namespace gr
{
enum class PluginCategory : int
{
    all = 0,
    noEffect,
    analyzer,
    delay,
    distortion,
    dynamics,
    eq,
    filter,
    mastering,
    modulation,
    other,
    pitchShift,
    restoration,
    reverb,
    spatialPanner,
    vocals
};

constexpr int pluginCategoryCount = 16;

const std::array<PluginCategory, pluginCategoryCount>& browserPluginCategories();
juce::String pluginCategoryName (PluginCategory category);
PluginCategory classifyPlugin (const juce::PluginDescription& plugin);
juce::String normalizedPluginCategoryName (const juce::PluginDescription& plugin);
bool pluginIsRackEffect (const juce::PluginDescription& plugin);
}
