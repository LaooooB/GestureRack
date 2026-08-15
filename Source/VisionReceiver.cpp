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
    const auto rawHeightVar = object->getProperty ("height_raw");
    hand.rawHeight = rawHeightVar.isVoid()
        ? juce::jlimit (0.0f, 1.0f, static_cast<float> (object->getProperty ("height")))
        : juce::jlimit (0.0f, 1.0f, static_cast<float> (rawHeightVar));
    hand.height = juce::jlimit (0.0f, 1.0f, static_cast<float> (object->getProperty ("height")));

    if (const auto shadowVar = object->getProperty ("shadow"); shadowVar.isObject())
    {
        if (auto* shadow = shadowVar.getDynamicObject())
        {
            hand.shadowAvailable = static_cast<bool> (shadow->getProperty ("available"));
            hand.shadowGesture = controlGestureFromString (shadow->getProperty ("gesture").toString());
            hand.shadowConfidence = static_cast<float> (shadow->getProperty ("confidence"));
            hand.shadowMargin = static_cast<float> (shadow->getProperty ("margin"));
            hand.shadowInferenceMs = static_cast<float> (shadow->getProperty ("inference_ms"));
            hand.shadowAgrees = static_cast<bool> (shadow->getProperty ("agrees"));
        }
    }

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

juce::var makeCommand (const juce::String& name)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("command", name);
    return juce::var (object);
}

bool physicalRolesTrusted (const DualHandVisionSnapshot& snapshot)
{
    if (snapshot.protocol < 2)
        return true;
    if (snapshot.handCalibrationActive)
        return false;

    const auto source = snapshot.handRoleSource.trim().toUpperCase();
    return source.isNotEmpty()
        && source != "UNCALIBRATED"
        && source != "CALIBRATING"
        && source != "DEFAULT";
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
    const auto receivedAtMs = lastPacketReceivedAtMs.load (std::memory_order_relaxed);
    const auto now = juce::Time::currentTimeMillis();
    return receivedAtMs > 0 && (now - receivedAtMs) < 1500;
}

bool VisionReceiver::sendControlCommand (const juce::var& command)
{
    const auto payload = juce::JSON::toString (command, true);
    const auto bytes = payload.getNumBytesAsUTF8();
    if (bytes <= 0)
        return false;
    return commandSocket.write (controlAddress, controlPort, payload.toRawUTF8(), bytes) == bytes;
}

bool VisionReceiver::beginHandCalibration()
{
    return sendControlCommand (makeCommand ("begin_hand_calibration"));
}

bool VisionReceiver::cancelHandCalibration()
{
    return sendControlCommand (makeCommand ("cancel_hand_calibration"));
}

bool VisionReceiver::setSwapHandedness (bool shouldSwap)
{
    auto command = makeCommand ("set_swap_handedness");
    if (auto* object = command.getDynamicObject())
        object->setProperty ("value", shouldSwap);
    return sendControlCommand (command);
}

bool VisionReceiver::toggleSwapHandedness()
{
    return sendControlCommand (makeCommand ("toggle_swap_handedness"));
}

void VisionReceiver::run()
{
    std::array<char, 16384> buffer {};

    while (! threadShouldExit())
    {
        const auto ready = socket.waitUntilReady (true, 250);
        if (ready <= 0)
            continue;

        int bytes = 0;
        int lastBytes = 0;
        do
        {
            bytes = socket.read (buffer.data(), static_cast<int> (buffer.size() - 1), false);
            if (bytes <= 0)
                break;
            lastBytes = bytes;
        } while (! threadShouldExit());

        if (lastBytes > 0)
        {
            buffer[static_cast<size_t> (lastBytes)] = '\0';
            parsePacket (juce::String::fromUTF8 (buffer.data(), lastBytes));
        }
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

    const auto wallReceiveMs = juce::Time::currentTimeMillis();

    DualHandVisionSnapshot next;
    next.protocol = protocol;
    next.sequence = static_cast<int64_t> (object->getProperty ("seq"));
    next.timestampMs = static_cast<int64_t> (object->getProperty ("timestamp_ms"));
    next.sessionId = object->getProperty ("session_id").toString();
    next.receivedAtMs = wallReceiveMs;

    if (protocol == 1)
    {
        parseLegacyV1 (*object, next);
    }
    else
    {
        if (const auto telemetryVar = object->getProperty ("telemetry"); telemetryVar.isObject())
        {
            if (auto* t = telemetryVar.getDynamicObject())
            {
                next.captureFps = static_cast<float> (t->getProperty ("capture_fps"));
                next.visionFps = static_cast<float> (t->getProperty ("vision_fps"));
                next.captureToResultMs = static_cast<float> (t->getProperty ("capture_to_result_ms"));
                next.frameAgeAtSubmitMs = static_cast<float> (t->getProperty ("frame_age_at_submit_ms"));
                next.inferenceMs = static_cast<float> (t->getProperty ("inference_ms"));
                next.cameraBackend = t->getProperty ("backend").toString();
                next.shadowModelLoaded = static_cast<bool> (t->getProperty ("shadow_model_loaded"));
                next.shadowSamples = static_cast<int> (t->getProperty ("shadow_samples"));
                next.shadowAgreementRate = static_cast<float> (t->getProperty ("shadow_agreement_rate"));
                next.shadowDisagreementRate = static_cast<float> (t->getProperty ("shadow_disagreement_rate"));
                next.shadowMeanInferenceMs = static_cast<float> (t->getProperty ("shadow_mean_inference_ms"));
                next.shadowP95InferenceMs = static_cast<float> (t->getProperty ("shadow_p95_inference_ms"));
            }
        }

        // receivedAtMs is used by the processor as the age of the control data,
        // not merely the age of the UDP datagram. Fold the measured camera-to-
        // result latency into it so a sidecar that is still sending packets but
        // is processing an old camera backlog cannot continue automating the rack.
        if (next.captureToResultMs > 0.0f)
        {
            const auto pipelineAgeMs = juce::roundToInt (
                juce::jlimit (0.0f, 10000.0f, next.captureToResultMs));
            next.receivedAtMs -= static_cast<int64_t> (pipelineAgeMs);
        }

        if (const auto roleVar = object->getProperty ("role_config"); roleVar.isObject())
        {
            if (auto* role = roleVar.getDynamicObject())
            {
                next.swapHandedness = static_cast<bool> (role->getProperty ("swap_handedness"));
                next.handCalibrationActive = static_cast<bool> (role->getProperty ("calibration_active"));
                next.handCalibrationSamples = static_cast<int> (role->getProperty ("calibration_samples"));
                next.handCalibrationConfidence = static_cast<float> (role->getProperty ("calibration_confidence"));
                next.handCalibrationStatus = role->getProperty ("calibration_status").toString();
                next.handRoleSource = role->getProperty ("source").toString();
            }
        }

        if (const auto leftVar = object->getProperty ("left"); leftVar.isObject())
            parseHand (leftVar.getDynamicObject(), next.left);
        if (const auto rightVar = object->getProperty ("right"); rightVar.isObject())
            parseHand (rightVar.getDynamicObject(), next.right);

        // Physical role is a hard safety boundary. Before the camera/backend has
        // been calibrated (or explicitly manually overridden), keep raw
        // detections and landmarks available for the UI but fail closed on the
        // stable control fields. This prevents the same two-finger pose from
        // silently selecting Slot 2 with the wrong hand or firing Victory on the
        // selector hand while the L/R convention is still unknown.
        if (! physicalRolesTrusted (next))
        {
            next.left.stableSlot = 0;
            next.right.stableGesture = ControlGesture::unknown;
        }
    }

    // Liveness tracks actual packet arrival independently from control-data age.
    lastPacketReceivedAtMs.store (wallReceiveMs, std::memory_order_relaxed);

    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    const auto sameSession = snapshot.sessionId.isNotEmpty()
                          && next.sessionId.isNotEmpty()
                          && snapshot.sessionId == next.sessionId;
    if (snapshot.receivedAtMs > 0 && sameSession && next.sequence <= snapshot.sequence)
        return;
    snapshot = next;
}
}
