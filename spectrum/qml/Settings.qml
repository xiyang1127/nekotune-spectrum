import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoTune 1.0
Item {
    id: root
    property var status: ({})
    property string error: ""
    readonly property bool chinese: i18n.language.indexOf("zh") === 0
    ExtensionApi { id: api; onEvent: function(message) { if (message.event === "extension.nekotune.spectrum.status") root.status = message.data } }
    function reasonLabel(value) {
        if (value === "playback_backend_unsupported") return chinese ? "当前播放后端不支持；请使用 FFmpeg 后端" : "Use the FFmpeg playback backend"
        if (value === "host_pcm_unsupported" || value === "pcm_unavailable") return chinese ? "需要提供 SDK 2.2 PCM 能力的宿主" : "Requires a host with SDK 2.2 PCM support"
        if (value === "audio_output_unavailable") return chinese ? "音频输出设备不可用" : "Audio output unavailable"
        return value || "—"
    }
    function refresh() {
        api.invoke("status", {}).then(function(value) { root.status = value; root.error = "" }, function(error) { root.error = String(error) })
    }
    Component.onCompleted: refresh()
    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
        spacing: 16
        Label { text: root.chinese ? "频谱 · Spectrum" : "Spectrum"; font.pixelSize: 26; color: api.theme.textPrimary }
        Label { text: (root.chinese ? "播放后端：" : "Playback backend: ") + (root.status.backend || "—"); color: api.theme.textSecondary }
        Label { text: (root.chinese ? "壁纸订阅：" : "Consumers: ") + (root.status.consumers || 0); color: api.theme.textSecondary }
        Label {
            text: root.error || (!root.status.available
                ? (root.chinese ? "频谱不可用：" : "Spectrum unavailable: ") + root.reasonLabel(root.status.reason)
                : root.status.analyzing ? (root.chinese ? "正在分析 · 48 频段 / 30 Hz" : "Analyzing · 48 bands / 30 Hz")
                : root.chinese ? "待机 · 播放且有壁纸订阅时开始分析" : "Idle · starts when playing with a wallpaper consumer")
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: api.theme.textSecondary
        }
        Button { text: root.chinese ? "刷新" : "Refresh"; onClicked: root.refresh() }
    }
}
