#include "PluginCategory.h"

namespace
{
using gr::PluginCategory;

bool containsAny (const juce::String& text, std::initializer_list<const char*> terms)
{
    for (auto* term : terms)
        if (text.containsIgnoreCase (term))
            return true;
    return false;
}

bool containsStandaloneEq (const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text, " |/\\_-+.,:;()[]{}", "");
    for (const auto& token : tokens)
        if (token.equalsIgnoreCase ("eq"))
            return true;
    return false;
}

PluginCategory classifyText (const juce::String& text)
{
    const auto lower = text.toLowerCase();

    if (containsAny (lower, { "no effect", "noeffect" }))
        return PluginCategory::noEffect;

    if (containsAny (lower, { "analyzer", "analyser", "spectrum", "oscilloscope",
                              "metering", "lufs", "phase meter", "level meter" }))
        return PluginCategory::analyzer;

    if (containsAny (lower, { "mastering", "maximizer", "maximiser", "master bus",
                              "masterbus" }))
        return PluginCategory::mastering;

    if (containsAny (lower, { "vocal", "voice", "de-esser", "deesser", "de esser",
                              "sibilance", "formant" }))
        return PluginCategory::vocals;

    if (containsAny (lower, { "restoration", "restore", "repair", "denoise", "de-noise",
                              "noise reduction", "declick", "de-click", "decrackle",
                              "de-crackle", "dehum", "de-hum" }))
        return PluginCategory::restoration;

    if (containsAny (lower, { "pitch shift", "pitchshift", "pitch shifter", "pitchshifter",
                              "pitch", "autotune", "auto-tune", "tuning", "harmonizer",
                              "harmoniser" }))
        return PluginCategory::pitchShift;

    if (containsAny (lower, { "spatial", "panner", "panoramic", "stereo width", "stereo image",
                              "imager", "widener", "binaural", "immersive", "surround" }))
        return PluginCategory::spatialPanner;

    if (containsAny (lower, { "reverb", "reverberation", "convolution", "convolver",
                              "room simulator", "hall reverb", "plate reverb" }))
        return PluginCategory::reverb;

    if (containsAny (lower, { "delay", "echo", "tape echo" }))
        return PluginCategory::delay;

    if (containsAny (lower, { "distortion", "distort", "saturation", "saturator", "overdrive",
                              "fuzz", "bitcrush", "bit crush", "waveshaper", "amp simulator",
                              "amplifier" }))
        return PluginCategory::distortion;

    if (containsAny (lower, { "dynamics", "compressor", "compression", "limiter", "expander",
                              "noise gate", "transient shaper", "transient designer",
                              "multiband dynamics" }))
        return PluginCategory::dynamics;

    if (containsStandaloneEq (lower)
        || containsAny (lower, { "equalizer", "equaliser", "parametric equal", "graphic equal" }))
        return PluginCategory::eq;

    if (containsAny (lower, { "filter", "filtering", "wah", "lowpass", "highpass",
                              "bandpass", "notch filter", "comb filter" }))
        return PluginCategory::filter;

    if (containsAny (lower, { "modulation", "chorus", "flanger", "phaser", "tremolo",
                              "vibrato", "rotary", "ring mod", "ringmod" }))
        return PluginCategory::modulation;

    return PluginCategory::other;
}
}

namespace gr
{
const std::array<PluginCategory, pluginCategoryCount>& browserPluginCategories()
{
    static const std::array<PluginCategory, pluginCategoryCount> categories
    {
        PluginCategory::all,
        PluginCategory::noEffect,
        PluginCategory::analyzer,
        PluginCategory::delay,
        PluginCategory::distortion,
        PluginCategory::dynamics,
        PluginCategory::eq,
        PluginCategory::filter,
        PluginCategory::mastering,
        PluginCategory::modulation,
        PluginCategory::other,
        PluginCategory::pitchShift,
        PluginCategory::restoration,
        PluginCategory::reverb,
        PluginCategory::spatialPanner,
        PluginCategory::vocals
    };
    return categories;
}

juce::String pluginCategoryName (PluginCategory category)
{
    switch (category)
    {
        case PluginCategory::all:           return "All";
        case PluginCategory::noEffect:      return "No Effect";
        case PluginCategory::analyzer:      return "Analyzer";
        case PluginCategory::delay:         return "Delay";
        case PluginCategory::distortion:    return "Distortion";
        case PluginCategory::dynamics:      return "Dynamics";
        case PluginCategory::eq:            return "EQ";
        case PluginCategory::filter:        return "Filter";
        case PluginCategory::mastering:     return "Mastering";
        case PluginCategory::modulation:    return "Modulation";
        case PluginCategory::other:         return "Other";
        case PluginCategory::pitchShift:    return "Pitch Shift";
        case PluginCategory::restoration:   return "Restoration";
        case PluginCategory::reverb:        return "Reverb";
        case PluginCategory::spatialPanner: return "Spatial + Panner";
        case PluginCategory::vocals:        return "Vocals";
    }
    return "Other";
}

bool pluginIsRackEffect (const juce::PluginDescription& plugin)
{
    return ! plugin.name.containsIgnoreCase ("Gesture Rack")
        && ! plugin.isInstrument
        && plugin.numInputChannels > 0;
}

PluginCategory classifyPlugin (const juce::PluginDescription& plugin)
{
    if (! pluginIsRackEffect (plugin))
        return PluginCategory::noEffect;

    const auto official = classifyText (plugin.category);
    if (official != PluginCategory::other || plugin.category.containsIgnoreCase ("other"))
        return official;

    const auto combined = plugin.name + " " + plugin.manufacturerName + " "
                        + plugin.fileOrIdentifier;
    return classifyText (combined);
}

juce::String normalizedPluginCategoryName (const juce::PluginDescription& plugin)
{
    return pluginCategoryName (classifyPlugin (plugin));
}
}
