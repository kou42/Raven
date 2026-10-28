[CmdletBinding()]
param(
    [switch]$InstallVulkanSdk
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$GlfwVersion = "3.4"
$GlfwSha256 = "54efa829400f2a0537f742b2b3bdd74e437bb4f2f048e4b7d3c5557d11a611e6"
$VulkanSdkVersion = "1.4.363.0"
$VulkanSdkSha256 = "94a82d378f7a5e3e54c9db7d2fb7016af136e14ac0a18dbf0f2f67a36352d141"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$CacheRoot = if ($env:RUNNER_TEMP) {
    Join-Path $env:RUNNER_TEMP "raven-build-cache"
} else {
    Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "RavenBuildCache"
}
New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null

function Add-BuildPath {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Build path does not exist: $Path"
    }

    $env:Path = "$Path;$env:Path"
    if ($env:GITHUB_PATH) {
        Add-Content -LiteralPath $env:GITHUB_PATH -Value $Path -Encoding utf8
    }
}

function Set-BuildEnvironment {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Value
    )

    Set-Item -Path "Env:$Name" -Value $Value
    if ($env:GITHUB_ENV) {
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "$Name=$Value" -Encoding utf8
    }
}

function Get-VerifiedDownload {
    param(
        [Parameter(Mandatory)][uri]$Uri,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$Sha256
    )

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $actual = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -eq $Sha256.ToLowerInvariant()) {
            Write-Host "Using verified cache: $Destination"
            return
        }

        Remove-Item -LiteralPath $Destination -Force
    }

    Write-Host "Downloading $Uri"
    Invoke-WebRequest -Uri $Uri -OutFile $Destination -UseBasicParsing
    $downloadedHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($downloadedHash -ne $Sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $Destination -Force
        throw "SHA-256 mismatch for $Uri. Expected $Sha256, got $downloadedHash"
    }
}

function Find-MsBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "Visual Studio Installer (vswhere.exe) was not found. Install Visual Studio 2022 Build Tools."
    }

    $installationPath = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installationPath) {
        throw "Visual Studio 2022 with MSBuild and the MSVC v143 C++ toolset was not found."
    }

    $candidate = Join-Path ($installationPath | Select-Object -First 1) "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "MSBuild.exe was not found below $installationPath"
    }

    return $candidate
}

function Ensure-GlfwLibrary {
    $targetDirectory = Join-Path $RepoRoot "Root\Root\lib"
    $targetLibrary = Join-Path $targetDirectory "glfw3.lib"
    if (Test-Path -LiteralPath $targetLibrary -PathType Leaf) {
        Write-Host "GLFW import library already exists: $targetLibrary"
        return
    }

    $archive = Join-Path $CacheRoot "glfw-$GlfwVersion.bin.WIN64.zip"
    $archiveUri = "https://github.com/glfw/glfw/releases/download/$GlfwVersion/glfw-$GlfwVersion.bin.WIN64.zip"
    Get-VerifiedDownload -Uri $archiveUri -Destination $archive -Sha256 $GlfwSha256

    $extractRoot = Join-Path $CacheRoot "glfw-$GlfwVersion"
    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force

    $sourceLibrary = Join-Path $extractRoot "glfw-$GlfwVersion.bin.WIN64\lib-vc2022\glfw3.lib"
    if (-not (Test-Path -LiteralPath $sourceLibrary -PathType Leaf)) {
        throw "The GLFW $GlfwVersion archive did not contain lib-vc2022\glfw3.lib"
    }

    New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    Copy-Item -LiteralPath $sourceLibrary -Destination $targetLibrary -Force
    Write-Host "Installed GLFW $GlfwVersion library: $targetLibrary"
}

function Test-VulkanSdk {
    param([string]$Root)

    if (-not $Root) { return $false }
    return (
        (Test-Path -LiteralPath (Join-Path $Root "Bin\glslc.exe") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $Root "Include\vulkan\vulkan.h") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $Root "Lib\vulkan-1.lib") -PathType Leaf)
    )
}

function Ensure-VulkanSdk {
    $sdkRoot = $env:VULKAN_SDK
    if (-not $InstallVulkanSdk -and (Test-VulkanSdk -Root $sdkRoot)) {
        Write-Host "Using existing Vulkan SDK: $sdkRoot"
    } else {
        if (-not $InstallVulkanSdk) {
            throw "A complete Vulkan SDK was not found in VULKAN_SDK. Re-run with -InstallVulkanSdk to install the pinned SDK."
        }

        $sdkRoot = if ($env:RUNNER_TEMP) {
            Join-Path $env:RUNNER_TEMP "VulkanSDK\$VulkanSdkVersion"
        } else {
            Join-Path $CacheRoot "VulkanSDK\$VulkanSdkVersion"
        }

        if (-not (Test-VulkanSdk -Root $sdkRoot)) {
            $installer = Join-Path $CacheRoot "vulkansdk-windows-X64-$VulkanSdkVersion.exe"
            $installerUri = "https://sdk.lunarg.com/sdk/download/$VulkanSdkVersion/windows/vulkan_sdk.exe"
            Get-VerifiedDownload -Uri $installerUri -Destination $installer -Sha256 $VulkanSdkSha256

            Write-Host "Installing Vulkan SDK $VulkanSdkVersion into $sdkRoot"
            & $installer --root $sdkRoot --accept-licenses --default-answer --confirm-command install copy_only=1
            if ($LASTEXITCODE -ne 0) {
                throw "Vulkan SDK installer failed with exit code $LASTEXITCODE"
            }
        }

        if (-not (Test-VulkanSdk -Root $sdkRoot)) {
            throw "Vulkan SDK $VulkanSdkVersion installation is incomplete: $sdkRoot"
        }
    }

    Set-BuildEnvironment -Name "VULKAN_SDK" -Value $sdkRoot
    Set-BuildEnvironment -Name "VK_SDK_PATH" -Value $sdkRoot
    Add-BuildPath -Path (Join-Path $sdkRoot "Bin")
    return $sdkRoot
}

function Find-Dxc {
    $command = Get-Command dxc.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $windowsKitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $windowsKitsBin -PathType Container) {
        $candidate = Get-ChildItem -LiteralPath $windowsKitsBin -Filter dxc.exe -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\dxc\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            Add-BuildPath -Path $candidate.DirectoryName
            return $candidate.FullName
        }
    }

    throw "dxc.exe was not found. Install a Windows 10/11 SDK that includes the DirectX Shader Compiler."
}

$msbuild = Find-MsBuild
Ensure-GlfwLibrary
$vulkanRoot = Ensure-VulkanSdk
$dxc = Find-Dxc

Write-Host "Windows build prerequisites are ready."
Write-Host "  MSBuild: $msbuild"
Write-Host "  Vulkan SDK: $vulkanRoot"
Write-Host "  DXC: $dxc"


