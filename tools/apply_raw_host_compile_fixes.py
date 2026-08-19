from pathlib import Path

root = Path(__file__).resolve().parents[1]

def replace_once(relative_path: str, old: str, new: str) -> None:
    path = root / relative_path
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{relative_path}: expected one match, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')

# Reorder must rebuild the same bus-aware graph used everywhere else.
replace_once(
    'Source/RackInteraction.cpp',
    '''    graphManager.rebuildSerialConnections (\n        juce::jmin (\n            getMainBusNumInputChannels(),\n            getMainBusNumOutputChannels()));''',
    '''    graphManager.rebuildRouting (buildHostBusLayout());''')

# JUCE 8's active-editor API is explicit; keep editor ownership in PluginSlot while
# making the child's active-editor state visible to the hosted plug-in.
replace_once(
    'Source/PluginSlot.cpp',
    'embeddedEditor.reset (child->createEditorIfNeeded());',
    'embeddedEditor.reset (child->createEditorAndMakeActive());')

# MIDI follows rack order. Non-MIDI-producing audio effects see the current event
# stream without breaking it; MIDI-producing processors become the source for the
# next slot. This avoids both the old broadcast-only semantics and duplicate output.
replace_once(
    'Source/RackGraphManager.cpp',
    '''    constexpr auto midiChannel = juce::AudioProcessorGraph::midiChannelIndex;\n    if (midiInputNode != nullptr && midiOutputNode != nullptr)\n        graph.addConnection (\n            { { midiInputNode->nodeID, midiChannel },\n              { midiOutputNode->nodeID, midiChannel } },\n            juce::AudioProcessorGraph::UpdateKind::none);\n\n    for (const auto& slot : slots)''',
    '''    constexpr auto midiChannel = juce::AudioProcessorGraph::midiChannelIndex;\n    Endpoint currentMidiSource {};\n    bool hasMidiSource = midiInputNode != nullptr;\n    if (hasMidiSource)\n        currentMidiSource = { midiInputNode->nodeID, midiChannel };\n\n    for (const auto& slot : slots)''')

replace_once(
    'Source/RackGraphManager.cpp',
    '''        if (midiInputNode != nullptr && child->acceptsMidi())\n            graph.addConnection (\n                { { midiInputNode->nodeID, midiChannel },\n                  { node->nodeID, midiChannel } },\n                juce::AudioProcessorGraph::UpdateKind::none);\n\n        if (midiOutputNode != nullptr && child->producesMidi())\n            graph.addConnection (\n                { { node->nodeID, midiChannel },\n                  { midiOutputNode->nodeID, midiChannel } },\n                juce::AudioProcessorGraph::UpdateKind::none);\n    }\n\n    std::vector<int> mainDestinations;''',
    '''        if (hasMidiSource && child->acceptsMidi())\n            graph.addConnection (\n                { currentMidiSource, { node->nodeID, midiChannel } },\n                juce::AudioProcessorGraph::UpdateKind::none);\n\n        if (child->producesMidi())\n        {\n            currentMidiSource = { node->nodeID, midiChannel };\n            hasMidiSource = true;\n        }\n    }\n\n    if (midiOutputNode != nullptr && hasMidiSource)\n        graph.addConnection (\n            { currentMidiSource, { midiOutputNode->nodeID, midiChannel } },\n            juce::AudioProcessorGraph::UpdateKind::none);\n\n    std::vector<int> mainDestinations;''')

# The graph itself supports mixed single/double child processors. Advertise 64-bit
# processing at the outer VST3 and let JUCE convert around children that only support
# single precision instead of forcing the whole rack to float.
replace_once(
    'Source/PluginProcessor.h',
    '''    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;\n    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;''',
    '''    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;\n    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;\n    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;\n    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;\n    bool supportsDoublePrecisionProcessing() const override { return true; }''')

replace_once(
    'Source/PluginProcessor.cpp',
    '''    graph.setPlayHead (getPlayHead());\n    graph.setNonRealtime (isNonRealtime());\n    graphManager.rebuildRouting (buildHostBusLayout());''',
    '''    graph.setPlayHead (getPlayHead());\n    graph.setProcessingPrecision (getProcessingPrecision());\n    graph.setNonRealtime (isNonRealtime());\n    graphManager.rebuildRouting (buildHostBusLayout());''')

replace_once(
    'Source/PluginProcessor.cpp',
    '''    hostBypassDelay.prepare (spec);\n    hostBypassDelay.reset();\n    updateTotalLatency();''',
    '''    hostBypassDelay.prepare (spec);\n    hostBypassDelay.reset();\n    hostBypassDelayDouble.prepare (spec);\n    hostBypassDelayDouble.reset();\n    updateTotalLatency();''')

replace_once(
    'Source/PluginProcessor.cpp',
    '''    graph.releaseResources();\n    hostBypassDelay.reset();\n}''',
    '''    graph.releaseResources();\n    hostBypassDelay.reset();\n    hostBypassDelayDouble.reset();\n}''')

replace_once(
    'Source/PluginProcessor.cpp',
    '''void GestureRackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)\n{\n    juce::ScopedNoDenormals noDenormals;\n    graph.setPlayHead (getPlayHead());\n    graph.processBlock (buffer, midi);\n}\n\nvoid GestureRackAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)''',
    '''void GestureRackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)\n{\n    juce::ScopedNoDenormals noDenormals;\n    graph.setPlayHead (getPlayHead());\n    graph.processBlock (buffer, midi);\n}\n\nvoid GestureRackAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)\n{\n    juce::ScopedNoDenormals noDenormals;\n    graph.setPlayHead (getPlayHead());\n    graph.processBlock (buffer, midi);\n}\n\nvoid GestureRackAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)''')

replace_once(
    'Source/PluginProcessor.cpp',
    '''void GestureRackAudioProcessor::timerCallback()''',
    '''void GestureRackAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)\n{\n    auto mainOutput = getBusBuffer (buffer, false, 0);\n    const auto latency = juce::jlimit (0, gr::RackGraphManager::maxRackLatencySamples - 1, getLatencySamples());\n    hostBypassDelayDouble.setDelay (static_cast<double> (latency));\n    for (int sample = 0; sample < mainOutput.getNumSamples(); ++sample)\n        for (int channel = 0; channel < mainOutput.getNumChannels(); ++channel)\n        {\n            hostBypassDelayDouble.pushSample (channel, mainOutput.getSample (channel, sample));\n            mainOutput.setSample (channel, sample, hostBypassDelayDouble.popSample (channel));\n        }\n}\n\nvoid GestureRackAudioProcessor::timerCallback()''')

replace_once(
    'Source/PluginProcessor.h',
    '''    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> hostBypassDelay\n        { gr::RackGraphManager::maxRackLatencySamples };\n    bool prepared = false;''',
    '''    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> hostBypassDelay\n        { gr::RackGraphManager::maxRackLatencySamples };\n    juce::dsp::DelayLine<double, juce::dsp::DelayLineInterpolationTypes::Linear> hostBypassDelayDouble\n        { gr::RackGraphManager::maxRackLatencySamples };\n    bool prepared = false;''')

# Keep compiler output clean around the async instance creation path.
replace_once(
    'Source/PluginProcessor.cpp',
    '''    const auto blockSize =\n        getBlockSize() > 0\n            ? getBlockSize()\n            : 512;''',
    '''    const auto instanceBlockSize =\n        getBlockSize() > 0\n            ? getBlockSize()\n            : 512;''')
replace_once(
    'Source/PluginProcessor.cpp',
    '''        sampleRate,\n        blockSize,\n        [this, alive, stableId, generation,''',
    '''        sampleRate,\n        instanceBlockSize,\n        [this, alive, stableId, generation,''')

print('Host compile fixes, serial MIDI, and double precision integration applied.')
