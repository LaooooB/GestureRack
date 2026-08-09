#include "PluginProcessor.h"
#include "PluginEditor.h"

class GestureRackAudioProcessor::ChildEditorWindow final : public juce::DocumentWindow
{
public:
    explicit ChildEditorWindow (juce::AudioProcessorEditor* editor)
        : juce::DocumentWindow ("Hosted Plugin",
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
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    juce::addDefaultFormatsToManager (formatManager);
    initialiseGraph();
    startTimerHz (50);
}

GestureRackAudioProcessor::~GestureRackAudioProcessor()
{
    stopTimer();
    childEditorWindow.reset();
    graph.clear();
}

void GestureRackAudioProcessor::initialiseGraph()
{
    graph.clear();

    inputNode = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                              (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    outputNode = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                               (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    connectDirect();
}

void GestureRackAudioProcessor::connectDirect()
{
    if (inputNode == nullptr || outputNode == nullptr)
        return;

    graph.disconnectNode (inputNode->nodeID);
    graph.disconnectNode (outputNode->nodeID);

    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    for (int ch = 0; ch < channels; ++ch)
        graph.addConnection ({ { inputNode->nodeID, ch }, { outputNode->nodeID, ch } });
}

void GestureRackAudioProcessor::connectThroughChild()
{
    if (inputNode == nullptr || outputNode == nullptr || childNode == nullptr)
    {
        connectDirect();
        return;
    }

    graph.disconnectNode (inputNode->nodeID);
    graph.disconnectNode (outputNode->nodeID);
    graph.disconnectNode (childNode->nodeID);

    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    for (int ch = 0; ch < channels; ++ch)
    {
        graph.addConnection ({ { inputNode->nodeID, ch }, { childNode->nodeID, ch } });
        graph.addConnection ({ { childNode->nodeID, ch }, { outputNode->nodeID, ch } });
    }
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

    updateLatencyFromChild();
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
                                       gr::GestureBypassWrapper::maxCompensatedLatencySamples - 1,
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

    if (gestureEnabled.load() && vision.isConnected())
    {
        if (snapshot.stableGesture != gr::Gesture::unknown
            && snapshot.stableGesture != lastAppliedGesture)
        {
            lastAppliedGesture = snapshot.stableGesture;

            if (snapshot.stableGesture == gr::Gesture::openPalm)
                requestedBypass.store (false);
            else if (snapshot.stableGesture == gr::Gesture::closedFist)
                requestedBypass.store (true);
        }
    }

    updateLatencyFromChild();
}

void GestureRackAudioProcessor::loadVst3FromFile (const juce::File& file)
{
    lastError.clear();

    if (! file.exists())
    {
        lastError = "Plugin file does not exist.";
        return;
    }

    if (file.getFileName().containsIgnoreCase ("Gesture Rack"))
    {
        lastError = "Gesture Rack cannot host itself.";
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
        lastError = "No loadable VST3 type was found in that file/bundle.";
        return;
    }

    loadDescriptionAsync (*found[0], nullptr);
}

void GestureRackAudioProcessor::loadDescriptionAsync (juce::PluginDescription description,
                                                       std::shared_ptr<juce::MemoryBlock> restoredState)
{
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto blockSize = getBlockSize() > 0 ? getBlockSize() : 512;

    formatManager.createPluginInstanceAsync (
        description, sampleRate, blockSize,
        [this, description, restoredState] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                            const juce::String& error)
        {
            if (instance == nullptr)
            {
                lastError = error.isNotEmpty() ? error : "Could not create the hosted plugin.";
                return;
            }

            installChild (std::move (instance), description,
                          restoredState != nullptr ? restoredState.get() : nullptr);
        });
}

void GestureRackAudioProcessor::installChild (std::unique_ptr<juce::AudioPluginInstance> instance,
                                               const juce::PluginDescription& description,
                                               const juce::MemoryBlock* restoredState)
{
    childEditorWindow.reset();

    if (description.isInstrument || description.numInputChannels <= 0)
    {
        lastError = "This build hosts audio effects, not instruments.";
        return;
    }

    const auto layout = getBusesLayout();
    if (! instance->setBusesLayout (layout))
    {
        lastError = "The hosted plugin does not support the current mono/stereo bus layout.";
        return;
    }

    instance->setPlayHead (getPlayHead());

    if (restoredState != nullptr && restoredState->getSize() > 0)
        instance->setStateInformation (restoredState->getData(), static_cast<int> (restoredState->getSize()));

    if (childNode != nullptr)
    {
        graph.removeNode (childNode->nodeID);
        childNode = nullptr;
    }

    auto wrapper = std::make_unique<gr::GestureBypassWrapper> (
        std::move (instance),
        layout.getMainInputChannelSet(),
        layout.getMainOutputChannelSet(),
        requestedBypass);

    childNode = graph.addNode (std::move (wrapper));
    loadedDescription = description;

    connectThroughChild();
    updateLatencyFromChild();
    lastError.clear();
}

gr::GestureBypassWrapper* GestureRackAudioProcessor::getWrapper() const noexcept
{
    if (childNode == nullptr)
        return nullptr;
    return dynamic_cast<gr::GestureBypassWrapper*> (childNode->getProcessor());
}

juce::AudioPluginInstance* GestureRackAudioProcessor::getChild() const noexcept
{
    if (auto* wrapper = getWrapper())
        return wrapper->getChild();
    return nullptr;
}

void GestureRackAudioProcessor::openChildEditor()
{
    auto* child = getChild();
    if (child == nullptr || ! child->hasEditor())
    {
        lastError = "The hosted plugin has no editor.";
        return;
    }

    if (childEditorWindow != nullptr)
    {
        childEditorWindow->setVisible (true);
        childEditorWindow->toFront (true);
        return;
    }

    if (auto* editor = child->createEditorIfNeeded())
        childEditorWindow = std::make_unique<ChildEditorWindow> (editor);
}

void GestureRackAudioProcessor::updateLatencyFromChild()
{
    auto newLatency = 0;
    if (auto* wrapper = getWrapper())
        newLatency = juce::jlimit (0,
                                  gr::GestureBypassWrapper::maxCompensatedLatencySamples - 1,
                                  wrapper->getChildLatencySamples());

    if (newLatency != getLatencySamples())
        setLatencySamples (newLatency);
}

juce::String GestureRackAudioProcessor::getLoadedPluginName() const
{
    return loadedDescription.has_value() ? loadedDescription->name : "NO PLUGIN";
}

juce::String GestureRackAudioProcessor::getLastError() const
{
    return lastError;
}

double GestureRackAudioProcessor::getTailLengthSeconds() const
{
    if (auto* child = getChild())
        return child->getTailLengthSeconds();
    return 0.0;
}

void GestureRackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("GESTURE_RACK_STATE");
    root.setAttribute ("gestureEnabled", gestureEnabled.load());
    root.setAttribute ("requestedBypass", requestedBypass.load());

    if (loadedDescription.has_value())
    {
        if (auto pluginXml = loadedDescription->createXml())
        {
            pluginXml->setTagName ("PLUGIN_DESCRIPTION");
            root.addChildElement (pluginXml.release());
        }
    }

    if (auto* child = getChild())
    {
        juce::MemoryBlock state;
        child->getStateInformation (state);
        root.createNewChildElement ("PLUGIN_STATE")->addTextElement (state.toBase64Encoding());
    }

    copyXmlToBinary (root, destData);
}

void GestureRackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("GESTURE_RACK_STATE"))
        return;

    gestureEnabled.store (xml->getBoolAttribute ("gestureEnabled", true));
    requestedBypass.store (xml->getBoolAttribute ("requestedBypass", false));

    auto* descXml = xml->getChildByName ("PLUGIN_DESCRIPTION");
    if (descXml == nullptr)
        return;

    // PluginDescription expects its original tag name. The attributes are what matter,
    // so clone and restore the conventional tag used by createXml().
    auto descClone = std::make_unique<juce::XmlElement> (*descXml);
    descClone->setTagName ("PLUGIN");

    juce::PluginDescription desc;
    if (! desc.loadFromXml (*descClone))
        return;

    auto restoredState = std::make_shared<juce::MemoryBlock>();
    if (auto* stateXml = xml->getChildByName ("PLUGIN_STATE"))
        restoredState->fromBase64Encoding (stateXml->getAllSubText());

    juce::MessageManager::callAsync ([this, desc, restoredState]
    {
        loadDescriptionAsync (desc, restoredState);
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
