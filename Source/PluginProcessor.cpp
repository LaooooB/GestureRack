#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto stateTag = "GESTURE_RACK_STATE";
constexpr auto slotTag = "SLOT";
constexpr auto descriptionTag = "PLUGIN_DESCRIPTION";
constexpr auto pluginStateTag = "PLUGIN_STATE";
constexpr int stateVersion = 2;
}

class GestureRackAudioProcessor::ChildEditorWindow final : public juce::DocumentWindow
{
public:
    ChildEditorWindow (int slotIndex, juce::String pluginName, juce::AudioProcessorEditor* editor)
        : juce::DocumentWindow ("Slot " + juce::String (slotIndex + 1) + " - " + pluginName,
                                juce::Colours::black,
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        ownedEditor.reset (editor);
        setContentNonOwned (ownedEditor.get(), true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }

private:
    std::unique_ptr<juce::AudioProcessorEditor> ownedEditor;
};

GestureRackAudioProcessor::GestureRackAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      graphManager (graph, slots)
{
    for (int i = 0; i < slotCount; ++i)
        slots[static_cast<size_t> (i)] = std::make_unique<gr::PluginSlot> (i);

    juce::addDefaultFormatsToManager (formatManager);
    graphManager.initialise (juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    startTimerHz (50);
}

GestureRackAudioProcessor::~GestureRackAudioProcessor()
{
    aliveFlag->store (false, std::memory_order_release);
    stopTimer();

    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);

    for (auto& window : childEditorWindows)
        window.reset();

    graphManager.clear();
}

void GestureRackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = true;

    graph.setPlayConfigDetails (getTotalNumInputChannels(), getTotalNumOutputChannels(), sampleRate, samplesPerBlock);
    graph.setPlayHead (getPlayHead());
    graph.prepareToPlay (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate,
                                 static_cast<juce::uint32> (samplesPerBlock),
                                 static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels())) };
    hostBypassDelay.prepare (spec);
    hostBypassDelay.reset();

    updateTotalLatency();
}

void GestureRackAudioProcessor::releaseResources()
{
    prepared = false;
    graph.releaseResources();
    hostBypassDelay.reset();
}

bool GestureRackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void GestureRackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    graph.setPlayHead (getPlayHead());
    graph.processBlock (buffer, midi);
}

void GestureRackAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto latency = juce::jlimit (0,
                                       gr::RackGraphManager::maxRackLatencySamples - 1,
                                       getLatencySamples());
    hostBypassDelay.setDelay (static_cast<float> (latency));

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            hostBypassDelay.pushSample (channel, buffer.getSample (channel, sample));
            buffer.setSample (channel, sample, hostBypassDelay.popSample (channel));
        }
    }
}

void GestureRackAudioProcessor::timerCallback()
{
    const auto snapshot = vision.getSnapshot();

    if (gestureEnabled.load (std::memory_order_relaxed) && vision.isConnected())
    {
        if (snapshot.stableGesture != gr::Gesture::unknown
            && snapshot.stableGesture != lastAppliedGesture)
        {
            lastAppliedGesture = snapshot.stableGesture;
            const auto slotIndex = getSelectedSlot();

            if (snapshot.stableGesture == gr::Gesture::openPalm)
                setSlotBypassed (slotIndex, false);
            else if (snapshot.stableGesture == gr::Gesture::closedFist)
                setSlotBypassed (slotIndex, true);
        }
    }

    updateTotalLatency();
}

void GestureRackAudioProcessor::setSelectedSlot (int slotIndex) noexcept
{
    if (isValidSlotIndex (slotIndex))
        selectedSlot.store (slotIndex, std::memory_order_relaxed);
}

bool GestureRackAudioProcessor::isSlotLoaded (int slotIndex) const noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return false;

    return slots[static_cast<size_t> (slotIndex)]->hasPlugin();
}

bool GestureRackAudioProcessor::isSlotBypassed (int slotIndex) const noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return false;

    return slots[static_cast<size_t> (slotIndex)]->isBypassed();
}

void GestureRackAudioProcessor::setSlotBypassed (int slotIndex, bool shouldBypass) noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return;

    slots[static_cast<size_t> (slotIndex)]->setBypassed (shouldBypass);
}

juce::String GestureRackAudioProcessor::getSlotPluginName (int slotIndex) const
{
    if (! isValidSlotIndex (slotIndex))
        return "INVALID SLOT";

    return slots[static_cast<size_t> (slotIndex)]->getPluginName();
}

juce::String GestureRackAudioProcessor::getSlotLastError (int slotIndex) const
{
    if (! isValidSlotIndex (slotIndex))
        return "Invalid slot.";

    return slots[static_cast<size_t> (slotIndex)]->getLastError();
}

void GestureRackAudioProcessor::loadVst3FromFile (int slotIndex, const juce::File& file)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    slot.clearLastError();

    if (! file.exists())
    {
        slot.setLastError ("Plugin file does not exist.");
        return;
    }

    if (file.getFileName().containsIgnoreCase ("Gesture Rack"))
    {
        slot.setLastError ("Gesture Rack cannot host itself.");
        return;
    }

    juce::OwnedArray<juce::PluginDescription> found;

    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr || format->getName() != "VST3")
            continue;

        if (! format->fileMightContainThisPluginType (file.getFullPathName()))
            continue;

        knownPlugins.scanAndAddFile (file.getFullPathName(), false, found, *format);
        break;
    }

    if (found.isEmpty())
    {
        slot.setLastError ("No loadable VST3 type was found in that file/bundle.");
        return;
    }

    loadDescriptionAsync (slotIndex, *found[0], nullptr);
}

void GestureRackAudioProcessor::loadDescriptionAsync (
    int slotIndex,
    juce::PluginDescription description,
    std::shared_ptr<juce::MemoryBlock> restoredState)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    const auto index = static_cast<size_t> (slotIndex);
    const auto generation = slotLoadGenerations[index].fetch_add (1, std::memory_order_relaxed) + 1;
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto blockSize = getBlockSize() > 0 ? getBlockSize() : 512;

    auto alive = aliveFlag;
    formatManager.createPluginInstanceAsync (
        description, sampleRate, blockSize,
        [this, alive, slotIndex, generation, description, restoredState]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (! alive->load (std::memory_order_acquire))
                return;

            if (! isValidSlotIndex (slotIndex))
                return;

            const auto index = static_cast<size_t> (slotIndex);
            if (slotLoadGenerations[index].load (std::memory_order_relaxed) != generation)
                return;

            if (instance == nullptr)
            {
                slots[index]->setLastError (
                    error.isNotEmpty() ? error : "Could not create the hosted plugin.");
                return;
            }

            installChild (slotIndex,
                          generation,
                          std::move (instance),
                          description,
                          restoredState != nullptr ? restoredState.get() : nullptr);
        });
}

void GestureRackAudioProcessor::installChild (
    int slotIndex,
    uint64_t loadGeneration,
    std::unique_ptr<juce::AudioPluginInstance> instance,
    const juce::PluginDescription& description,
    const juce::MemoryBlock* restoredState)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    const auto index = static_cast<size_t> (slotIndex);
    auto& slot = *slots[index];

    if (slotLoadGenerations[index].load (std::memory_order_relaxed) != loadGeneration)
        return;

    if (description.isInstrument || description.numInputChannels <= 0)
    {
        slot.setLastError ("This build hosts audio effects, not instruments.");
        return;
    }

    const auto layout = getBusesLayout();
    if (! instance->setBusesLayout (layout))
    {
        slot.setLastError ("The hosted plugin does not support the current mono/stereo bus layout.");
        return;
    }

    instance->setPlayHead (getPlayHead());

    if (restoredState != nullptr && restoredState->getSize() > 0)
        instance->setStateInformation (restoredState->getData(), static_cast<int> (restoredState->getSize()));

    childEditorWindows[index].reset();

    auto wrapper = std::make_unique<gr::GestureBypassWrapper> (
        std::move (instance),
        layout.getMainInputChannelSet(),
        layout.getMainOutputChannelSet(),
        slot.getBypassState());

    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    const auto newNode = graphManager.installSlotProcessor (slotIndex, std::move (wrapper), channels);

    if (newNode == nullptr)
    {
        slot.setLastError ("Could not insert the hosted plugin into the rack graph.");
        return;
    }

    slot.setDescription (description);
    slot.clearLastError();
    updateTotalLatency();
}

void GestureRackAudioProcessor::removeSlotPlugin (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    const auto index = static_cast<size_t> (slotIndex);
    slotLoadGenerations[index].fetch_add (1, std::memory_order_relaxed);
    childEditorWindows[index].reset();

    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    graphManager.removeSlotProcessor (slotIndex, channels);

    auto& slot = *slots[index];
    slot.clearDescription();
    slot.setBypassed (false);
    slot.clearLastError();

    updateTotalLatency();
}

void GestureRackAudioProcessor::openChildEditor (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    const auto index = static_cast<size_t> (slotIndex);
    auto& slot = *slots[index];
    auto* child = slot.getChild();

    if (child == nullptr || ! child->hasEditor())
    {
        slot.setLastError ("The hosted plugin has no editor.");
        return;
    }

    if (childEditorWindows[index] != nullptr)
    {
        childEditorWindows[index]->setVisible (true);
        childEditorWindows[index]->toFront (true);
        return;
    }

    if (auto* editor = child->createEditorIfNeeded())
    {
        childEditorWindows[index] = std::make_unique<ChildEditorWindow> (
            slotIndex, slot.getPluginName(), editor);
        slot.clearLastError();
    }
    else
    {
        slot.setLastError ("The hosted plugin could not create its editor.");
    }
}

void GestureRackAudioProcessor::updateTotalLatency()
{
    const auto newLatency = graphManager.getTotalLatencySamples();
    if (newLatency != getLatencySamples())
        setLatencySamples (newLatency);
}

double GestureRackAudioProcessor::getTailLengthSeconds() const
{
    return graphManager.getTotalTailLengthSeconds();
}

void GestureRackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root (stateTag);
    root.setAttribute ("version", stateVersion);
    root.setAttribute ("gestureEnabled", gestureEnabled.load (std::memory_order_relaxed));
    root.setAttribute ("selectedSlot", getSelectedSlot());

    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        const auto& slot = *slots[static_cast<size_t> (slotIndex)];
        auto* slotXml = root.createNewChildElement (slotTag);
        slotXml->setAttribute ("index", slotIndex);
        slotXml->setAttribute ("bypassed", slot.isBypassed());

        if (const auto& description = slot.getDescription(); description.has_value())
        {
            if (auto pluginXml = description->createXml())
            {
                pluginXml->setTagName (descriptionTag);
                slotXml->addChildElement (pluginXml.release());
            }
        }

        if (auto* child = slot.getChild())
        {
            juce::MemoryBlock state;
            child->getStateInformation (state);
            slotXml->createNewChildElement (pluginStateTag)->addTextElement (state.toBase64Encoding());
        }
    }

    copyXmlToBinary (root, destData);
}

void GestureRackAudioProcessor::clearRackForStateRestore()
{
    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);

    for (auto& window : childEditorWindows)
        window.reset();

    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    graphManager.removeAllSlotProcessors (channels);

    for (auto& slot : slots)
    {
        slot->clearDescription();
        slot->setBypassed (false);
        slot->clearLastError();
    }

    updateTotalLatency();
}

void GestureRackAudioProcessor::restoreSlotFromXml (const juce::XmlElement& slotXml)
{
    const auto slotIndex = slotXml.getIntAttribute ("index", -1);
    if (! isValidSlotIndex (slotIndex))
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    slot.setBypassed (slotXml.getBoolAttribute ("bypassed", false));

    auto* descXml = slotXml.getChildByName (descriptionTag);
    if (descXml == nullptr)
        return;

    auto descClone = std::make_unique<juce::XmlElement> (*descXml);
    descClone->setTagName ("PLUGIN");

    juce::PluginDescription description;
    if (! description.loadFromXml (*descClone))
    {
        slot.setLastError ("Could not restore the saved plugin description.");
        return;
    }

    auto restoredState = std::make_shared<juce::MemoryBlock>();
    if (auto* stateXml = slotXml.getChildByName (pluginStateTag))
        restoredState->fromBase64Encoding (stateXml->getAllSubText());

    loadDescriptionAsync (slotIndex, description, restoredState);
}

void GestureRackAudioProcessor::restoreLegacySingleSlotState (const juce::XmlElement& root)
{
    auto* descXml = root.getChildByName (descriptionTag);
    if (descXml == nullptr)
        return;

    auto descClone = std::make_unique<juce::XmlElement> (*descXml);
    descClone->setTagName ("PLUGIN");

    juce::PluginDescription description;
    if (! description.loadFromXml (*descClone))
        return;

    auto restoredState = std::make_shared<juce::MemoryBlock>();
    if (auto* stateXml = root.getChildByName (pluginStateTag))
        restoredState->fromBase64Encoding (stateXml->getAllSubText());

    slots[0]->setBypassed (root.getBoolAttribute ("requestedBypass", false));
    loadDescriptionAsync (0, description, restoredState);
}

void GestureRackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateTag))
        return;

    auto stateCopy = std::make_shared<juce::XmlElement> (*xml);
    auto alive = aliveFlag;
    const auto restoreGeneration = stateRestoreGeneration.fetch_add (1, std::memory_order_relaxed) + 1;

    juce::MessageManager::callAsync ([this, alive, stateCopy, restoreGeneration]
    {
        if (! alive->load (std::memory_order_acquire))
            return;

        if (stateRestoreGeneration.load (std::memory_order_relaxed) != restoreGeneration)
            return;

        clearRackForStateRestore();

        gestureEnabled.store (stateCopy->getBoolAttribute ("gestureEnabled", true),
                              std::memory_order_relaxed);
        setSelectedSlot (juce::jlimit (0,
                                       slotCount - 1,
                                       stateCopy->getIntAttribute ("selectedSlot", 0)));

        const auto version = stateCopy->getIntAttribute ("version", 1);
        if (version < 2)
        {
            restoreLegacySingleSlotState (*stateCopy);
            return;
        }

        for (auto* slotXml = stateCopy->getFirstChildElement();
             slotXml != nullptr;
             slotXml = slotXml->getNextElement())
        {
            if (slotXml->hasTagName (slotTag))
                restoreSlotFromXml (*slotXml);
        }
    });
}

juce::AudioProcessorEditor* GestureRackAudioProcessor::createEditor()
{
    return new GestureRackAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GestureRackAudioProcessor();
}
