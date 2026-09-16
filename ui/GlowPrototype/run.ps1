$prototypeRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$serverScript = Join-Path $prototypeRoot 'server.js'
$server = Start-Process -FilePath 'node' -ArgumentList @("`"$serverScript`"") -WorkingDirectory $prototypeRoot -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 700
Start-Process 'http://127.0.0.1:4173/?variant=A&state=idle'
Write-Host "Glow prototype opened. Server PID: $($server.Id)"
