param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [Parameter(Mandatory=$true)][string]$BackendBuild,
    [switch]$SkipBackend,
    [string]$PackageSource
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$BackendBuild=(Resolve-Path -LiteralPath $BackendBuild).Path
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio with Desktop C++ tools is required.' }
$msbuild=Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (!$SkipBackend) {
& cmake --build $BackendBuild --config $Configuration --target guardian_engine_client --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Service client compilation failed.' }
}
& python -B (Join-Path $root 'tools\embed_languages.py') (Join-Path $root 'config\languages') (Join-Path $root 'build-phase17\generated\languages.cpp')
if ($LASTEXITCODE -ne 0) { throw 'Language validation failed.' }
$includeFile=Join-Path $BackendBuild 'ui17-json-include.txt'
if (Test-Path -LiteralPath $includeFile) { $includes=(Get-Content -LiteralPath $includeFile -Raw).Split(';') }
else {
$project=Join-Path $BackendBuild 'guardian_engine_client.vcxproj'
[xml]$native=Get-Content -LiteralPath $project
$includes=($native.Project.ItemDefinitionGroup.ClCompile.AdditionalIncludeDirectories | Select-Object -First 1).Split(';')
}
$jsonInclude=$includes | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'nlohmann\json.hpp') } | Select-Object -First 1
if (!$jsonInclude) { throw 'JSON headers are missing from the backend build.' }
$sourceArgs=@()
if ($PackageSource) { $sourceArgs+=('/p:RestoreSources='+$PackageSource) }
& $msbuild (Join-Path $root 'src\winui\Aegis.vcxproj') /restore /m:2 /p:NuGetAudit=false /p:RestoreIgnoreFailedSources=true /p:Configuration=$Configuration /p:Platform=x64 "/p:GuardianBuild=$BackendBuild" "/p:GuardianJsonInclude=$jsonInclude" @sourceArgs /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'WinUI compilation failed.' }
