#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <cstdint>
#include "VisionReceiver.h"
#include "PluginSlot.h"
#include "RackGraphManager.h"

class GestureRackAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Timer
{
public:
    static constexpr int slotCount = gr::RackGraphManager::slotCount;

    GestureRackAudioProcessor();
    ~GestureRackAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Rack slot API. UI and the later gesture-mapping layer both use these methods.
    void loadVst3FromFile (int slotIndex, const juce::File& file);
    void loadVst3FromFile (const juce::File& file) { loadVst3FromFile (getSelectedSlot(), file); }
    void removeSlotPlugin (int slotIndex);
    void openChildEditor (int slotIndex);
    void openChildEditor() { openChildEditor (getSelectedSlot()); }

    int getSelectedSlot() const noexcept { return selectedSlot.load (std::memory_order_relaxed); }
    void setSelectedSlot (int slotIndex) noexcept;

    bool isSlotLoaded (int slotIndex) const noexcept;
    bool isSlotBypassed (int slotIndex) const noexcept;
    void setSlotBypassed (int slotIndex, bool shouldBypass) noexcept;
    juce::String getSlotPluginName (int slotIndex) const;
    juce::String getSlotLastError (int slotIndex) const;

    // Compatibility helpers used by the existing editor while Phase A is being integrated.
    juce::String getLoadedPluginName() const { return getSlotPluginName (getSelectedSlot()); }
    juce::String getLastError() const { return getSlotLastError (getSelectedSlot()); }

    gr::VisionSnapshot getVisionSnapshot() const { return vision.getSnapshot(); }
    bool isVisionConnected() const { return vision.isConnected(); }
    bool isGestureEnabled() const noexcept { return gestureEnabled.load (std::memory_order_relaxed); }
    void setGestureEnabled (bool enabled) noexcept { gestureEnabled.store (enabled, std::memory_order_relaxed); }
    bool isGestureBypassed() const noexcept { return isSlotBypassed (getSelectedSlot()); }

private:
    class ChildEditorWindow;

    static bool isValidSlotIndex (int slotIndex) noexcept
    {
        return slotIndex >= 0 && slotIndex < slotCount;
    }

    void timerCallback() override;

    void loadDescriptionAsync (int slotIndex,
                               juce::PluginDescription description,
                               std::shared_ptr<juce::MemoryBlock> restoredState);
    void installChild (int slotIndex,
                       uint64_t loadGeneration,
                       std::unique_ptr<juce::AudioPluginInstance> instance,
                       const juce::PluginDescription& description,
                       const juce::MemoryBlock* restoredState);

    void clearRackForStateRestore();
    void restoreSlotFromXml (const juce::XmlElement& slotXml);
    void restoreLegacySingleSlotState (const juce::XmlElement& root);
    void updateTotalLatency();

    gr::VisionReceiver vision;
    std::atomic<bool> gestureEnabled { true };
    std::atomic<int> selectedSlot { 0 };
    gr::Gesture lastAppliedGesture = gr::Gesture::unknown;

    juce::AudioProcessorGraph graph;
    gr::RackGraphManager::SlotArray slots;
    gr::RackGraphManager graphManager;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::array<std::unique_ptr<ChildEditorWindow>, slotCount> childEditorWindows;
    std::array<std::atomic<uint64_t>, slotCount> slotLoadGenerations {};
    std::atomic<uint64_t> stateRestoreGeneration { 0 };

    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>> (true) };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> hostBypassDelay
        { gr::RackGraphManager::maxRackLatencySamples };
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessor)
};
