$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$outputPath = Join-Path $projectRoot 'bin'
$compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw 'The Visual Studio C# compiler was not found.'
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $outputPath 'Assets') | Out-Null

$references = @(
    'C:\Windows\Microsoft.NET\assembly\GAC_64\PresentationCore\v4.0_4.0.0.0__31bf3856ad364e35\PresentationCore.dll'
    'C:\Windows\Microsoft.NET\assembly\GAC_MSIL\PresentationFramework\v4.0_4.0.0.0__31bf3856ad364e35\PresentationFramework.dll'
    'C:\Windows\Microsoft.NET\assembly\GAC_MSIL\WindowsBase\v4.0_4.0.0.0__31bf3856ad364e35\WindowsBase.dll'
    'C:\Windows\Microsoft.NET\assembly\GAC_MSIL\System.Xaml\v4.0_4.0.0.0__b77a5c561934e089\System.Xaml.dll'
    'C:\Windows\Microsoft.NET\assembly\GAC_MSIL\System.Speech\v4.0_4.0.0.0__31bf3856ad364e35\System.Speech.dll'
)

foreach ($reference in $references) {
    if (-not (Test-Path -LiteralPath $reference)) {
        throw "Required WPF assembly was not found: $reference"
    }
}

$responsePath = Join-Path $outputPath 'compile.rsp'
$responseLines = @(
    '/target:winexe'
    '/platform:anycpu'
    '/langversion:latest'
    '/nullable:enable'
    "/win32manifest:`"$(Join-Path $projectRoot 'app.manifest')`""
    "/out:`"$(Join-Path $outputPath 'PixelMini.exe')`""
)
$responseLines += $references | ForEach-Object { "/reference:`"$_`"" }
$responseLines += @(
    "`"$(Join-Path $projectRoot 'Program.cs')`""
    "`"$(Join-Path $projectRoot 'PixelWindow.cs')`""
    "`"$(Join-Path $projectRoot 'Json.cs')`""
    "`"$(Join-Path $projectRoot 'Telemetry.cs')`""
    "`"$(Join-Path $projectRoot 'PixelStateArbiter.cs')`""
    "`"$(Join-Path $projectRoot 'Chat.cs')`""
    "`"$(Join-Path $projectRoot 'VoiceAssistant.cs')`""
)
Set-Content -LiteralPath $responsePath -Value $responseLines -Encoding UTF8

& $compiler "@$responsePath"
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed with exit code $LASTEXITCODE."
}

$staleRuntimeConfig = Join-Path $outputPath 'PixelMini.runtimeconfig.json'
if (Test-Path -LiteralPath $staleRuntimeConfig) {
    Remove-Item -LiteralPath $staleRuntimeConfig -Force
}
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Assets') -Filter '*.png' |
    Copy-Item -Destination (Join-Path $outputPath 'Assets') -Force

Write-Host "Built $(Join-Path $outputPath 'PixelMini.exe')"
