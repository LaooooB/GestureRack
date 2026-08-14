#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "DualHandVisionTypes.h"

namespace gr
{
class VisionReceiver final : private juce::Thread
{
public:
    static constexpr int port = 17777;
    static constexpr int controlPort = 17778;
    static constexpr const char* multicastAddress = "239.255.71.77";
    static constexpr const char* controlAddress = "127.0.0.1";

    VisionReceiver();
    ~VisionReceiver() override;

    VisionSnapshot getSnapshot() const;
    DualHandVisionSnapshot getDualHandSnapshot() const;
    bool isConnected() const;

    bool beginHandCalibration();
    bool cancelHandCalibration();
    bool setSwapHandedness (bool shouldSwap);
    bool toggleSwapHandedness();

private:
    void run() override;
    void parsePacket (const juce::String& jsonText);
    bool sendControlCommand (const juce::var& command);

    mutable juce::SpinLock snapshotLock;
    DualHandVisionSnapshot snapshot;
    std::atomic<int64_t> lastPacketReceivedAtMs { 0 };
    juce::DatagramSocket socket { false };
    juce::DatagramSocket commandSocket { false };
};
}
