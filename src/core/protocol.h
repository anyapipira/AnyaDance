#pragma once

#include "core/constants.h"
#include "core/frame_state.h"
#include "core/pose_sample.h"

#include <array>
#include <string>
#include <string_view>

namespace anyadance {

struct ParsedFrame {
    std::array<PoseSample, kDevices.size()> samples{};
    std::array<bool, kDevices.size()> present{};
    std::array<bool, kDevices.size()> y_clamped{};
};

bool ParsePoseFrame(std::string_view json, ParsedFrame& frame);
bool ParsePoseFrameBytes(const char* data, int size, ParsedFrame& frame);
// True when two accepted frames ask the devices for the same thing. Senders
// stream a held pose at the frame rate, so consecutive frames are commonly
// identical; comparing the commanded state lets a consumer collapse them.
// Arrival time is not part of the comparison.
bool SamePoseCommand(const ParsedFrame& a, const ParsedFrame& b);
std::string SerializeFrame(const FrameState& frame);
std::string PrettyPrintJson(std::string_view compactJson);

} // namespace anyadance