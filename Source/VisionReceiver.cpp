#include "VisionReceiver.h"

namespace gr
{
VisionReceiver::VisionReceiver()
    : juce::Thread ("Gesture Rack Vision Receiver")
{
    socket.setEnablePortReuse (true);

    if (socket.bindToPort (port))
    {
        socket.joinMulticast (multicastAddress);
        startThread (juce::Thread::Priority::normal);
    }
}

VisionReceiver::~VisionReceiver()
{
    signalThreadShouldExit();
    socket.shutdown();
    stopThread (1500);
}

VisionSnapshot VisionReceiver::getSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    return snapshot;
}

bool VisionReceiver::isConnected() const
{
    const auto s = getSnapshot();
    const auto now = juce::Time::currentTimeMillis();
    return s.receivedAtMs > 0 && (now - s.receivedAtMs) < 1500;
}

void VisionReceiver::run()
{
    std::array<char, 8192> buffer {};

    while (! threadShouldExit())
    {
        const auto ready = socket.waitUntilReady (true, 250);
        if (ready <= 0)
            continue;

        const auto bytes = socket.read (buffer.data(), static_cast<int> (buffer.size() - 1), false);
        if (bytes <= 0)
            continue;

        buffer[static_cast<size_t> (bytes)] = '\0';
        parsePacket (juce::String::fromUTF8 (buffer.data(), bytes));
    }
}

void VisionReceiver::parsePacket (const juce::String& jsonText)
{
    const auto parsed = juce::JSON::parse (jsonText);
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return;

    VisionSnapshot next;
    next.protocol = static_cast<int> (object->getProperty ("protocol"));
    if (next.protocol != 1)
        return;

    next.sequence = static_cast<int64_t> (object->getProperty ("seq"));
    next.timestampMs = static_cast<int64_t> (object->getProperty ("timestamp_ms"));
    next.receivedAtMs = juce::Time::currentTimeMillis();
    next.handPresent = static_cast<bool> (object->getProperty ("hand"));
    next.rawGesture = gestureFromString (object->getProperty ("raw").toString());
    next.stableGesture = gestureFromString (object->getProperty ("stable").toString());
    next.confidence = static_cast<float> (object->getProperty ("confidence"));

    if (const auto landmarksVar = object->getProperty ("landmarks"); landmarksVar.isArray())
    {
        if (const auto* landmarks = landmarksVar.getArray())
        {
            const auto count = juce::jmin (21, landmarks->size());
            for (int i = 0; i < count; ++i)
            {
                const auto& pointVar = landmarks->getReference (i);
                if (const auto* point = pointVar.getArray(); point != nullptr && point->size() >= 2)
                {
                    next.landmarks[static_cast<size_t> (i)].x = static_cast<float> (point->getReference (0));
                    next.landmarks[static_cast<size_t> (i)].y = static_cast<float> (point->getReference (1));
                    if (point->size() >= 3)
                        next.landmarks[static_cast<size_t> (i)].z = static_cast<float> (point->getReference (2));
                }
            }
        }
    }

    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    snapshot = next;
}
}
