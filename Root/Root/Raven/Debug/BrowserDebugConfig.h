#pragma once

namespace Raven
{

// ============================================================================
// Browser Debug Configuration
// ============================================================================
// Browser Debug Viewerは、通常のPhysics Simulationとは独立した診断機能です。
// 特にSoftBodyのReject Snapshot生成ではBroad Phase相当の判定を低頻度で再評価し、さらにSVGを
// ファイルへ書き出すため、Profiler計測や通常プレイ時には無効化できることが重要です。
//
// このフラグをfalseにすると、次の両方をまとめて停止します。
//   1. main.cppでのViewer.html / Startup.svg生成、localhost HTTP Server起動、ブラウザ起動
//   2. SoftBodyClothDemoLayerでの100msごとのPhysics Snapshot再構築とSVGファイル出力
//
// Browser Debugを再開したい場合は、この1箇所だけtrueへ戻してください。
// Release Buildでは各呼び出し自体が_DEBUGで囲まれているため、この設定はDebug Build向けです。
inline constexpr bool kEnableBrowserDebugViewer = false;

} // namespace Raven
