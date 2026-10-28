#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$repo_root"

required_files=(
  "AGENTS.md"
  "structure.txt"
  "Root/Root.sln"
  "Root/Root/Root.vcxproj"
  "Root/Tests/README.md"
  "Root/Tests/UITextReflowTests.cpp"
  "Root/Tests/UIFontTextureGPUIntegrationTests.cpp"
  ".gitmodules"
)

for required_file in "${required_files[@]}"; do
  if [[ ! -f "$required_file" ]]; then
    echo "[FAIL] required file is missing: $required_file" >&2
    exit 1
  fi
done

if [[ ! -f "Root/Root/vendor/imgui/imgui.cpp" ]]; then
  echo "[FAIL] Dear ImGui submodule is not initialized." >&2
  echo "       Run: git submodule update --init --recursive" >&2
  exit 1
fi

require_text() {
  local pattern="$1"
  local file="$2"
  local label="$3"
  if ! grep -Eq "$pattern" "$file"; then
    echo "[FAIL] $label: $file" >&2
    exit 1
  fi
}

require_text 'Root[\\/]Root\.vcxproj' "Root/Root.sln" "solution does not reference Root.vcxproj"
require_text '<PlatformToolset>v143</PlatformToolset>' "Root/Root/Root.vcxproj" "MSVC v143 configuration is missing"
require_text 'RavenUITest' "Root/Root/Root.vcxproj" "CPU UI test property is missing"
require_text 'RavenUIGPUTest' "Root/Root/Root.vcxproj" "GPU UI test property is missing"
require_text 'UITextReflowTests\.cpp' "Root/Root/Root.vcxproj" "CPU UI test entry point is missing"
require_text 'UIFontTextureGPUIntegrationTests\.cpp' "Root/Root/Root.vcxproj" "GPU UI test entry point is missing"
require_text 'VULKAN_SDK' "Root/Root/Root.vcxproj" "Vulkan SDK dependency is missing"
require_text 'glfw3\.lib' "Root/Root/Root.vcxproj" "GLFW static library dependency is missing"
require_text 'path[[:space:]]*=[[:space:]]*Root/Root/vendor/imgui' ".gitmodules" "Dear ImGui submodule path is missing"
require_text 'マイルストーン承認ゲート' "AGENTS.md" "milestone approval gate is missing"

echo "[PASS] Raven Codex Cloud baseline"
echo "       Static configuration only; Windows build and executable tests were not run."

