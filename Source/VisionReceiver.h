#pragma once
#include <JuceHeader.h>
#include "GestureTypes.h"

namespace gr
{
class VisionReceiver final : private juce::Thread
{
public:
    static constexpr int port = 17777;
    static constexpr const char* multicastAddress = "239.255.71.77";

    VisionReceiver();
    ~VisionReceiver() override;

    VisionSnapshot getSnapshot() const;
    bool isConnected() const;

private:
    void run() override;
    void parsePacket (const juce::String& jsonText);

    mutable juce::SpinLock snapshotLock;
    VisionSnapshot snapshot;
    juce::DatagramSocket socket { false };
};
}
