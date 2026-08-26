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
    // 現段階では「ファイルを1回開く」責務だけを持たせます。
    // SVG生成処理やPhysics固有の可視化処理をここへ混ぜないことで、将来的に
    // HTML自動更新 / localhost / WebSocket方式へ拡張するときも責務を分離できます。
    class BrowserDebugViewer
    {
    public:
        // 指定されたローカルファイルをOSの既定ブラウザで開きます。
        // 成功した場合はtrue、ファイルが存在しない場合やOS側の起動に失敗した場合はfalseです。
        static bool Open(const std::filesystem::path& filePath);

        // BrowserDebugViewerの動作確認用SVGを生成します。
        // 親ディレクトリが存在しない場合は自動的に作成します。
        static bool WriteStartupSvg(const std::filesystem::path& filePath);
    };
}
