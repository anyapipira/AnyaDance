#include "core/protocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace anyadance {
namespace {

// The runtime UDP protocol is intentionally small and fixed-shape. This parser
// extracts the driver-supported fields from that packet shape, which keeps
// validation predictable in the SteamVR driver.
bool IsEscaped(std::string_view text, std::size_t index) {
    std::size_t slashCount = 0;
    while (index > 0 && text[--index] == '\\') {
        ++slashCount;
    }
    return (slashCount % 2) != 0;
}

std::size_t FindKey(std::string_view json, std::string_view key, std::size_t start = 0) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t pos = json.find(needle, start);
    while (pos != std::string_view::npos) {
        if (pos == 0 || !IsEscaped(json, pos - 1)) {
            return pos;
        }
        pos = json.find(needle, pos + 1);
    }
    return std::string_view::npos;
}

bool FindObjectAfterKey(std::string_view json, std::string_view key, std::string_view& out) {
    const std::size_t keyPos = FindKey(json, key);
    if (keyPos == std::string_view::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::size_t open = json.find('{', colon);
    if (open == std::string_view::npos) {
        return false;
    }

    // Walk balanced braces while ignoring braces inside strings. Device and
    // input objects are nested, so a simple "next }" search would cut them short.
    int depth = 0;
    bool inString = false;
    for (std::size_t i = open; i < json.size(); ++i) {
        const char ch = json[i];
        if (ch == '"' && !IsEscaped(json, i)) {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                out = json.substr(open, i - open + 1);
                return true;
            }
        }
    }
    return false;
}

bool ExtractBool(std::string_view object, std::string_view key, bool& out) {
    const std::size_t keyPos = FindKey(object, key);
    if (keyPos == std::string_view::npos) {
        return false;
    }
    std::size_t pos = object.find(':', keyPos);
    if (pos == std::string_view::npos) {
        return false;
    }
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }
    if (object.substr(pos, 4) == "true") {
        out = true;
        return true;
    }
    if (object.substr(pos, 5) == "false") {
        out = false;
        return true;
    }
    return false;
}

bool ExtractInt(std::string_view object, std::string_view key, int& out) {
    const std::size_t keyPos = FindKey(object, key);
    if (keyPos == std::string_view::npos) {
        return false;
    }
    std::size_t pos = object.find(':', keyPos);
    if (pos == std::string_view::npos) {
        return false;
    }
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }

    const std::string tail(object.substr(pos));
    char* end = nullptr;
    const long value = std::strtol(tail.c_str(), &end, 10);
    if (end == tail.c_str()) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool ExtractFloat(std::string_view object, std::string_view key, float& out) {
    const std::size_t keyPos = FindKey(object, key);
    if (keyPos == std::string_view::npos) {
        return false;
    }
    std::size_t pos = object.find(':', keyPos);
    if (pos == std::string_view::npos) {
        return false;
    }
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }

    const std::string tail(object.substr(pos));
    char* end = nullptr;
    const float value = std::strtof(tail.c_str(), &end);
    if (end == tail.c_str() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

template <std::size_t N>
bool ExtractFloatArray(std::string_view object, std::string_view key, std::array<float, N>& out) {
    const std::size_t keyPos = FindKey(object, key);
    if (keyPos == std::string_view::npos) {
        return false;
    }
    std::size_t pos = object.find('[', keyPos);
    if (pos == std::string_view::npos) {
        return false;
    }
    ++pos;

    for (std::size_t i = 0; i < N; ++i) {
        while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
            ++pos;
        }
        const std::string tail(object.substr(pos));
        char* end = nullptr;
        const float value = std::strtof(tail.c_str(), &end);
        if (end == tail.c_str() || !std::isfinite(value)) {
            return false;
        }
        out[i] = value;
        pos += static_cast<std::size_t>(end - tail.c_str());

        while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
            ++pos;
        }
        if (i + 1 < N) {
            if (pos >= object.size() || object[pos] != ',') {
                return false;
            }
            ++pos;
        }
    }

    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }
    return pos < object.size() && object[pos] == ']';
}

bool NormalizeQuaternion(std::array<float, 4>& q) {
    Quat quat{q[0], q[1], q[2], q[3]};
    if (!NormalizeIfAcceptable(quat)) {
        return false;
    }
    q = {quat.x, quat.y, quat.z, quat.w};
    return true;
}

bool ParseProtocolVersion(std::string_view json) {
    int version = 0;
    return ExtractInt(json, "version", version) && version == kProtocolVersion;
}

bool ParseDeviceSample(std::string_view devicesObject, const DeviceInfo& device, PoseSample& sample) {
    const std::string_view deviceId = device.id;
    std::string_view deviceObject;
    if (!FindObjectAfterKey(devicesObject, deviceId, deviceObject)) {
        return false;
    }

    std::string_view poseObject;
    if (!FindObjectAfterKey(deviceObject, "pose", poseObject)) {
        return false;
    }

    // Missing validity/connectivity is treated as malformed so every accepted
    // device object has the complete protocol shape.
    bool valid = true;
    bool connected = true;
    if (!ExtractBool(deviceObject, "valid", valid) || !ExtractBool(deviceObject, "connected", connected)) {
        return false;
    }

    std::array<float, 3> position{};
    std::array<float, 4> rotation{};
    if (!ExtractFloatArray(poseObject, "position", position) ||
        !ExtractFloatArray(poseObject, "rotation_xyzw", rotation)) {
        return false;
    }

    for (float component : position) {
        if (!std::isfinite(component) || std::fabs(component) > kMaxAbsPositionMeters) {
            return false;
        }
    }
    if (!NormalizeQuaternion(rotation)) {
        return false;
    }

    sample.valid = valid;
    sample.connected = connected;
    sample.position = position;
    if (sample.position[1] < MinDeviceY(device.index) || sample.position[1] > kMaxDeviceY) {
        sample.position[1] = ClampDeviceY(device.index, sample.position[1]);
        sample.y_clamped = true;
    }
    sample.rotation_xyzw = rotation;
    sample.received_at = std::chrono::steady_clock::now();
    return true;
}

void ParseControllerInput(std::string_view inputsObject, std::string_view deviceId, PoseSample& sample) {
    std::string_view deviceInput;
    if (!FindObjectAfterKey(inputsObject, deviceId, deviceInput)) {
        return;
    }

    bool triggerClick = false;
    if (ExtractBool(deviceInput, "trigger_click", triggerClick)) {
        sample.trigger_click = triggerClick;
    }

    float triggerValue = 0.0f;
    if (ExtractFloat(deviceInput, "trigger_value", triggerValue)) {
        sample.trigger_value = ClampFloat(triggerValue, 0.0f, 1.0f);
    } else if (sample.trigger_click) {
        sample.trigger_value = 1.0f;
    }

    bool menuClick = false;
    if (ExtractBool(deviceInput, "menu_click", menuClick)) {
        sample.menu_click = menuClick;
    }

    bool systemClick = false;
    if (ExtractBool(deviceInput, "system_click", systemClick)) {
        sample.system_click = systemClick;
    }

    bool aClick = false;
    if (ExtractBool(deviceInput, "a_click", aClick)) {
        sample.a_click = aClick;
    }

    bool bClick = false;
    if (ExtractBool(deviceInput, "b_click", bClick)) {
        sample.b_click = bClick;
    }

    bool gripClick = false;
    if (ExtractBool(deviceInput, "grip_click", gripClick)) {
        sample.grip_click = gripClick;
    }

    float gripValue = 0.0f;
    if (ExtractFloat(deviceInput, "grip_value", gripValue)) {
        sample.grip_value = ClampFloat(gripValue, 0.0f, 1.0f);
    } else if (sample.grip_click) {
        sample.grip_value = 1.0f;
    }

    float joystickX = 0.0f;
    if (ExtractFloat(deviceInput, "joystick_x", joystickX)) {
        sample.joystick_x = ClampFloat(joystickX, -1.0f, 1.0f);
    }

    float joystickY = 0.0f;
    if (ExtractFloat(deviceInput, "joystick_y", joystickY)) {
        sample.joystick_y = ClampFloat(joystickY, -1.0f, 1.0f);
    }

    // Trackpad mirrors the thumbstick when omitted. That keeps the protocol
    // compact while still satisfying bindings that read either control.
    float trackpadX = 0.0f;
    if (ExtractFloat(deviceInput, "trackpad_x", trackpadX)) {
        sample.trackpad_x = ClampFloat(trackpadX, -1.0f, 1.0f);
    } else {
        sample.trackpad_x = sample.joystick_x;
    }

    float trackpadY = 0.0f;
    if (ExtractFloat(deviceInput, "trackpad_y", trackpadY)) {
        sample.trackpad_y = ClampFloat(trackpadY, -1.0f, 1.0f);
    } else {
        sample.trackpad_y = sample.joystick_y;
    }

    std::string_view bendsObject;
    if (FindObjectAfterKey(deviceInput, "finger_bends", bendsObject)) {
        float thumb = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;
        if (ExtractFloat(bendsObject, "thumb", thumb) && ExtractFloat(bendsObject, "index", index) &&
            ExtractFloat(bendsObject, "middle", middle) && ExtractFloat(bendsObject, "ring", ring) &&
            ExtractFloat(bendsObject, "pinky", pinky)) {
            sample.has_finger_bends = true;
            sample.finger_bends.thumb = ClampFloat(thumb, 0.0f, 1.0f);
            sample.finger_bends.index = ClampFloat(index, 0.0f, 1.0f);
            sample.finger_bends.middle = ClampFloat(middle, 0.0f, 1.0f);
            sample.finger_bends.ring = ClampFloat(ring, 0.0f, 1.0f);
            sample.finger_bends.pinky = ClampFloat(pinky, 0.0f, 1.0f);
        }
    }
}

bool SameFingerBends(const FingerBends& a, const FingerBends& b) {
    return a.thumb == b.thumb && a.index == b.index && a.middle == b.middle &&
        a.ring == b.ring && a.pinky == b.pinky;
}

// Exact float comparison is deliberate. A held pose is re-serialized from
// unchanged state, so a repeat arrives bit-identical; a tolerance here would
// instead swallow slow deliberate motion.
bool SameSample(const PoseSample& a, const PoseSample& b) {
    return a.valid == b.valid && a.connected == b.connected &&
        a.position == b.position && a.rotation_xyzw == b.rotation_xyzw &&
        a.trigger_click == b.trigger_click && a.trigger_value == b.trigger_value &&
        a.menu_click == b.menu_click && a.system_click == b.system_click &&
        a.a_click == b.a_click && a.b_click == b.b_click &&
        a.grip_click == b.grip_click && a.grip_value == b.grip_value &&
        a.joystick_x == b.joystick_x && a.joystick_y == b.joystick_y &&
        a.trackpad_x == b.trackpad_x && a.trackpad_y == b.trackpad_y &&
        a.has_finger_bends == b.has_finger_bends &&
        (!a.has_finger_bends || SameFingerBends(a.finger_bends, b.finger_bends)) &&
        a.y_clamped == b.y_clamped;
}

void AppendBool(std::ostringstream& out, bool value) {
    out << (value ? "true" : "false");
}

void AppendFloat(std::ostringstream& out, float value) {
    out << std::setprecision(9) << value;
}

void AppendDevice(std::ostringstream& out, const DeviceState& device) {
    out << "{\"valid\":";
    AppendBool(out, device.valid);
    out << ",\"connected\":";
    AppendBool(out, device.connected);
    out << ",\"pose\":{\"position\":[";
    AppendFloat(out, device.position.x);
    out << ',';
    AppendFloat(out, device.position.y);
    out << ',';
    AppendFloat(out, device.position.z);
    out << "],\"rotation_xyzw\":[";
    AppendFloat(out, device.rotation.x);
    out << ',';
    AppendFloat(out, device.rotation.y);
    out << ',';
    AppendFloat(out, device.rotation.z);
    out << ',';
    AppendFloat(out, device.rotation.w);
    out << "]}}";
}

void AppendController(std::ostringstream& out, const ControllerState& input) {
    out << "{\"trigger_click\":";
    AppendBool(out, input.trigger_click);
    out << ",\"trigger_value\":";
    AppendFloat(out, ClampFloat(input.trigger_value, 0.0f, 1.0f));
    out << ",\"menu_click\":";
    AppendBool(out, input.menu_click);
    out << ",\"system_click\":";
    AppendBool(out, input.system_click);
    out << ",\"a_click\":";
    AppendBool(out, input.a_click);
    out << ",\"b_click\":";
    AppendBool(out, input.b_click);
    out << ",\"grip_click\":";
    AppendBool(out, input.grip_click);
    out << ",\"grip_value\":";
    AppendFloat(out, ClampFloat(input.grip_value, 0.0f, 1.0f));
    out << ",\"joystick_x\":";
    AppendFloat(out, ClampFloat(input.joystick_x, -1.0f, 1.0f));
    out << ",\"joystick_y\":";
    AppendFloat(out, ClampFloat(input.joystick_y, -1.0f, 1.0f));
    out << ",\"trackpad_x\":";
    AppendFloat(out, ClampFloat(input.trackpad_x, -1.0f, 1.0f));
    out << ",\"trackpad_y\":";
    AppendFloat(out, ClampFloat(input.trackpad_y, -1.0f, 1.0f));
    if (input.has_finger_bends) {
        out << ",\"finger_bends\":{\"thumb\":";
        AppendFloat(out, ClampFloat(input.finger_bends.thumb, 0.0f, 1.0f));
        out << ",\"index\":";
        AppendFloat(out, ClampFloat(input.finger_bends.index, 0.0f, 1.0f));
        out << ",\"middle\":";
        AppendFloat(out, ClampFloat(input.finger_bends.middle, 0.0f, 1.0f));
        out << ",\"ring\":";
        AppendFloat(out, ClampFloat(input.finger_bends.ring, 0.0f, 1.0f));
        out << ",\"pinky\":";
        AppendFloat(out, ClampFloat(input.finger_bends.pinky, 0.0f, 1.0f));
        out << '}';
    }
    out << '}';
}

} // namespace

bool ParsePoseFrame(std::string_view json, ParsedFrame& frame) {
    frame = {};
    if (!ParseProtocolVersion(json)) {
        return false;
    }

    std::string_view devicesObject;
    if (!FindObjectAfterKey(json, "devices", devicesObject)) {
        return false;
    }

    std::string_view inputsObject;
    const bool hasInputs = FindObjectAfterKey(json, "inputs", inputsObject);

    bool anyPresent = false;
    for (const DeviceInfo& device : kDevices) {
        PoseSample sample;
        if (ParseDeviceSample(devicesObject, device, sample)) {
            if (hasInputs) {
                ParseControllerInput(inputsObject, device.id, sample);
            }
            const std::size_t slot = DeviceSlot(device.index);
            frame.samples[slot] = sample;
            frame.present[slot] = true;
            frame.y_clamped[slot] = sample.y_clamped;
            anyPresent = true;
        }
    }

    return anyPresent;
}

bool ParsePoseFrameBytes(const char* data, int size, ParsedFrame& frame) {
    if (!data || size <= 0 || size >= kMaxPacketBytes) {
        frame = {};
        return false;
    }
    return ParsePoseFrame(std::string_view(data, static_cast<std::size_t>(size)), frame);
}

bool SamePoseCommand(const ParsedFrame& a, const ParsedFrame& b) {
    if (a.present != b.present || a.y_clamped != b.y_clamped) {
        return false;
    }
    // Absent slots keep default-constructed samples that no device acted on, so
    // only the entries this frame actually carried are compared.
    for (std::size_t slot = 0; slot < a.samples.size(); ++slot) {
        if (a.present[slot] && !SameSample(a.samples[slot], b.samples[slot])) {
            return false;
        }
    }
    return true;
}

std::string SerializeFrame(const FrameState& sourceFrame) {
    FrameState frame = sourceFrame;
    ClampFrameY(frame);

    std::ostringstream out;
    out << "{\"version\":" << kProtocolVersion << ",\"devices\":{";
    for (std::size_t i = 0; i < kDevices.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << '\"' << kDevices[i].id << "\":";
        AppendDevice(out, frame.devices[i]);
    }
    out << "},\"inputs\":{";
    out << "\"left_controller\":";
    AppendController(out, frame.controllers[0]);
    out << ",\"right_controller\":";
    AppendController(out, frame.controllers[1]);
    out << "}}";
    return out.str();
}

std::string PrettyPrintJson(std::string_view compactJson) {
    std::string out;
    out.reserve(compactJson.size() + 64);
    int indent = 0;
    bool inString = false;
    for (std::size_t i = 0; i < compactJson.size(); ++i) {
        const char ch = compactJson[i];
        if (ch == '"' && !IsEscaped(compactJson, i)) {
            inString = !inString;
            out.push_back(ch);
            continue;
        }
        if (inString) {
            out.push_back(ch);
            continue;
        }
        if (ch == '{' || ch == '[') {
            out.push_back(ch);
            out.push_back('\n');
            ++indent;
            out.append(static_cast<std::size_t>(indent) * 2, ' ');
        } else if (ch == '}' || ch == ']') {
            out.push_back('\n');
            indent = std::max(0, indent - 1);
            out.append(static_cast<std::size_t>(indent) * 2, ' ');
            out.push_back(ch);
        } else if (ch == ',') {
            out.push_back(ch);
            out.push_back('\n');
            out.append(static_cast<std::size_t>(indent) * 2, ' ');
        } else if (ch == ':') {
            out.push_back(ch);
            out.push_back(' ');
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace anyadance
