#include "Raven/Debug/BrowserDebugViewer.h"

#include <algorithm>
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

    bool BrowserDebugViewer::OpenUrl(const std::string& url)
    {
#ifdef _WIN32
        if (url.empty())
        {
            return false;
        }

        // Browser Debug ServerのURLはASCII文字だけで構成されるため、UTF-8 -> UTF-16変換を
        // MultiByteToWideCharで明示的に行います。将来URLへ日本語を含める場合も同じ変換経路を使えます。
        const int requiredLength = MultiByteToWideChar(
            CP_UTF8,
            0,
            url.c_str(),
            static_cast<int>(url.size()),
            nullptr,
            0);
        if (requiredLength <= 0)
        {
            return false;
        }

        std::wstring wideUrl(static_cast<std::size_t>(requiredLength), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            url.c_str(),
            static_cast<int>(url.size()),
            wideUrl.data(),
            requiredLength);

        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"open",
            wideUrl.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        const INT_PTR resultCode = reinterpret_cast<INT_PTR>(result);
        if (resultCode <= 32)
        {
            std::cerr << "[BrowserDebugViewer] URLをブラウザで開けませんでした。ErrorCode="
                      << resultCode << '\n';
            return false;
        }
        return true;
#else
        static_cast<void>(url);
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

        // Queryへ時刻を付けてブラウザキャッシュを回避し、Ravenが上書きしたSVGを定期再取得します。
        //
        // 左側は従来のSpatial Hash / Particle / Triangle全体表示です。
        // 右側はCandidateRejects.svgを<object>で同一Origin表示します。<img>ではSVG内部の
        // JavaScript / click eventが無効化されるため、Particle / Triangle直接選択には<object>が必要です。
        //
        // Reject SVGからは /filter endpointへ選択値を送り、postMessageで親Viewerへ同じ値を通知します。
        // 親側は入力欄とURL Queryだけを同期し、同じ/filterを二重送信しません。
        const std::string svgFileName = svgPath.filename().generic_string();
        const std::string candidateSvgFileName = "CandidateRejects.svg";
        const uint32_t safeReloadInterval = std::max(reloadIntervalMilliseconds, 50u);

        stream << "<!doctype html>\n"
               << "<html><head><meta charset=\"utf-8\"><title>Raven Physics Debug Viewer</title>"
               << "<style>"
               << "html,body{margin:0;width:100%;height:100%;background:#020617;color:#e2e8f0;font-family:sans-serif;}"
               << "body{display:flex;flex-direction:column;overflow:hidden;}"
               << "header{min-height:42px;display:flex;align-items:center;gap:12px;padding:4px 12px;background:#0f172a;font-size:13px;}"
               << ".title{font-weight:600;margin-right:auto;}"
               << ".controls{display:flex;align-items:center;gap:7px;color:#94a3b8;}"
               << ".controls input{width:82px;background:#020617;color:#e2e8f0;border:1px solid #475569;border-radius:4px;padding:4px 6px;}"
               << ".controls button{background:#1e293b;color:#e2e8f0;border:1px solid #475569;border-radius:4px;padding:4px 9px;cursor:pointer;}"
               << ".controls button:hover{background:#334155;}"
               << ".status{min-width:72px;color:#64748b;}"
               << "main{flex:1;min-height:0;display:grid;grid-template-columns:1fr 1fr;gap:2px;background:#334155;}"
               << ".pane{min-width:0;min-height:0;background:#020617;display:flex;flex-direction:column;}"
               << ".label{height:24px;padding:4px 10px;box-sizing:border-box;background:#111827;color:#94a3b8;font-size:12px;}"
               << ".view{flex:1;min-height:0;width:100%;object-fit:contain;border:0;}"
               << "</style></head>\n"
               << "<body><header><span class=\"title\">Raven Physics Browser Debug Viewer</span>"
               << "<div class=\"controls\"><label>Particle <input id=\"particleFilter\" type=\"number\" min=\"0\" placeholder=\"ALL\"></label>"
               << "<label>Triangle <input id=\"triangleFilter\" type=\"number\" min=\"0\" placeholder=\"ALL\"></label>"
               << "<button id=\"applyFilter\">Apply</button><button id=\"clearFilter\">Clear</button><span id=\"filterStatus\" class=\"status\"></span></div></header><main>"
               << "<section class=\"pane\"><div class=\"label\">Spatial Hash / Physics Snapshot</div>"
               << "<img id=\"physicsView\" class=\"view\" alt=\"Raven Physics Debug SVG\"></section>"
               << "<section class=\"pane\"><div id=\"candidateLabel\" class=\"label\">Particle-Triangle Reject Funnel</div>"
               << "<object id=\"candidateView\" class=\"view\" type=\"image/svg+xml\" aria-label=\"Raven Candidate Reject SVG\"></object></section>"
               << "</main><script>"
               << "const physicsView=document.getElementById('physicsView');"
               << "const candidateView=document.getElementById('candidateView');"
               << "const candidateLabel=document.getElementById('candidateLabel');"
               << "const particleFilter=document.getElementById('particleFilter');"
               << "const triangleFilter=document.getElementById('triangleFilter');"
               << "const filterStatus=document.getElementById('filterStatus');"
               << "const physicsSource='" << svgFileName << "';"
               << "const candidateSource='" << candidateSvgFileName << "';"
               << "const params=new URLSearchParams(window.location.search);"
               << "particleFilter.value=params.get('particle')??'';triangleFilter.value=params.get('triangle')??'';"
               << "function normalize(v){if(v==='')return '';const n=Number(v);return Number.isInteger(n)&&n>=0?String(n):'';}"
               << "function updateLabel(){const p=normalize(particleFilter.value)||'ALL';const t=normalize(triangleFilter.value)||'ALL';candidateLabel.textContent='Particle-Triangle Reject Funnel | Particle='+p+' Triangle='+t;}"
               << "function buildFilterQuery(){const p=normalize(particleFilter.value);const t=normalize(triangleFilter.value);const q=new URLSearchParams();if(p!=='')q.set('particle',p);if(t!=='')q.set('triangle',t);return q;}"
               << "function persistUrlOnly(){const q=buildFilterQuery();const suffix=q.toString();history.replaceState(null,'',window.location.pathname+(suffix?'?'+suffix:''));updateLabel();}"
               << "async function persist(){const q=buildFilterQuery();const suffix=q.toString();persistUrlOnly();filterStatus.textContent='Applying...';try{const r=await fetch('/filter'+(suffix?'?'+suffix:''),{cache:'no-store'});filterStatus.textContent=r.ok?'Applied':'Error';}catch(e){filterStatus.textContent='Offline';}}"
               << "async function clear(){particleFilter.value='';triangleFilter.value='';await persist();}"
               << "document.getElementById('applyFilter').addEventListener('click',persist);"
               << "document.getElementById('clearFilter').addEventListener('click',clear);"
               << "particleFilter.addEventListener('keydown',e=>{if(e.key==='Enter')persist();});"
               << "triangleFilter.addEventListener('keydown',e=>{if(e.key==='Enter')persist();});"
               << "window.addEventListener('message',e=>{if(e.origin!==window.location.origin)return;if(!e.data||e.data.type!=='raven-debug-filter')return;particleFilter.value=e.data.particle===null?'':String(e.data.particle);triangleFilter.value=e.data.triangle===null?'':String(e.data.triangle);persistUrlOnly();filterStatus.textContent='Selected';});"
               << "function refresh(){const stamp='?t='+Date.now();physicsView.src=physicsSource+stamp;candidateView.data=candidateSource+stamp;}"
               << "updateLabel();persist();refresh();setInterval(refresh," << safeReloadInterval << ");"
               << "</script></body></html>\n";

        return stream.good();
    }
}
