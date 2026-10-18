#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/package-support.ps1"
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest = Assert-PackageIntegrity $package
# Reports/fixtures are always outside the immutable candidate and personal session.
$report = Join-Path ([IO.Path]::GetTempPath()) ('DirOpen package a' + [char]0xe7 + [char]0xe3 + 'o-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $report, "$report/left", "$report/right" | Out-Null
Write-PackageText "$report/left/demonstracao.txt" 'Directory Open: dados de demonstracao.'
$capture = Join-Path $report 'window.png'
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $package 'DirOpen.exe'
$start.WorkingDirectory = $report
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.Arguments = '--no-session --left "' + "$report/left" + '" --right "' + "$report/right" + '" --capture "' + $capture + '"'
# Change only the child's environment. Never serialize the inherited environment.
foreach ($key in @($start.EnvironmentVariables.Keys)) {
    if ($key -like 'QT_*' -or $key -like 'QML*' -or $key -like 'NAVEGA_*' -or $key -eq 'QT_ROOT_DIR') { $start.EnvironmentVariables.Remove($key) }
}
$start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
$start.EnvironmentVariables['QT_QPA_PLATFORM'] = 'windows'
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
$timer = [Diagnostics.Stopwatch]::StartNew()
$started = $false
$stdout = $null
$stderr = $null
try {
    if (!$process.Start()) { throw 'Processo do candidato nao iniciou.' }
    $started = $true
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $peakObserved = [long]0
    $observedModules = @{}
    while (!$process.WaitForExit(250)) {
        if ($timer.ElapsedMilliseconds -gt 30000) {
            # This Process owns the newly created handle; no PID-based rediscovery.
            $process.Kill(); $process.WaitForExit()
            throw 'Candidato nao encerrou em 30 segundos.'
        }
        try {
            $process.Refresh()
            $peakObserved = [Math]::Max($peakObserved, [long]$process.PeakWorkingSet64)
            $modules = @($process.Modules | ForEach-Object { @{ name = $_.ModuleName; path = $_.FileName } })
        } catch {
            if ($process.HasExited) { break }
            throw
        }
        foreach ($module in $modules) {
            if ($module.name -like 'Qt6*' -or $module.name -eq 'qwindows.dll') {
                $relative = Get-PackageRelativePath $package $module.path
                $observedModules[$relative] = $true
            }
        }
    }
    Write-PackageText "$report/stdout.txt" $stdout.GetAwaiter().GetResult()
    Write-PackageText "$report/stderr.txt" $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "Abertura falhou ($($process.ExitCode)); veja $report" }
    $result = Read-PackageJson "$capture.json"
    if (!$result.windowExposed -or !$result.captured -or $result.platform -ne 'windows' -or
        $result.appVersion -ne $manifest.version -or $result.qtVersion -ne $manifest.toolchain.qtVersion -or
        $result.leftPath.Replace('\','/') -ne "$report/left".Replace('\','/') -or
        $result.rightPath.Replace('\','/') -ne "$report/right".Replace('\','/') -or !(Test-Path -LiteralPath $capture)) {
        throw "Captura/versao/pastas divergentes; veja $report"
    }
    $null = Assert-PackageIntegrity $package
    foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll')) {
        if (!$observedModules.ContainsKey($required)) { throw "Origem do modulo nao verificada: $required" }
    }
    Write-PackageJson "$report/verification.json" @{
        schemaVersion = 1; state = 'passed'; scope = 'integrity-and-window-start-without-development-path'
        manifestSha256 = (Get-FileHash -LiteralPath "$package/package-manifest.json" -Algorithm SHA256).Hash.ToLowerInvariant()
        elapsedMs = $timer.ElapsedMilliseconds; cpuMs = $process.TotalProcessorTime.TotalMilliseconds
        peakObservedProcessWorkingSetBytes = $peakObserved
        resourceMeasurement = 'single local run; peak sampled while alive; process-family resources and OS cold cache not measured'
        observedPackageModules = @($observedModules.Keys | Sort-Object)
        independentWindows = 'not-established'; pdfMediaAndFileOperations = 'not-exercised'
    }
    Write-Host "Integridade e abertura aprovadas. Evidencia: $report"
    Write-Host 'Este teste local nao comprova outro Windows, PDF/midia/operacoes ou licencas completas.'
} finally {
    if ($started) {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        if ($stdout) { Write-PackageText "$report/stdout.txt" $stdout.GetAwaiter().GetResult() }
        if ($stderr) { Write-PackageText "$report/stderr.txt" $stderr.GetAwaiter().GetResult() }
    }
    $process.Dispose()
}
