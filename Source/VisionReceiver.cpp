#include "VisionReceiver.h"

namespace gr
{
namespace
{
void parseLandmarks (const juce::var& landmarksVar, std::array<HandPoint, 21>& destination)
{
    if (! landmarksVar.isArray())
        return;

    const auto* landmarks = landmarksVar.getArray();
    if (landmarks == nullptr)
        return;

    const auto count = juce::jmin (21, landmarks->size());
    for (int i = 0; i < count; ++i)
    {
        const auto& pointVar = landmarks->getReference (i);
        const auto* point = pointVar.getArray();
        if (point == nullptr || point->size() < 2)
            continue;

        auto& target = destination[static_cast<size_t> (i)];
        target.x = static_cast<float> (point->getReference (0));
        target.y = static_cast<float> (point->getReference (1));
        if (point->size() >= 3)
            target.z = static_cast<float> (point->getReference (2));
    }
}

void parseHand (juce::DynamicObject* object, HandSnapshot& hand)
{
    if (object == nullptr)
        return;

    hand.present = static_cast<bool> (object->getProperty ("present"));
    hand.handednessConfidence = static_cast<float> (object->getProperty ("handedness_confidence"));
    hand.rawSlot = static_cast<int> (object->getProperty ("raw_slot"));
    hand.stableSlot = static_cast<int> (object->getProperty ("stable_slot"));
    hand.rawGesture = controlGestureFromString (object->getProperty ("raw_gesture").toString());
    hand.stableGesture = controlGestureFromString (object->getProperty ("stable_gesture").toString());
    hand.confidence = static_cast<float> (object->getProperty ("confidence"));
    hand.palmX = static_cast<float> (object->getProperty ("palm_x"));
    hand.palmY = static_cast<float> (object->getProperty ("palm_y"));
    hand.palmZ = static_cast<float> (object->getProperty ("palm_z"));
    hand.height = juce::jlimit (0.0f, 1.0f, static_cast<float> (object->getProperty ("height")));
    parseLandmarks (object->getProperty ("landmarks"), hand.landmarks);
}

void parseLegacyV1 (juce::DynamicObject& object, DualHandVisionSnapshot& next)
{
    next.protocol = 1;
    next.right.present = static_cast<bool> (object.getProperty ("hand"));
    next.right.rawGesture = controlGestureFromString (object.getProperty ("raw").toString());
    next.right.stableGesture = controlGestureFromString (object.getProperty ("stable").toString());
    next.right.confidence = static_cast<float> (object.getProperty ("confidence"));
    parseLandmarks (object.getProperty ("landmarks"), next.right.landmarks);
}
}

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
    const auto dual = getDualHandSnapshot();
    VisionSnapshot legacy;
    legacy.protocol = dual.protocol;
    legacy.sequence = dual.sequence;
    legacy.timestampMs = dual.timestampMs;
    legacy.receivedAtMs = dual.receivedAtMs;
    legacy.handPresent = dual.right.present;
    legacy.confidence = dual.right.confidence;
    legacy.landmarks = dual.right.landmarks;

    if (dual.right.present)
    {
        if (dual.right.rawGesture == ControlGesture::openPalm)
            legacy.rawGesture = Gesture::openPalm;
        else if (dual.right.rawGesture == ControlGesture::closedFist)
            legacy.rawGesture = Gesture::closedFist;

        if (dual.right.stableGesture == ControlGesture::openPalm)
            legacy.stableGesture = Gesture::openPalm;
        else if (dual.right.stableGesture == ControlGesture::closedFist)
            legacy.stableGesture = Gesture::closedFist;
    }

    return legacy;
}

DualHandVisionSnapshot VisionReceiver::getDualHandSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    return snapshot;
}

bool VisionReceiver::isConnected() const
{
    const auto current = getDualHandSnapshot();
    const auto now = juce::Time::currentTimeMillis();
    return current.receivedAtMs > 0 && (now - current.receivedAtMs) < 1500;
}

void VisionReceiver::run()
{
    std::array<char, 16384> buffer {};

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

    const auto protocol = static_cast<int> (object->getProperty ("protocol"));
    if (protocol != 1 && protocol != 2)
        return;

    DualHandVisionSnapshot next;
    next.protocol = protocol;
    next.sessionId = object->getProperty ("session_id").toString();
    next.sequence = static_cast<int64_t> (object->getProperty ("seq"));
    next.timestampMs = static_cast<int64_t> (object->getProperty ("timestamp_ms"));
    next.receivedAtMs = juce::Time::currentTimeMillis();

    if (protocol == 1)
    {
        parseLegacyV1 (*object, next);
    }
    else
    {
        if (const auto leftVar = object->getProperty ("left"); leftVar.isObject())
            parseHand (leftVar.getDynamicObject(), next.left);
        if (const auto rightVar = object->getProperty ("right"); rightVar.isObject())
            parseHand (rightVar.getDynamicObject(), next.right);
    }

    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    snapshot = next;
}
}
