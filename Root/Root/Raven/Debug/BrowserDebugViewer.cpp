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
        if (std::filesystem::exists(filePath) == false)
        {
            std::cerr << "[BrowserDebugViewer] ファイルが存在しません: " << filePath.string() << '\n';
            return false;
        }

#ifdef _WIN32
        // 特定ブラウザへ依存せず、Windowsの既定アプリケーションへ表示を委譲します。
        HINSTANCE result = ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        const INT_PTR resultCode = reinterpret_cast<INT_PTR>(result);
        if (resultCode <= 32)
        {
            std::cerr << "[BrowserDebugViewer] ブラウザを起動できませんでした。ErrorCode=" << resultCode << '\n';
            return false;
        }
        return true;
#else
        std::cerr << "[BrowserDebugViewer] 現在はWindowsのみ対応しています。\n";
        return false;
#endif
    }

    bool BrowserDebugViewer::WriteStartupSvg(const std::filesystem::path& filePath)
    {
        const std::filesystem::path parentPath = filePath.parent_path();
        if (parentPath.empty() == false)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(parentPath, errorCode);
            if (errorCode)
            {
                return false;
            }
        }

        std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
        if (stream.is_open() == false)
        {
            return false;
        }

        stream << R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="960" height="540" viewBox="0 0 960 540">
  <rect width="960" height="540" fill="#111827" />
  <text x="48" y="72" fill="#f9fafb" font-family="sans-serif" font-size="34" font-weight="bold">Raven Browser Debug Viewer</text>
  <text x="48" y="112" fill="#9ca3af" font-family="sans-serif" font-size="18">Waiting for runtime physics SVG...</text>
  <rect x="48" y="160" width="864" height="300" rx="12" fill="#1f2937" stroke="#4b5563" stroke-width="2" />
  <text x="96" y="245" fill="#60a5fa" font-family="monospace" font-size="20">Browser connection: ready</text>
  <text x="96" y="290" fill="#9ca3af" font-family="monospace" font-size="16">SoftBodyPhysicsDebugSvgWriter will replace this SVG.</text>
</svg>
)SVG";
        return stream.good();
    }

    bool BrowserDebugViewer::WriteAutoReloadHtml(
        const std::filesystem::path& htmlPath,
        const std::filesystem::path& svgPath,
        uint32_t reloadIntervalMilliseconds)
    {
        const std::filesystem::path parentPath = htmlPath.parent_path();
        if (parentPath.empty() == false)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(parentPath, errorCode);
            if (errorCode)
            {
                return false;
            }
        }

        std::ofstream stream(htmlPath, std::ios::binary | std::ios::trunc);
        if (stream.is_open() == false)
        {
            return false;
        }

        // HTMLとSVGは同じGeneratedディレクトリへ置く想定なので、ブラウザへは相対パスを渡します。
        // Date.now()をQueryへ付与してブラウザキャッシュを回避し、RavenがSVGを上書きした内容を
        // reloadIntervalMillisecondsごとに確実に再取得します。
        const std::string svgFileName = svgPath.filename().generic_string();
        const uint32_t safeReloadInterval = std::max(reloadIntervalMilliseconds, 50u);

        stream << "<!doctype html>\n"
               << "<html><head><meta charset=\"utf-8\">"
               << "<title>Raven Physics Debug Viewer</title>"
               << "<style>html,body{margin:0;width:100%;height:100%;background:#020617;overflow:hidden;}"
               << "#view{width:100%;height:100%;object-fit:contain;}</style></head>\n"
               << "<body><img id=\"view\" alt=\"Raven Physics Debug SVG\">\n"
               << "<script>\n"
               << "const view=document.getElementById('view');\n"
               << "const source='" << svgFileName << "';\n"
               << "function refresh(){view.src=source+'?t='+Date.now();}\n"
               << "refresh();setInterval(refresh," << safeReloadInterval << ");\n"
               << "</script></body></html>\n";

        return stream.good();
    }
}
