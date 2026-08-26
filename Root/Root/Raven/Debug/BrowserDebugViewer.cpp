#include "Raven/Debug/BrowserDebugViewer.h"

#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace Raven
{
    bool BrowserDebugViewer::Open(const std::filesystem::path& filePath)
    {
        // ブラウザへ渡す前に存在確認を行います。
        // ShellExecute側のエラーだけに依存すると「パスが間違っている」のか
        // 「関連付けされたアプリケーションを起動できない」のか判断しづらいためです。
        if (std::filesystem::exists(filePath) == false)
        {
            std::cerr << "[BrowserDebugViewer] ファイルが存在しません: "
                      << filePath.string() << '\n';
            return false;
        }

#ifdef _WIN32
        // ShellExecuteWにL"open"を渡すことで、ChromeやEdgeを直接指定せず、
        // Windowsでユーザーが設定している既定ブラウザに処理を委譲します。
        // std::filesystem::path::c_str()はWindowsではwchar_t文字列になるため、
        // 日本語を含むパスでもANSI変換を挟まずに渡せます。
        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"open",
            filePath.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);

        // ShellExecuteの仕様では、戻り値が32より大きければ成功です。
        const INT_PTR resultCode = reinterpret_cast<INT_PTR>(result);
        if (resultCode <= 32)
        {
            std::cerr << "[BrowserDebugViewer] ブラウザを起動できませんでした。ErrorCode="
                      << resultCode << '\n';
            return false;
        }

        return true;
#else
        // 現在のRavenはVisual Studio / Windowsを中心に構成されています。
        // Linux/macOS対応時はxdg-open / open等をここへ追加する想定です。
        std::cerr << "[BrowserDebugViewer] 現在はWindowsのみ対応しています。\n";
        return false;
#endif
    }

    bool BrowserDebugViewer::WriteStartupSvg(const std::filesystem::path& filePath)
    {
        const std::filesystem::path parentPath = filePath.parent_path();

        // output.svgだけのように親ディレクトリを持たないパスも許可します。
        if (parentPath.empty() == false)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(parentPath, errorCode);

            if (errorCode)
            {
                std::cerr << "[BrowserDebugViewer] 出力ディレクトリを作成できませんでした: "
                          << parentPath.string() << '\n';
                return false;
            }
        }

        std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
        if (stream.is_open() == false)
        {
            std::cerr << "[BrowserDebugViewer] SVGファイルを作成できませんでした: "
                      << filePath.string() << '\n';
            return false;
        }

        // 最初の段階ではViewerそのものの起動確認を目的とした固定SVGを生成します。
        // 次段階でSoftBody / Spatial Hashなどの実デバッグ情報をこのSVG生成処理から
        // 独立したWriterへ切り出し、毎フレーム更新できる構造へ発展させます。
        stream
            << R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="960" height="540" viewBox="0 0 960 540">
  <rect width="960" height="540" fill="#111827" />
  <text x="48" y="72" fill="#f9fafb" font-family="sans-serif" font-size="34" font-weight="bold">Raven Browser Debug Viewer</text>
  <text x="48" y="112" fill="#9ca3af" font-family="sans-serif" font-size="18">SVG debug output is ready.</text>

  <rect x="48" y="160" width="864" height="300" rx="12" fill="#1f2937" stroke="#4b5563" stroke-width="2" />

  <circle cx="190" cy="310" r="42" fill="#60a5fa" />
  <circle cx="310" cy="270" r="28" fill="#34d399" />
  <circle cx="405" cy="340" r="34" fill="#f59e0b" />
  <line x1="190" y1="310" x2="310" y2="270" stroke="#e5e7eb" stroke-width="4" />
  <line x1="310" y1="270" x2="405" y2="340" stroke="#e5e7eb" stroke-width="4" />

  <text x="500" y="250" fill="#f9fafb" font-family="monospace" font-size="20">Startup SVG</text>
  <text x="500" y="290" fill="#9ca3af" font-family="monospace" font-size="16">Next: Physics debug visualization</text>
  <text x="500" y="325" fill="#9ca3af" font-family="monospace" font-size="16">- Spatial Hash cells</text>
  <text x="500" y="355" fill="#9ca3af" font-family="monospace" font-size="16">- Particles / Triangles</text>
  <text x="500" y="385" fill="#9ca3af" font-family="monospace" font-size="16">- NarrowPhase candidates</text>
</svg>
)SVG";

        if (stream.good() == false)
        {
            std::cerr << "[BrowserDebugViewer] SVGの書き込み中にエラーが発生しました。\n";
            return false;
        }

        return true;
    }
}
