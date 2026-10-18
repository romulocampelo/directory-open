# Shared package boundaries; no filesystem/configuration writes when dot-sourced.
# Load the host's own Utility module explicitly: Windows PowerShell's script
# functions (Get-FileHash) are not reliably auto-loaded inside a CTest pipeline.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
function Write-PackageText([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Write-PackageJson([string]$Path, $Value) {
    Write-PackageText $Path ($Value | ConvertTo-Json -Depth 12)
}
function Read-PackageJson([string]$Path) {
    # The producer writes UTF-8 without BOM; PS 5.1 defaults to the ANSI code page.
    Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}
function Get-PackageBuildDirectory([string]$Root, [string]$Path = 'build', [switch]$RequireNew) {
    $full = if ([IO.Path]::IsPathRooted($Path)) { [IO.Path]::GetFullPath($Path) }
        else { [IO.Path]::GetFullPath((Join-Path $Root $Path)) }
    $null = Get-PackageRelativePath $Root $full
    # Existing parents must be ordinary directories, not aliases into another tree.
    $parent = $full
    while ($parent) {
        if (Test-Path -LiteralPath $parent) {
            $item = Get-Item -LiteralPath $parent -Force
            if (!$item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Build requer diretorio normal: $parent"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($parent)
    }
    if ($RequireNew -and (Test-Path -LiteralPath $full)) { throw "Build novo ja existe: $full" }
    return $full
}
function ConvertTo-PackageNativeArgument([string]$Value) {
    # Windows CommandLineToArgvW/CRT escaping, including trailing backslashes.
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Invoke-PackageNative([string]$Executable, [string[]]$Arguments, [int]$TimeoutMs = 60000) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = ($Arguments | ForEach-Object { ConvertTo-PackageNativeArgument $_ }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (!$process.Start()) { throw "Ferramenta nao iniciou: $Executable" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutMs)) {
            $process.Kill(); $process.WaitForExit()
            throw "Ferramenta excedeu $TimeoutMs ms: $Executable"
        }
        @{ exitCode = $process.ExitCode; stdout = $stdout.GetAwaiter().GetResult(); stderr = $stderr.GetAwaiter().GetResult() }
    } finally { $process.Dispose() }
}
function Get-PackageRelativePath([string]$Root, [string]$Path) {
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $full = [IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Caminho fora do pacote: $Path" }
    $full.Substring($prefix.Length).Replace('\', '/')
}
function ConvertFrom-PackageGitPaths([string]$Text) {
    # Explicit char[] selects the same overload on .NET Framework and .NET.
    # Split(char, options) is absent on Framework; its params overload kept a
    # trailing empty entry and invented a deleted path at the repository root.
    $unique = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $Text.Split([char[]]@([char]0), [StringSplitOptions]::RemoveEmptyEntries)) {
        $null = $unique.Add($path)
    }
    [string[]]$paths = @($unique)
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    return $paths
}
function Get-PackageSourceState([string]$Root) {
    $commit = (& git -C $Root rev-parse HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Git HEAD indisponivel.' }
    $inventory = Invoke-PackageNative (Get-Command git.exe).Source @('-C', $Root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard')
    if ($inventory.exitCode -ne 0) { throw 'Inventario Git indisponivel.' }
    $entries = foreach ($path in (ConvertFrom-PackageGitPaths $inventory.stdout)) {
        $full = Join-Path $Root $path
        $hash = if (Test-Path -LiteralPath $full -PathType Leaf) { (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash } else { 'deleted' }
        "$path`t$hash"
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $fingerprint = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($commit + "`n" + ($entries -join "`n"))))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $status = @(& git -C $Root status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'Estado Git indisponivel.' }
    [ordered]@{ commit = $commit; dirty = $status.Count -gt 0; fingerprint = $fingerprint; fingerprintFormat = 'sha256(commit LF ordinal-sorted-git-path TAB file-sha256-uppercase LF ...), UTF-8, no-final-LF' }
}
function Assert-PackageIntegrity([string]$Root) {
    $rootItem = Get-Item -LiteralPath $Root
    $all = @($rootItem) + @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
    if (@($all | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Links/juncoes nao sao permitidos no candidato.' }
    $manifest = Read-PackageJson (Join-Path $Root 'package-manifest.json')
    if ($manifest.schemaVersion -ne 1 -or $manifest.state -ne 'local-candidate' -or $manifest.publishable -ne $false) { throw 'Contrato de candidato desconhecido.' }
    $seen = @{}
    foreach ($entry in $manifest.files) {
        $relative = [string]$entry.path
        if (!$relative -or $relative.Contains('\') -or $relative -match '(^/|:|(^|/)\.\.?(/|$))' -or $seen.ContainsKey($relative)) { throw "Caminho invalido/duplicado: $relative" }
        $full = Join-Path $Root $relative
        if ((Get-PackageRelativePath $Root $full) -cne $relative) { throw "Caminho nao canonico: $relative" }
        $file = Get-Item -LiteralPath $full
        if ($file.PSIsContainer -or $file.Length -ne $entry.bytes -or
            (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Integridade divergente: $relative" }
        $seen[$relative] = $true
    }
    foreach ($file in ($all | Where-Object { !$_.PSIsContainer })) {
        if (@(Get-Item -LiteralPath $file.FullName -Stream * | Where-Object { $_.Stream -ne ':$DATA' }).Count) {
            throw 'Fluxo ADS nao inventariado no candidato.'
        }
        $relative = Get-PackageRelativePath $Root $file.FullName
        if ($relative -ne 'package-manifest.json' -and !$seen.ContainsKey($relative)) { throw "Arquivo nao inventariado: $relative" }
    }
    if (!$seen.ContainsKey('DirOpen.exe') -or !$seen.ContainsKey('platforms/qwindows.dll')) { throw 'Inventario sem aplicativo/plataforma.' }
    return $manifest
}
