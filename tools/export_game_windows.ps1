[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectManifest,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent $scriptDirectory
$manifestPath = (Resolve-Path -LiteralPath $ProjectManifest).Path
$projectDirectory = Split-Path -Parent $manifestPath

function Get-ManifestValue([string]$Key) {
    $escapedKey = [Regex]::Escape($Key)
    $line = Get-Content -LiteralPath $manifestPath |
        Where-Object { $_ -match "^\s*$escapedKey\s*=" } |
        Select-Object -First 1
    if ($null -eq $line) { return "" }
    return ($line -replace "^[^=]*=\s*", "").Trim()
}

$projectName = Get-ManifestValue "name"
$defaultScene = Get-ManifestValue "default_scene"
if ([string]::IsNullOrWhiteSpace($projectName) -or
    [string]::IsNullOrWhiteSpace($defaultScene)) {
    throw "project.schizo must contain name and default_scene"
}

$scenePath = Join-Path $projectDirectory $defaultScene
if (-not (Test-Path -LiteralPath $scenePath -PathType Leaf)) {
    throw "Default scene not found: $scenePath. Save the scene before exporting."
}

$gameName = [Regex]::Replace($projectName, "[^A-Za-z0-9._-]+", "_")
if ([string]::IsNullOrWhiteSpace($gameName)) { $gameName = "Game" }
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$outputParent = Split-Path -Parent $outputPath
$archivePath = Join-Path $outputParent "$gameName-windows-x86_64.zip"

if (Test-Path -LiteralPath $archivePath) {
    throw "Export archive already exists: $archivePath"
}

if ((Test-Path -LiteralPath (Join-Path $repositoryRoot "CMakeLists.txt")) -and
    (Test-Path -LiteralPath (Join-Path $repositoryRoot "CMakePresets.json"))) {
    # CMake resolves CMakePresets.json from the process working directory. The
    # editor runs inside the open project, so temporarily enter the engine root.
    Push-Location -LiteralPath $repositoryRoot
    try {
        Write-Output "[1/5] Configuring the optimized Windows build"
        & cmake --preset windows-release
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

        Write-Output "[2/5] Building the Windows runtime"
        & cmake --build --preset windows-release --target editor --parallel 4
        if ($LASTEXITCODE -ne 0) { throw "Runtime build failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
    $runtimePath = Join-Path $repositoryRoot "build/windows-release/bin/editor.exe"
} else {
    Write-Output "[1/5] Using the installed Windows engine"
    Write-Output "[2/5] Reusing its release runtime"
    $runtimePath = Join-Path $repositoryRoot "editor.exe"
}

if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
    throw "Runtime build missing: $runtimePath"
}

$stagePath = Join-Path ([IO.Path]::GetTempPath()) (
    "gameworldshaper-export-" + [Guid]::NewGuid().ToString("N"))
$projectStage = Join-Path $stagePath "project"
$assetStage = Join-Path $stagePath "assets"
New-Item -ItemType Directory -Path $projectStage, $assetStage -Force | Out-Null

try {
    Write-Output "[3/5] Copying the saved project and runtime content"
    $excludedNames = @(
        "dist", "cache", "diagnostics", ".git",
        "editor.ini", "editor_layout.version"
    )
    Get-ChildItem -LiteralPath $projectDirectory -Force |
        Where-Object { $excludedNames -notcontains $_.Name } |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $projectStage -Recurse -Force
        }

    Copy-Item -LiteralPath $runtimePath -Destination (Join-Path $stagePath "$gameName.exe")
    $runtimeDirectory = Split-Path -Parent $runtimePath
    Get-ChildItem -LiteralPath $runtimeDirectory -Filter "*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $stagePath -Force }

    foreach ($assetGroup in @("skies", "scripts")) {
        $assetSource = Join-Path (Join-Path $repositoryRoot "assets") $assetGroup
        if (Test-Path -LiteralPath $assetSource -PathType Container) {
            Copy-Item -LiteralPath $assetSource -Destination $assetStage -Recurse -Force
        }
    }

    foreach ($compilerCandidate in @(
        (Join-Path $runtimeDirectory "glslangValidator.exe"),
        (Join-Path $repositoryRoot "glslangValidator.exe")
    )) {
        if (Test-Path -LiteralPath $compilerCandidate -PathType Leaf) {
            Copy-Item -LiteralPath $compilerCandidate -Destination $stagePath -Force
            break
        }
    }

    Set-Content -LiteralPath (Join-Path $stagePath "game-export.marker") `
        -Value "GameWorldshaper packaged game" -Encoding ASCII
    @(
        "$projectName - Windows export",
        "Start: $gameName.exe",
        "Scene: $defaultScene",
        "ESC releases input; click the game to resume. Close the window to quit."
    ) | Set-Content -LiteralPath (Join-Path $stagePath "README.txt") -Encoding UTF8

    Write-Output "[4/5] Preparing the portable Windows package"
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null

    Write-Output "[5/5] Creating the portable Windows archive"
    Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $archivePath -CompressionLevel Optimal
} finally {
    if (Test-Path -LiteralPath $stagePath) {
        Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
}

Write-Output ""
Write-Output "Export complete: $archivePath"
