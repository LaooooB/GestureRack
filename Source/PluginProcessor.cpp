#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto stateTag = "GESTURE_RACK_STATE";
constexpr auto slotTag = "SLOT";
constexpr auto descriptionTag = "PLUGIN_DESCRIPTION";
constexpr auto pluginStateTag = "PLUGIN_STATE";
constexpr auto mappingsTag = "MAPPINGS";
constexpr int currentStateVersion = 6;
}

GestureRackAudioProcessor::GestureRackAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      graphManager (graph, slots),
      mappingEngine (slots),
      parameterLearnManager (mappingEngine)
{
    for (int i = 0; i < slotCount; ++i)
        slots[static_cast<size_t> (i)] = std::make_unique<gr::PluginSlot> (i);

    installDefaultMappingsForAllSlots();
    juce::addDefaultFormatsToManager (formatManager);
    graphManager.initialise (juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    startTimerHz (100);
}

GestureRackAudioProcessor::~GestureRackAudioProcessor()
{
    aliveFlag->store (false, std::memory_order_release);
    stopTimer();
    parameterLearnManager.cancel();
    mappingEngine.releaseAllActiveGestures();
    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);
    graphManager.clear();
}

void GestureRackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = true;
    hostSampleRate.store (sampleRate, std::memory_order_relaxed);
    hostBlockSize.store (samplesPerBlock, std::memory_order_relaxed);

    graph.setPlayConfigDetails (getTotalNumInputChannels(), getTotalNumOutputChannels(), sampleRate, samplesPerBlock);
    graphManager.rebuildSerialConnections (juce::jmin (getMainBusNumInputChannels(), getMainBusNumOutputChannels()));
    graph.setPlayHead (getPlayHead());
    graph.prepareToPlay (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate,
                                 static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)),
                                 static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels())) };
    hostBypassDelay.prepare (spec);
    hostBypassDelay.reset();
    updateTotalLatency();
}

void GestureRackAudioProcessor::releaseResources()
{
    prepared = false;
    mappingEngine.releaseAllActiveGestures();
    graph.releaseResources();
    hostBypassDelay.reset();
}

bool GestureRackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out || (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()))
        return false;

    const auto sidechain = layouts.getChannelSet (true, 1);
    return sidechain.isDisabled()
        || sidechain == juce::AudioChannelSet::mono()
        || sidechain == juce::AudioChannelSet::stereo();
}

void GestureRackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    graph.setPlayHead (getPlayHead());
    graph.processBlock (buffer, midi);
}

void GestureRackAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto mainOutput = getBusBuffer (buffer, false, 0);
    const auto latency = juce::jlimit (0, gr::RackGraphManager::maxRackLatencySamples - 1, getLatencySamples());
    hostBypassDelay.setDelay (static_cast<float> (latency));
    for (int sample = 0; sample < mainOutput.getNumSamples(); ++sample)
        for (int channel = 0; channel < mainOutput.getNumChannels(); ++channel)
        {
            hostBypassDelay.pushSample (channel, mainOutput.getSample (channel, sample));
            mainOutput.setSample (channel, sample, hostBypassDelay.popSample (channel));
        }
}

void GestureRackAudioProcessor::timerCallback()
{
    constexpr auto controlDeltaSeconds = 1.0f / 100.0f;
    const auto snapshot = vision.getDualHandSnapshot();
    const auto connected = vision.isConnected();
    const auto packetAgeMs = snapshot.receivedAtMs > 0
        ? juce::Time::currentTimeMillis() - snapshot.receivedAtMs : 1000000;
    const auto controlFresh = connected && packetAgeMs >= 0 && packetAgeMs <= 300;

    if (! connected)
    {
        lastVisionStableSlot = 0;
        lastVisionSequence = 0;
        lastVisionSessionId.clear();
        mappingEngine.releaseAllActiveGestures();
        rightRuntime.reset();
    }
    else
    {
        const auto sessionChanged = lastVisionSessionId.isNotEmpty()
                                 && snapshot.sessionId != lastVisionSessionId;
        if (sessionChanged || snapshot.sequence < lastVisionSequence)
        {
            lastVisionStableSlot = 0;
            lastVisionSequence = 0;
            mappingEngine.releaseAllActiveGestures();
            rightRuntime.reset();
        }
        lastVisionSequence = snapshot.sequence;
        lastVisionSessionId = snapshot.sessionId;
    }

    if (connected && ! controlFresh)
    {
        mappingEngine.releaseAllActiveGestures();
        rightRuntime.reset();
    }

    const auto enabled = gestureEnabled.load (std::memory_order_relaxed);
    if (enabled && controlFresh)
    {
        const auto stableSlot = snapshot.left.stableSlot;
        if (snapshot.protocol >= 2 && snapshot.left.present
            && stableSlot >= 1 && stableSlot <= slotCount
            && stableSlot != lastVisionStableSlot)
        {
            lastVisionStableSlot = stableSlot;
            setSelectedSlot (stableSlot - 1);
        }
    }

    const auto liveRightGesture = controlFresh && snapshot.right.present
        ? snapshot.right.stableGesture : gr::ControlGesture::unknown;
    const auto runtimeFrame = rightRuntime.update (controlFresh && snapshot.right.present,
                                                   liveRightGesture);

    const auto testing = testSignalEnabled.load (std::memory_order_relaxed);
    if (enabled && controlFresh && ! testing)
    {
        const auto slotIndex = getSelectedSlot();
        if (runtimeFrame.exited && runtimeFrame.exitedGesture != gr::ControlGesture::unknown)
            mappingEngine.triggerGestureExited (slotIndex, runtimeFrame.exitedGesture);

        if (runtimeFrame.entered && runtimeFrame.gesture != gr::ControlGesture::unknown)
        {
            mappingEngine.triggerGestureEntered (slotIndex, runtimeFrame.gesture);
            updateMappingStatus ("LIVE ENTER: " + gr::controlGestureToString (runtimeFrame.gesture));
        }
        if (runtimeFrame.continuousActive && runtimeFrame.gesture != gr::ControlGesture::unknown)
            mappingEngine.processContinuous (slotIndex, runtimeFrame.gesture,
                                             snapshot.right.palmX,
                                             snapshot.right.height,
                                             controlDeltaSeconds);
    }

    if (testing)
        mappingEngine.processContinuous (getSelectedSlot(), getTestGesture(),
                                         0.5f, getTestHeight(), controlDeltaSeconds);

    if (const auto capture = parameterLearnManager.pollCapture(); capture.has_value())
    {
        juce::String error;
        if (mappingEngine.addParameterBinding (capture->slotIndex, capture->gesture, capture->parameterIndex, error))
        {
            auto name = juce::String ("parameter ") + juce::String (capture->parameterIndex);
            for (const auto& parameter : mappingEngine.enumerateParameters (capture->slotIndex))
                if (parameter.index == capture->parameterIndex)
                    name = parameter.name;
            updateMappingStatus ("LEARNED " + gr::controlGestureToString (capture->gesture) + " -> " + name);
        }
        else
            updateMappingStatus ("LEARN FAILED: " + error);
    }

    updateTotalLatency();
}

void GestureRackAudioProcessor::setSelectedSlot (int slotIndex) noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return;
    const auto previous = selectedSlot.exchange (slotIndex, std::memory_order_relaxed);
    if (previous != slotIndex)
    {
        mappingEngine.releaseAllActiveGestures();
        rightRuntime.disarmForSlotChange();
        testSignalEnabled.store (false, std::memory_order_relaxed);
        parameterLearnManager.cancel();
    }
}

void GestureRackAudioProcessor::setGestureEnabled (bool enabled) noexcept
{
    const auto previous = gestureEnabled.exchange (enabled, std::memory_order_relaxed);
    if (previous != enabled)
    {
        mappingEngine.releaseAllActiveGestures();
        rightRuntime.disarmForSlotChange();
    }
}

bool GestureRackAudioProcessor::isSlotLoaded (int slotIndex) const noexcept
{
    return isValidSlotIndex (slotIndex) && slots[static_cast<size_t> (slotIndex)]->hasPlugin();
}

bool GestureRackAudioProcessor::isSlotBypassed (int slotIndex) const noexcept
{
    return isValidSlotIndex (slotIndex) && slots[static_cast<size_t> (slotIndex)]->isBypassed();
}

void GestureRackAudioProcessor::setSlotBypassed (int slotIndex, bool shouldBypass) noexcept
{
    if (isValidSlotIndex (slotIndex))
        slots[static_cast<size_t> (slotIndex)]->setBypassed (shouldBypass);
}

juce::String GestureRackAudioProcessor::getSlotPluginName (int slotIndex) const
{
    return isValidSlotIndex (slotIndex) ? slots[static_cast<size_t> (slotIndex)]->getPluginName() : "INVALID SLOT";
}

juce::String GestureRackAudioProcessor::getSlotLastError (int slotIndex) const
{
    return isValidSlotIndex (slotIndex) ? slots[static_cast<size_t> (slotIndex)]->getLastError() : "Invalid slot.";
}

int GestureRackAudioProcessor::getSlotMappingCount (int slotIndex) const
{
    return isValidSlotIndex (slotIndex)
        ? static_cast<int> (slots[static_cast<size_t> (slotIndex)]->getMappings().size()) : 0;
}

juce::AudioProcessorEditor* GestureRackAudioProcessor::getOrCreateSlotEditor (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return nullptr;
    if (auto* wrapper = slots[static_cast<size_t> (slotIndex)]->getWrapper())
        return wrapper->getOrCreateEmbeddedEditor();
    return nullptr;
}

bool GestureRackAudioProcessor::slotHasNativeEditor (int slotIndex) const noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return false;
    if (auto* wrapper = slots[static_cast<size_t> (slotIndex)]->getWrapper())
        return wrapper->hasChildEditor();
    return false;
}

uintptr_t GestureRackAudioProcessor::getSlotChildIdentity (int slotIndex) const noexcept
{
    if (! isValidSlotIndex (slotIndex))
        return 0;
    return reinterpret_cast<uintptr_t> (slots[static_cast<size_t> (slotIndex)]->getChild());
}

int GestureRackAudioProcessor::getLiveMappingCount() const
{
    const auto gesture = isTestSignalEnabled() ? getTestGesture()
                                               : (rightRuntime.isArmed() ? rightRuntime.getCurrentGesture()
                                                                         : gr::ControlGesture::unknown);
    if (gesture == gr::ControlGesture::unknown)
        return 0;
    const auto mappings = getSlotMappings (getSelectedSlot());
    return static_cast<int> (std::count_if (mappings.begin(), mappings.end(), [gesture] (const gr::GestureBinding& binding)
    {
        return binding.enabled && binding.sourceGesture == gesture;
    }));
}

juce::String GestureRackAudioProcessor::getGestureRuntimeStatus() const
{
    if (isTestSignalEnabled())
        return "TEST " + gr::controlGestureToString (getTestGesture()).toUpperCase();
    if (! isVisionConnected())
        return "VISION OFFLINE";
    if (! rightRuntime.isArmed())
    {
        const auto blocked = rightRuntime.getBlockedGesture();
        return "RE-ARM: RELEASE / CHANGE "
             + (blocked == gr::ControlGesture::unknown ? juce::String ("GESTURE")
                                                       : gr::controlGestureToString (blocked).toUpperCase());
    }
    const auto gesture = rightRuntime.getCurrentGesture();
    if (gesture == gr::ControlGesture::unknown)
        return "RIGHT READY";
    return "LIVE " + gr::controlGestureToString (gesture).toUpperCase()
         + "  |  " + juce::String (getLiveMappingCount()) + " MAP";
}

std::vector<gr::ParameterDescriptor> GestureRackAudioProcessor::getSlotParameters (int slotIndex) const
{
    return mappingEngine.enumerateParameters (slotIndex);
}

std::vector<gr::GestureBinding> GestureRackAudioProcessor::getSlotMappings (int slotIndex) const
{
    return mappingEngine.getMappings (slotIndex);
}

bool GestureRackAudioProcessor::addParameterGestureMapping (int parameterIndex,
                                                            gr::ControlGesture gesture,
                                                            juce::String& error)
{
    const auto slotIndex = getSelectedSlot();
    const auto ok = mappingEngine.addParameterBinding (slotIndex, gesture, parameterIndex, error);
    if (ok)
    {
        auto name = juce::String ("parameter ") + juce::String (parameterIndex);
        for (const auto& parameter : mappingEngine.enumerateParameters (slotIndex))
            if (parameter.index == parameterIndex)
                name = parameter.name;
        updateMappingStatus (gr::controlGestureToString (gesture) + " -> " + name);
    }
    else
        updateMappingStatus ("MAP FAILED: " + error);
    return ok;
}

bool GestureRackAudioProcessor::addSlotActionGestureMapping (gr::ControlGesture gesture,
                                                             gr::MappingMode mode,
                                                             juce::String& error)
{
    const auto ok = mappingEngine.addSlotActionBinding (getSelectedSlot(), gesture, mode, error);
    updateMappingStatus (ok ? gr::controlGestureToString (gesture) + " -> " + gr::mappingModeToString (mode)
                            : "MAP FAILED: " + error);
    return ok;
}

bool GestureRackAudioProcessor::updateGestureMapping (const gr::GestureBinding& binding, juce::String& error)
{
    const auto ok = mappingEngine.updateBinding (binding, error);
    updateMappingStatus (ok ? "MAPPING UPDATED" : "UPDATE FAILED: " + error);
    return ok;
}

bool GestureRackAudioProcessor::removeGestureMapping (const juce::Uuid& id)
{
    const auto ok = mappingEngine.removeBinding (getSelectedSlot(), id);
    updateMappingStatus (ok ? "MAPPING REMOVED" : "REMOVE FAILED: mapping not found");
    return ok;
}

void GestureRackAudioProcessor::triggerTestGestureEntered()
{
    mappingEngine.triggerGestureEntered (getSelectedSlot(), getTestGesture());
    updateMappingStatus ("TEST ENTER: " + gr::controlGestureToString (getTestGesture()));
}

bool GestureRackAudioProcessor::beginParameterLearn (gr::ControlGesture gesture, juce::String& error)
{
    const auto slotIndex = getSelectedSlot();
    if (! isValidSlotIndex (slotIndex))
    {
        error = "Invalid slot.";
        return false;
    }
    const auto ok = parameterLearnManager.arm (slotIndex, gesture,
                                               slots[static_cast<size_t> (slotIndex)]->getChild(), error);
    updateMappingStatus (ok ? "LEARN ARMED: " + gr::controlGestureToString (gesture)
                            : "LEARN FAILED: " + error);
    return ok;
}

void GestureRackAudioProcessor::cancelParameterLearn()
{
    parameterLearnManager.cancel();
    updateMappingStatus ("LEARN CANCELLED");
}

void GestureRackAudioProcessor::updateMappingStatus (const juce::String& text)
{
    mappingStatus = text;
}

void GestureRackAudioProcessor::installDefaultMappingsForSlot (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;
    juce::String ignored;
    mappingEngine.addSlotActionBinding (slotIndex, gr::ControlGesture::openPalm,
                                        gr::MappingMode::triggerSetActive, ignored);
    ignored.clear();
    mappingEngine.addSlotActionBinding (slotIndex, gr::ControlGesture::closedFist,
                                        gr::MappingMode::triggerSetBypassed, ignored);
}

void GestureRackAudioProcessor::installDefaultMappingsForAllSlots()
{
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        installDefaultMappingsForSlot (slotIndex);
}

juce::File GestureRackAudioProcessor::getPluginCatalogFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("GestureRack").getChildFile ("plugin_catalog.xml");
}

std::optional<juce::PluginDescription> GestureRackAudioProcessor::findCatalogDescriptionForFile (const juce::File& file) const
{
    const auto catalogFile = getPluginCatalogFile();
    if (! catalogFile.existsAsFile())
        return std::nullopt;
    auto xml = juce::XmlDocument::parse (catalogFile);
    if (xml == nullptr)
        return std::nullopt;
    juce::KnownPluginList list;
    list.recreateFromXml (*xml);
    const auto wanted = file.getFullPathName();
    for (const auto& description : list.getTypes())
    {
        const auto candidate = juce::File (description.fileOrIdentifier).getFullPathName();
        if (candidate.equalsIgnoreCase (wanted))
            return description;
    }
    return std::nullopt;
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

    // Never probe unknown third-party binaries inside the DAW process. File drag
    // only resolves descriptions that the isolated scanner has already verified.
    if (const auto description = findCatalogDescriptionForFile (file); description.has_value())
        loadPluginDescription (slotIndex, *description);
    else
        slot.setLastError ("Not in safe plugin catalog. Add its folder in PLUGINS and run SAFE SCAN first.");
}

void GestureRackAudioProcessor::loadDescriptionAsync (int slotIndex,
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
            if (! alive->load (std::memory_order_acquire) || ! isValidSlotIndex (slotIndex))
                return;
            const auto index = static_cast<size_t> (slotIndex);
            if (slotLoadGenerations[index].load (std::memory_order_relaxed) != generation)
                return;
            if (instance == nullptr)
            {
                slots[index]->setLastError (error.isNotEmpty() ? error : "Could not create the hosted plugin.");
                return;
            }
            installChild (slotIndex, generation, std::move (instance), description,
                          restoredState != nullptr ? restoredState.get() : nullptr);
        });
}

void GestureRackAudioProcessor::installChild (int slotIndex,
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
    if (description.isInstrument)
    {
        slot.setLastError ("This build hosts audio effects, not instruments.");
        return;
    }

    const auto rackLayout = getBusesLayout();
    const auto mainInput = rackLayout.getMainInputChannelSet();
    const auto mainOutput = rackLayout.getMainOutputChannelSet();
    auto hostSidechain = juce::AudioChannelSet::disabled();
    if (getBusCount (true) > 1)
        hostSidechain = getChannelLayoutOfBus (true, 1);

    gr::HostedPluginCapabilities capabilities;
    juce::String compatibilityError;
    if (! gr::GestureBypassWrapper::configureChildForHosting (*instance, mainInput, mainOutput,
                                                               hostSidechain, capabilities,
                                                               compatibilityError))
    {
        slot.setLastError ("HOST COMPATIBILITY: " + compatibilityError);
        return;
    }

    instance->setPlayHead (getPlayHead());
    if (restoredState != nullptr && restoredState->getSize() > 0)
        instance->setStateInformation (restoredState->getData(), static_cast<int> (restoredState->getSize()));

    // A rack/meta plug-in can restore bus enablement from its own state. Re-negotiate
    // after state restore so AUX buses never invalidate the main rack path and a usable
    // sidechain is reattached when the outer host exposes one.
    if (! gr::GestureBypassWrapper::configureChildForHosting (*instance, mainInput, mainOutput,
                                                               hostSidechain, capabilities,
                                                               compatibilityError))
    {
        slot.setLastError ("HOST STATE COMPATIBILITY: " + compatibilityError);
        return;
    }
    instance->setPlayHead (getPlayHead());

    parameterLearnManager.cancelIfSlot (slotIndex);
    if (const auto& oldDescription = slot.getDescription(); oldDescription.has_value())
        if (! oldDescription->matchesIdentifierString (description.createIdentifierString()))
            mappingEngine.clearChildParameterMappings (slotIndex);

    auto wrapper = std::make_unique<gr::GestureBypassWrapper> (
        std::move (instance), mainInput, mainOutput, hostSidechain,
        capabilities, slot.getBypassState());
    const auto channels = juce::jmin (getMainBusNumInputChannels(), getMainBusNumOutputChannels());
    if (graphManager.installSlotProcessor (slotIndex, std::move (wrapper), channels) == nullptr)
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
    parameterLearnManager.cancelIfSlot (slotIndex);
    mappingEngine.releaseAllActiveGestures();
    slotLoadGenerations[index].fetch_add (1, std::memory_order_relaxed);
    graphManager.removeSlotProcessor (slotIndex, juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    mappingEngine.clearChildParameterMappings (slotIndex);

    auto& slot = *slots[index];
    slot.clearDescription();
    slot.setBypassed (false);
    slot.clearLastError();
    if (slotIndex == getSelectedSlot())
        testSignalEnabled.store (false, std::memory_order_relaxed);
    updateTotalLatency();
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
    root.setAttribute ("version", currentStateVersion);
    root.setAttribute ("gestureEnabled", gestureEnabled.load (std::memory_order_relaxed));
    root.setAttribute ("selectedSlot", getSelectedSlot());
    root.setAttribute ("rightControllerArmed", rightRuntime.isArmed());
    root.setAttribute ("rightBlockedGesture", gr::controlGestureToString (rightRuntime.getBlockedGesture()));

    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        const auto& slot = *slots[static_cast<size_t> (slotIndex)];
        auto* slotXml = root.createNewChildElement (slotTag);
        slotXml->setAttribute ("index", slotIndex);
        slotXml->setAttribute ("bypassed", slot.isBypassed());

        if (const auto& description = slot.getDescription(); description.has_value())
            if (auto pluginXml = description->createXml())
            {
                pluginXml->setTagName (descriptionTag);
                slotXml->addChildElement (pluginXml.release());
            }

        if (auto* child = slot.getChild())
        {
            juce::MemoryBlock state;
            child->getStateInformation (state);
            slotXml->createNewChildElement (pluginStateTag)->addTextElement (state.toBase64Encoding());
        }

        auto* mappingsXml = slotXml->createNewChildElement (mappingsTag);
        for (const auto& binding : slot.getMappings())
            mappingsXml->addChildElement (binding.toXml().release());
    }
    copyXmlToBinary (root, destData);
}

void GestureRackAudioProcessor::clearRackForStateRestore()
{
    parameterLearnManager.cancel();
    testSignalEnabled.store (false, std::memory_order_relaxed);
    mappingEngine.releaseAllActiveGestures();
    rightRuntime.reset();
    lastVisionStableSlot = 0;
    lastVisionSequence = 0;
    lastVisionSessionId.clear();

    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);
    graphManager.removeAllSlotProcessors (juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        auto& slot = *slots[static_cast<size_t> (slotIndex)];
        slot.clearDescription();
        slot.setBypassed (false);
        slot.clearLastError();
        mappingEngine.clearAllMappings (slotIndex);
    }
    updateTotalLatency();
}

void GestureRackAudioProcessor::restoreSlotFromXml (const juce::XmlElement& slotXml, int stateVersion)
{
    const auto slotIndex = slotXml.getIntAttribute ("index", -1);
    if (! isValidSlotIndex (slotIndex))
        return;
    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    slot.setBypassed (slotXml.getBoolAttribute ("bypassed", false));

    if (stateVersion >= 3)
        if (auto* mappingsXml = slotXml.getChildByName (mappingsTag))
            for (auto* bindingXml = mappingsXml->getFirstChildElement(); bindingXml != nullptr;
                 bindingXml = bindingXml->getNextElement())
                if (auto binding = gr::GestureBinding::fromXml (*bindingXml); binding.has_value())
                {
                    binding->slotIndex = slotIndex;

                    // State v3 still came from the old exclusive-gesture model.
                    // Modern states intentionally support gesture fan-out, so never
                    // delete sibling mappings simply because they share a source.
                    if (stateVersion <= 3)
                        for (const auto& existing : slot.getMappings())
                            if (existing.sourceGesture == binding->sourceGesture)
                                slot.removeMapping (existing.id);

                    slot.addMapping (*binding);
                }

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
        if (! alive->load (std::memory_order_acquire)
            || stateRestoreGeneration.load (std::memory_order_relaxed) != restoreGeneration)
            return;

        clearRackForStateRestore();
        gestureEnabled.store (stateCopy->getBoolAttribute ("gestureEnabled", true), std::memory_order_relaxed);
        setSelectedSlot (juce::jlimit (0, slotCount - 1, stateCopy->getIntAttribute ("selectedSlot", 0)));

        const auto version = stateCopy->getIntAttribute ("version", 1);
        if (version < 2)
        {
            installDefaultMappingsForAllSlots();
            restoreLegacySingleSlotState (*stateCopy);
            rightRuntime.reset();
            return;
        }

        for (auto* slotXml = stateCopy->getFirstChildElement(); slotXml != nullptr; slotXml = slotXml->getNextElement())
            if (slotXml->hasTagName (slotTag))
                restoreSlotFromXml (*slotXml, version);

        if (version >= 2 && version <= 3)
            installDefaultMappingsForAllSlots();

        if (version >= 4)
        {
            const auto armed = stateCopy->getBoolAttribute ("rightControllerArmed", true);
            const auto blocked = gr::controlGestureFromString (
                stateCopy->getStringAttribute ("rightBlockedGesture", "Unknown"));
            rightRuntime.restoreArmingState (armed, blocked);
        }
        else
            rightRuntime.reset();
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
