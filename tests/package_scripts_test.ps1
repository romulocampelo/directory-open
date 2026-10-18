#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/package-support.ps1"
$unicode = 'a' + [char]0xe7 + [char]0xe3 + 'o ' + [char]0x6d4b
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('DirOpen package ' + $unicode + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$fixture/platforms" | Out-Null
$script:checks = 0
function Expect-Rejection([scriptblock]$Action, [string]$Reason) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (!$rejected) { throw "Deveria rejeitar: $Reason" }
    $script:checks++
}
Write-PackageText "$fixture/DirOpen.exe" 'fixture, never executed'
$native = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile', '-Command', '[Console]::Out.Write("output"); [Console]::Error.Write("warning"); exit 7')
if ($native.exitCode -ne 7 -or $native.stdout -ne 'output' -or $native.stderr -ne 'warning') { throw 'Native stdout/stderr/exit code lost.' }
$script:checks++
Write-PackageJson "$fixture/utf8.json" @{ text = $unicode }
if ((Read-PackageJson "$fixture/utf8.json").text -cne $unicode) { throw 'UTF-8 JSON lost.' }
$script:checks++
# Exercise CRT quoting through a real child, including spaces, Unicode, quotes and a trailing slash.
Write-PackageText "$fixture/arguments.ps1" 'param([string]$Value) [Console]::OutputEncoding=[Text.Encoding]::UTF8; [Console]::Out.Write($Value)'
$argument = $unicode + ' "quoted" end\'
$native = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$fixture/arguments.ps1",$argument)
if ($native.exitCode -ne 0 -or $native.stdout -cne $argument) { throw 'Native argument quoting lost.' }
$script:checks++
Remove-Item -LiteralPath "$fixture/arguments.ps1", "$fixture/utf8.json"
$null = Get-PackageBuildDirectory $fixture 'new build'
Expect-Rejection { Get-PackageBuildDirectory $fixture '.' } 'build at repository root'
Expect-Rejection { Get-PackageBuildDirectory $fixture '../outside' } 'build outside repository'
Expect-Rejection { Get-PackageBuildDirectory $fixture 'platforms' -RequireNew } 'explicit build already exists'
# Real owned process with its image inside a build directory must block reuse.
$liveBuild = Join-Path $fixture 'live-build'
New-Item -ItemType Directory -Path $liveBuild | Out-Null
Write-PackageText "$fixture/sleeper.ps1" 'Add-Type -TypeDefinition "public class Sleeper { public static void Main() { System.Threading.Thread.Sleep(5000); } }" -OutputType ConsoleApplication -OutputAssembly "$PSScriptRoot/live-build/sleeper.exe"'
$compiled = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$fixture/sleeper.ps1")
if ($compiled.exitCode -ne 0) { throw ('Sleeper fixture failed: '+$compiled.stderr) }
$live = [Diagnostics.Process]::new()
$live.StartInfo.FileName = "$liveBuild/sleeper.exe"
$live.StartInfo.UseShellExecute = $false; $live.StartInfo.CreateNoWindow = $true
try {
    if (!$live.Start()) { throw 'Live build fixture did not start.' }
    $guard = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$PSScriptRoot/../scripts/check-build-directory.ps1",'-Root',$fixture,'-BuildDirectory',$liveBuild)
    if ($guard.exitCode -eq 0 -or $guard.stderr -notlike '*Build em uso*') { throw ('Live build was not rejected: ' + ($guard | ConvertTo-Json -Compress)) }
    $script:checks++
    $live.WaitForExit()
} finally { $live.Dispose() }
# Only these exact owned fixture files are removed; no recursive cleanup.
Remove-Item -LiteralPath "$liveBuild/sleeper.exe", "$fixture/sleeper.ps1"
[IO.Directory]::Delete($liveBuild)
# Exercise build.cmd's actual parser in a nested Unicode path without compiling.
# A missing chosen kit must fail before configuration, not find a sibling backup script.
$repoRoot = Split-Path $PSScriptRoot -Parent
$nestedBuild = Join-Path $repoRoot ('.cache/package-contract/' + [guid]::NewGuid().ToString('N') + '/' + $unicode)
Write-PackageText "$fixture/build-failure.ps1" 'param($RepoRoot,$BuildPath) $env:NAVEGA_QT_DIR=Join-Path $BuildPath "absent-kit"; & "$RepoRoot/build.cmd" --fresh --build-dir $BuildPath; exit $LASTEXITCODE'
$failedBuild = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$fixture/build-failure.ps1",$repoRoot,$nestedBuild)
if ($failedBuild.exitCode -eq 0 -or $failedBuild.stdout -notlike '*Qt MSVC x64 nao encontrado*' -or $failedBuild.stderr) { throw ('Build parser failure: '+($failedBuild|ConvertTo-Json -Compress)) }
$script:checks++
Remove-Item -LiteralPath "$fixture/build-failure.ps1"

Write-PackageText "$fixture/platforms/qwindows.dll" 'fixture plugin'
$files = @('DirOpen.exe', 'platforms/qwindows.dll' | ForEach-Object {
    $file = Get-Item -LiteralPath (Join-Path $fixture $_)
    @{ path = $_; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
})
$manifest = @{ schemaVersion = 1; state = 'local-candidate'; publishable = $false; files = $files }
function Save-Fixture { Write-PackageJson "$fixture/package-manifest.json" $manifest }
Save-Fixture
$null = Assert-PackageIntegrity $fixture; $script:checks++
Expect-Rejection { Get-PackageRelativePath $fixture "$fixture-other/file" } 'sibling prefix'
Expect-Rejection { Get-PackageRelativePath $fixture (Join-Path $fixture '../outside') } 'parent traversal'
foreach ($invalid in @('../escape', '/absolute', 'C:/absolute', 'platforms/../DirOpen.exe', 'platforms\qwindows.dll', 'platforms//qwindows.dll', 'DirOpen.exe:stream')) {
    $manifest.files = @(@{ path = $invalid; bytes = 1; sha256 = '0' })
    Save-Fixture
    Expect-Rejection { Assert-PackageIntegrity $fixture } $invalid
}
$manifest.files = @($files[0], $files[0], $files[1]); Save-Fixture
Expect-Rejection { Assert-PackageIntegrity $fixture } 'duplicate'
$manifest.files = $files; $manifest.schemaVersion = 99; Save-Fixture
Expect-Rejection { Assert-PackageIntegrity $fixture } 'unknown schema'
$manifest.schemaVersion = 1
$manifest.files = $files; $manifest.publishable = $true; Save-Fixture
Expect-Rejection { Assert-PackageIntegrity $fixture } 'unknown release contract'
$manifest.publishable = $false; Save-Fixture
Write-PackageText "$fixture/DirOpen.exe" 'Fixture, never executed'
Expect-Rejection { Assert-PackageIntegrity $fixture } 'tampered binary'
Write-PackageText "$fixture/DirOpen.exe" 'fixture, never executed'
Set-Content -LiteralPath "$fixture/DirOpen.exe" -Stream 'unexpected' -Value 'unlisted stream'
Expect-Rejection { Assert-PackageIntegrity $fixture } 'actual ADS'
Remove-Item -LiteralPath "$fixture/DirOpen.exe" -Stream 'unexpected'
# A real junction, not just a manifest path containing a colon.
$junction = Join-Path $fixture 'linked'
$null = New-Item -ItemType Junction -Path $junction -Target "$fixture/platforms"
Expect-Rejection { Assert-PackageIntegrity $fixture } 'actual junction'
Expect-Rejection { Get-PackageBuildDirectory $fixture 'linked/build' } 'build through junction'
[IO.Directory]::Delete($junction) # Exact owned link, never recursive.
Write-PackageText "$fixture/unlisted.dll" 'unexpected dependency'
Expect-Rejection { Assert-PackageIntegrity $fixture } 'unlisted file'
# No recursive deletion: leave only owned temporary fixtures for diagnosis.
# Controlled tools exercise failure propagation, including negative Windows exit codes.
# They never configure/compile product sources and do not establish real-tool behavior.
New-Item -ItemType Directory -Path "$fixture/fake/bin", "$fixture/fake/lib/cmake/Qt6", "$fixture/fake/VC/Auxiliary/Build" | Out-Null
Write-PackageText "$fixture/fake/lib/cmake/Qt6/Qt6Config.cmake" '# controlled failure fixture'
Write-PackageText "$fixture/fake/VC/Auxiliary/Build/vcvars64.bat" "@exit /b 0`r`n"
Write-PackageText "$fixture/negative.ps1" 'Add-Type -TypeDefinition ''public class Negative { public static int Main(string[] args) { System.IO.File.AppendAllText(System.Reflection.Assembly.GetExecutingAssembly().Location + ".calls", "called\n"); return -9; } }'' -OutputType ConsoleApplication -OutputAssembly "$PSScriptRoot/fake/bin/cmake.exe"'
$compiled = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$fixture/negative.ps1")
if ($compiled.exitCode -ne 0) { throw $compiled.stderr }
Write-PackageText "$fixture/negative-build.ps1" 'param($RepoRoot,$BuildPath) $env:NAVEGA_QT_DIR="$PSScriptRoot/fake"; $env:NAVEGA_VS_DIR="$PSScriptRoot/fake"; $env:NAVEGA_NINJA="$PSScriptRoot/fake/bin/cmake.exe"; $env:PATH="$PSScriptRoot/fake/bin;"+$env:PATH; & "$RepoRoot/build.cmd" --build-dir $BuildPath; exit $LASTEXITCODE'
$failedBuild = Invoke-PackageNative (Get-Command powershell.exe).Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$fixture/negative-build.ps1",$repoRoot,$nestedBuild)
$calls=@(Get-Content -LiteralPath "$fixture/fake/bin/cmake.exe.calls")
if ($failedBuild.exitCode -eq 0 -or $calls.Count -ne 1) { throw 'Negative configure exit did not stop downstream build/test/deploy.' }
$script:checks++
# Fixture outputs deliberately retained for diagnosis outside the product tree.
$paths = @(ConvertFrom-PackageGitPaths ('z/' + $unicode + [char]0 + 'A/a' + [char]0 + 'a_A' + [char]0 + 'A/a' + [char]0))
if ($paths.Count -ne 3 -or $paths[0] -cne 'A/a' -or $paths[1] -cne 'a_A' -or $paths[2] -cne ('z/'+$unicode) -or @(ConvertFrom-PackageGitPaths '').Count -ne 0) { throw 'Git paths lost identity/order or retained empty entries.' }
$script:checks++
Write-PackageText "$fixture/source-state.ps1" 'param($RepoRoot) . "$RepoRoot/scripts/package-support.ps1"; [Console]::OutputEncoding=[Text.Encoding]::UTF8; [Console]::Out.Write(((Get-PackageSourceState $RepoRoot)|ConvertTo-Json -Compress))'
$expectedState=Get-PackageSourceState $repoRoot
foreach ($hostName in @('powershell.exe','pwsh.exe')) {
    $hostCommand=Get-Command $hostName -ErrorAction SilentlyContinue
    if (!$hostCommand) { Write-Host "$hostName not installed: cross-host check not run."; continue }
    $actual=Invoke-PackageNative $hostCommand.Source @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',"$fixture/source-state.ps1",$repoRoot)
    if ($actual.exitCode -ne 0) { throw 'Source-state child failed.' }
    $state=$actual.stdout | ConvertFrom-Json
    if ($state.fingerprint -ne $expectedState.fingerprint -or $state.fingerprintFormat -ne $expectedState.fingerprintFormat) { throw 'Source fingerprint differs between PowerShell hosts.' }
    $script:checks++
}
Write-Host "$script:checks package contract checks passed. Fixtures: $fixture"
