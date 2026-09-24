import QtQuick 2.15
import QtQuick.Controls 2.15
import StreamingPreferences 1.0

// Per-host encoding choices. A current PLANK host advertises only modes that
// passed its real encoder probes. An empty list is the compatibility fallback
// for older hosts, including the macOS preview host.
PlankComboBox {
    id: control

    property int captureSource: StreamingPreferences.PLANK_CAPTURE_NVFBC_8BIT
    property var supportedModes: []
    property int videoProfile: StreamingPreferences.PLANK_PROFILE_NVENC_HEVC_8BIT_420
    property bool rebuilding: false
    readonly property bool hasChoices: choices.count > 0

    textRole: "text"
    model: ListModel { id: choices }

    function modeSupported(mode) {
        if (supportedModes === undefined || supportedModes === null ||
                supportedModes.length === 0) {
            return true
        }
        for (var i = 0; i < supportedModes.length; ++i) {
            if (supportedModes[i] === mode) {
                return true
            }
        }
        return false
    }

    function addChoice(text, profile, mode) {
        if (modeSupported(mode)) {
            choices.append({text: text, val: profile, mode: mode})
        }
    }

    function selectVideoProfile(profile) {
        videoProfile = profile
        rebuild()
    }

    function rebuild() {
        rebuilding = true
        var desired = videoProfile
        choices.clear()

        if (captureSource === StreamingPreferences.PLANK_CAPTURE_SCREENCAPTUREKIT) {
            addChoice(qsTr("HEVC 10-bit 4:2:0 — Apple VideoToolbox (Preview)"),
                      StreamingPreferences.PLANK_PROFILE_APPLE_HEVC_10BIT_420,
                      "hevc-10-420-videotoolbox")
            addChoice(qsTr("HEVC 10-bit 4:4:4 — Apple VideoToolbox (Preview)"),
                      StreamingPreferences.PLANK_PROFILE_APPLE_HEVC_10BIT_444,
                      "hevc-10-444-videotoolbox")
        } else if (captureSource === StreamingPreferences.PLANK_CAPTURE_X11_NATIVE10) {
            addChoice(qsTr("H.264 10-bit 4:4:4 (identity GBR) — x264"),
                      StreamingPreferences.PLANK_PROFILE_H264_10BIT_444,
                      "h264-10-444-software")
            addChoice(qsTr("H.265 10-bit 4:4:4 (identity GBR) — NVENC"),
                      StreamingPreferences.PLANK_PROFILE_NVENC_HEVC_10BIT_444,
                      "hevc-10-444-nvenc")
        } else {
            addChoice(qsTr("H.264 8-bit 4:2:2"),
                      StreamingPreferences.PLANK_PROFILE_H264_8BIT_422,
                      "h264-8-422-software")
            addChoice(qsTr("H.264 8-bit 4:4:4 (identity GBR)"),
                      StreamingPreferences.PLANK_PROFILE_H264_8BIT_444,
                      "h264-8-444-software")
            addChoice(qsTr("H.264 10-bit 4:2:2"),
                      StreamingPreferences.PLANK_PROFILE_H264_10BIT_422,
                      "h264-10-422-software")
            addChoice(qsTr("H.264 10-bit 4:4:4 (identity GBR)"),
                      StreamingPreferences.PLANK_PROFILE_H264_10BIT_444,
                      "h264-10-444-software")
            addChoice(qsTr("H.264 8-bit 4:4:4 (identity GBR) — NVENC"),
                      StreamingPreferences.PLANK_PROFILE_NVENC_H264_8BIT_444,
                      "h264-8-444-nvenc")
            addChoice(qsTr("H.265 8-bit 4:2:0 — NVENC (best for limited bandwidth)"),
                      StreamingPreferences.PLANK_PROFILE_NVENC_HEVC_8BIT_420,
                      "hevc-8-420-nvenc")
            addChoice(qsTr("H.265 8-bit 4:4:4 (identity GBR) — NVENC"),
                      StreamingPreferences.PLANK_PROFILE_NVENC_HEVC_8BIT_444,
                      "hevc-8-444-nvenc")
            addChoice(qsTr("H.265 10-bit 4:4:4 (identity GBR) — NVENC"),
                      StreamingPreferences.PLANK_PROFILE_NVENC_HEVC_10BIT_444,
                      "hevc-10-444-nvenc")
        }

        var selected = -1
        for (var i = 0; i < choices.count; ++i) {
            if (choices.get(i).val === desired) {
                selected = i
                break
            }
        }
        if (selected < 0 &&
                (captureSource === StreamingPreferences.PLANK_CAPTURE_NVFBC_8BIT ||
                 captureSource === StreamingPreferences.PLANK_CAPTURE_DDUP ||
                 captureSource === StreamingPreferences.PLANK_CAPTURE_WGC)) {
            for (var fallback = 0; fallback < choices.count; ++fallback) {
                if (choices.get(fallback).val ===
                        StreamingPreferences.PLANK_PROFILE_NVENC_HEVC_8BIT_420) {
                    selected = fallback
                    break
                }
            }
        }
        if (selected < 0 && choices.count > 0) {
            selected = 0
        }
        currentIndex = selected
        videoProfile = selected >= 0 ? choices.get(selected).val : desired
        rebuilding = false
    }

    onCurrentIndexChanged: {
        if (!rebuilding && currentIndex >= 0 && currentIndex < choices.count) {
            videoProfile = choices.get(currentIndex).val
        }
    }
    onCaptureSourceChanged: rebuild()
    onSupportedModesChanged: rebuild()
    Component.onCompleted: rebuild()
}
