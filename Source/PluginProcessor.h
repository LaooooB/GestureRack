#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <cstdint>
#include <vector>
#include "VisionReceiver.h"
#include "PluginSlot.h"
#include "RackGraphManager.h"
#include "GestureMappingEngine.h"
#include "RightGestureRuntime.h"
#include "ParameterLearnManager.h"

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
    int getSlotMappingCount (int slotIndex) const;

    juce::String getLoadedPluginName() const { return getSlotPluginName (getSelectedSlot()); }
    juce::String getLastError() const { return getSlotLastError (getSelectedSlot()); }

    gr::VisionSnapshot getVisionSnapshot() const { return vision.getSnapshot(); }
    gr::DualHandVisionSnapshot getDualHandVisionSnapshot() const { return vision.getDualHandSnapshot(); }
    bool isVisionConnected() const { return vision.isConnected(); }
    bool isGestureEnabled() const noexcept { return gestureEnabled.load (std::memory_order_relaxed); }
    void setGestureEnabled (bool enabled) noexcept;
    bool isGestureBypassed() const noexcept { return isSlotBypassed (getSelectedSlot()); }

    bool isRightControllerArmed() const noexcept { return rightRuntime.isArmed(); }
    gr::ControlGesture getLiveRightGesture() const noexcept { return rightRuntime.getCurrentGesture(); }
    int getLiveMappingCount() const;
    juce::String getGestureRuntimeStatus() const;

    std::vector<gr::ParameterDescriptor> getSlotParameters (int slotIndex) const;
    std::vector<gr::GestureBinding> getSlotMappings (int slotIndex) const;

    bool addParameterGestureMapping (int parameterIndex,
                                     gr::ControlGesture gesture,
                                     juce::String& error);
    bool addSlotActionGestureMapping (gr::ControlGesture gesture,
                                      gr::MappingMode mode,
                                      juce::String& error);
    bool updateGestureMapping (const gr::GestureBinding& binding, juce::String& error);
    bool removeGestureMapping (const juce::Uuid& id);

    void setTestGesture (gr::ControlGesture gesture) noexcept
    {
        testGesture.store (static_cast<int> (gesture), std::memory_order_relaxed);
    }
    gr::ControlGesture getTestGesture() const noexcept
    {
        return static_cast<gr::ControlGesture> (testGesture.load (std::memory_order_relaxed));
    }
    void setTestHeight (float value) noexcept
    {
        testHeight.store (juce::jlimit (0.0f, 1.0f, value), std::memory_order_relaxed);
    }
    float getTestHeight() const noexcept { return testHeight.load (std::memory_order_relaxed); }
    void setTestSignalEnabled (bool enabled) noexcept
    {
        testSignalEnabled.store (enabled, std::memory_order_relaxed);
    }
    bool isTestSignalEnabled() const noexcept
    {
        return testSignalEnabled.load (std::memory_order_relaxed);
    }
    void triggerTestGestureEntered();

    bool beginParameterLearn (gr::ControlGesture gesture, juce::String& error);
    void cancelParameterLearn();
    bool isParameterLearnArmed() const noexcept { return parameterLearnManager.isArmed(); }
    juce::String getParameterLearnStatus() const { return parameterLearnManager.getStatusText(); }
    juce::String getMappingStatus() const { return mappingStatus; }

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
    void restoreSlotFromXml (const juce::XmlElement& slotXml, int stateVersion);
    void restoreLegacySingleSlotState (const juce::XmlElement& root);
    void installDefaultMappingsForSlot (int slotIndex);
    void installDefaultMappingsForAllSlots();
    void updateTotalLatency();
    void updateMappingStatus (const juce::String& text);

    gr::VisionReceiver vision;
    std::atomic<bool> gestureEnabled { true };
    std::atomic<int> selectedSlot { 0 };
    gr::RightGestureRuntime rightRuntime;
    int lastVisionStableSlot = 0;
    int64_t lastVisionSequence = 0;

    juce::AudioProcessorGraph graph;
    gr::RackGraphManager::SlotArray slots;
    gr::RackGraphManager graphManager;
    gr::GestureMappingEngine mappingEngine;
    gr::ParameterLearnManager parameterLearnManager;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::array<std::unique_ptr<ChildEditorWindow>, slotCount> childEditorWindows;
    std::array<std::atomic<uint64_t>, slotCount> slotLoadGenerations {};
    std::atomic<uint64_t> stateRestoreGeneration { 0 };

    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>> (true) };

    std::atomic<int> testGesture { static_cast<int> (gr::ControlGesture::victory) };
    std::atomic<float> testHeight { 0.5f };
    std::atomic<bool> testSignalEnabled { false };
    juce::String mappingStatus { "MAPPING READY" };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> hostBypassDelay
        { gr::RackGraphManager::maxRackLatencySamples };
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessor)
};
