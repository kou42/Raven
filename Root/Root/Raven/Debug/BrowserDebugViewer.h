#pragma once

#include <filesystem>

namespace Raven
{
    // ========================================================================
    // BrowserDebugViewer
    // ========================================================================
    // Ravenが生成したSVGなどのデバッグ用ファイルを、OSの既定ブラウザで表示するための
    // 小さなユーティリティです。
    //
    // Physics固有のSVG生成は別Writerへ分離し、このクラスは「ブラウザ表示環境」の責務だけを持ちます。
    class BrowserDebugViewer
    {
    public:
        // 指定されたローカルファイルをOSの既定ブラウザで開きます。
        static bool Open(const std::filesystem::path& filePath);

        // BrowserDebugViewer単体の動作確認用SVGを生成します。
        static bool WriteStartupSvg(const std::filesystem::path& filePath);

        // 指定SVGを一定間隔で再読み込みするHTML Viewerを生成します。
        // SVG自体を書き換えるだけでブラウザ側へ最新状態が反映されるため、Raven側で毎回
        // ブラウザを開き直す必要がありません。
        static bool WriteAutoReloadHtml(
            const std::filesystem::path& htmlPath,
            const std::filesystem::path& svgPath,
            uint32_t reloadIntervalMilliseconds = 250u);
    };
}
