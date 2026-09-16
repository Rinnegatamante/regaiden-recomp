param(
    [ValidateSet("Debug", "MinSizeRel", "Release", "RelWithDebInfo")]
    [string]$BuildType = "Release",
    [ValidateRange(1, 32)]
    [int]$Jobs = 12
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$vitaSdkRoot = [Environment]::GetEnvironmentVariable("VITASDK")
if (-not $vitaSdkRoot) {
    throw "VITASDK is not set. Install VitaSDK and set VITASDK to its root directory."
}

$toolchain = Join-Path $vitaSdkRoot "share/vita.toolchain.cmake"
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "VitaSDK toolchain was not found at '$toolchain'."
}

$buildDir = Join-Path $projectRoot "build-vita"
cmake -S $projectRoot -B $buildDir -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DGBRECOMP_GENERATED_COMPILE_JOBS=$Jobs" `
    "-DGBRECOMP_LTO_JOBS=$Jobs" `
    -DGBRECOMP_GENERATED_OPT_LEVEL=3 `
    -DGBRECOMP_ENABLE_PERFORMANCE_COUNTERS=OFF `
    -DGBRECOMP_ENABLE_IPO=ON `
    -DGBRECOMP_ENABLE_STRIP=OFF
if ($LASTEXITCODE -ne 0) {
    throw "Vita CMake configuration failed with exit code $LASTEXITCODE."
}

cmake --build $buildDir --parallel $Jobs
if ($LASTEXITCODE -ne 0) {
    throw "Vita build failed with exit code $LASTEXITCODE."
}

Write-Output "VPK: $buildDir/ResidentEvilGaiden.vpk"
