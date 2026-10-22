param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [switch]$FullBuild
)
$ErrorActionPreference='Stop'
$kernelRoot=$PSScriptRoot
$kernelBuild=Join-Path $kernelRoot 'build-codex'
Push-Location -LiteralPath $kernelRoot
try {
    & cmake -S $kernelRoot -B $kernelBuild -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed.' }
    if ($FullBuild) {
        & cmake --build $kernelBuild --config $Configuration --parallel 2
    } else {
        & cmake --build $kernelBuild --config $Configuration --target guardian_reference --parallel 2
    }
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    & cmake --build $kernelBuild --config $Configuration --target guardian_guest --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Freestanding guest contract/image audit failed.' }
    & ctest --test-dir $kernelBuild -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Kernel regression tests failed.' }
    Write-Host ('Kernel tools: '+(Join-Path $kernelBuild $Configuration))
    Write-Host 'A skipped WHP test means virtualization was unavailable; no Windows settings are changed.'
} finally { Pop-Location }
