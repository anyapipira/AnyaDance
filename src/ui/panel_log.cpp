#include "ui/ui_state.h"

#include <string>
#include <vector>

namespace anyadance::ui {

// Hover preview for a log row: timestamp, endpoint, reason/result, and the
// pretty-printed JSON payload in the monospace font.
void RenderLogTooltip(const UdpLogEntry& entry) {
    ImGui::BeginTooltip();
    ImGui::Text("%s", entry.timeText.c_str());
    ImGui::Text("127.0.0.1:%u", kUdpPort);
    if (!entry.detail.empty()) {
        ImGui::TextWrapped("%s", entry.detail.c_str());
    } else {
        ImGui::TextWrapped("%s", entry.result.c_str());
    }
    ImGui::Separator();
    ImGui::BeginChild("json_tip", ImVec2(640, 320), true, ImGuiWindowFlags_HorizontalScrollbar);
    const std::string pretty = PrettyPrintJson(entry.payload);
    if (g_monoFont) { ImGui::PushFont(g_monoFont); }
    ImGui::TextUnformatted(pretty.c_str());
    if (g_monoFont) { ImGui::PopFont(); }
    ImGui::EndChild();
    ImGui::EndTooltip();
}

// curl has no UDP transport, so a runnable "resend" command for this loopback
// UDP datagram is a dependency-free PowerShell one-liner the user already has.
std::string BuildResendCommand(const std::string& payload) {
    std::string escaped;
    escaped.reserve(payload.size());
    for (const char c : payload) {
        escaped.push_back(c);
        if (c == '\'') {
            escaped.push_back('\''); // double single quotes for a PS literal string
        }
    }
    std::string command = "$c=New-Object System.Net.Sockets.UdpClient;";
    command += "$b=[Text.Encoding]::UTF8.GetBytes('" + escaped + "');";
    command += "$c.Send($b,$b.Length,'127.0.0.1'," + std::to_string(kUdpPort) + ")>$null;";
    command += "$c.Close()";
    return command;
}

// Floating detail window for the selected log entry: header fields, the three
// payload actions (copy raw, copy a PowerShell resend command, resend in-UI),
// and the full pretty-printed JSON. Closing it clears the selection.
void RenderPinnedLogDetail(const UdpLogEntry& entry) {
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(720.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UDP Log Detail", &open)) {
        ImGui::End();
        if (!open) {
            g_app.selectedLogIndex = -1;
        }
        return;
    }
    if (!open) {
        g_app.selectedLogIndex = -1;
    }
    ImGui::Text("%s", entry.timeText.c_str());
    ImGui::Text("127.0.0.1:%u", kUdpPort);
    ImGui::Text("%s: %s", Tr(Text::Reason), entry.reason.c_str());
    if (!entry.detail.empty()) {
        ImGui::TextWrapped("%s", entry.detail.c_str());
    } else {
        ImGui::Text("%s: %s", Tr(Text::Result), entry.result.c_str());
    }
    // Copy the raw request body so it can be inspected or edited elsewhere.
    if (ImGui::Button(Tr(Text::Copy))) {
        ImGui::SetClipboardText(entry.payload.c_str());
    }
    ImGui::SameLine();
    // Copy a ready-to-run command that resends this datagram to the UDP endpoint.
    if (ImGui::Button(Tr(Text::CopyCommand))) {
        ImGui::SetClipboardText(BuildResendCommand(entry.payload).c_str());
    }
    ImGui::SameLine();
    // Resend this exact datagram to the UDP endpoint from the UI itself.
    if (ImGui::Button(Tr(Text::Resend))) {
        g_app.streamer.SendRaw(entry.payload, En(Text::ResendReason));
    }
    ImGui::Separator();
    ImGui::BeginChild("pinned_json", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const std::string pretty = PrettyPrintJson(entry.payload);
    if (g_monoFont) { ImGui::PushFont(g_monoFont); }
    ImGui::TextUnformatted(pretty.c_str());
    if (g_monoFont) { ImGui::PopFont(); }
    ImGui::EndChild();
    ImGui::End();
}

// Right half of the window: the scrolling table of UDP log entries. Rows show a
// tooltip on hover and open the pinned detail window when selected; the view
// follows the newest row only while it is already scrolled to the bottom, so
// scrolling up to inspect or click an older entry is not yanked back down.
void RenderLogPanel() {
    ImGui::BeginChild("log", ImVec2(0, 0), false);
    ImGui::TextUnformatted(Tr(Text::UdpLog));
    // Right-align the checkbox and Clear button: Clear pins to the right edge and
    // the checkbox sits just left of it. A checkbox spans the box (frame height)
    // plus the inner spacing and its label.
    const ImGuiStyle& logStyle = ImGui::GetStyle();
    const float clearX = ImGui::GetWindowContentRegionMax().x - 50.0f;
    const float checkboxWidth = ImGui::GetFrameHeight() + logStyle.ItemInnerSpacing.x +
        ImGui::CalcTextSize(Tr(Text::LogScrollLatest)).x;
    ImGui::SameLine(clearX - logStyle.ItemSpacing.x - checkboxWidth);
    ImGui::Checkbox(Tr(Text::LogScrollLatest), &g_app.logScrollToLatest);
    ImGui::SameLine(clearX);
    if (ImGui::SmallButton(Tr(Text::Clear))) {
        g_app.streamer.ClearLog();
        g_app.selectedLogIndex = -1;
    }

    const std::vector<UdpLogEntry> entries = g_app.streamer.SnapshotLog();
    if (g_app.selectedLogIndex >= static_cast<int>(entries.size())) {
        g_app.selectedLogIndex = -1;
    }

    if (ImGui::BeginTable("udp_log_table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, -1))) {
        ImGui::TableSetupColumn(Tr(Text::Time), ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn(Tr(Text::Reason), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(Tr(Text::Result), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const UdpLogEntry& entry = entries[static_cast<std::size_t>(i)];
            const bool selected = g_app.selectedLogIndex == i;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (ImGui::Selectable(entry.timeText.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing()))) {
                g_app.selectedLogIndex = selected ? -1 : i;
                g_app.logScrollToLatest = false;  // inspecting an entry stops following the tail
            }
            const bool rowHovered = ImGui::IsItemHovered();
            if (rowHovered && !selected) {
                RenderLogTooltip(entry);
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.reason.c_str());
            ImGui::TableSetColumnIndex(2);
            // Green for a delivered datagram, red for any failure/socket error.
            const bool ok = entry.result == En(Text::Sent);
            ImGui::TextColored(ok ? col::Green : col::Red, "%s", entry.result.c_str());
        }
        // Follow the newest row while "Scroll to latest" is checked. Clicking a
        // log entry unchecks it (see above), so inspecting an older row is not
        // yanked back down; re-checking the box resumes tailing.
        if (g_app.logScrollToLatest && !entries.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (g_app.selectedLogIndex >= 0 && g_app.selectedLogIndex < static_cast<int>(entries.size())) {
        RenderPinnedLogDetail(entries[static_cast<std::size_t>(g_app.selectedLogIndex)]);
    }
}

} // namespace anyadance::ui
