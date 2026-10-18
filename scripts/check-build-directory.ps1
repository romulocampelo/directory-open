#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Root, [Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/package-support.ps1"
$full = Get-PackageBuildDirectory $Root $BuildDirectory
if (Test-Path -LiteralPath $full) {
    # Read only. Never control processes discovered by PID or overwrite their images.
    foreach ($process in [Diagnostics.Process]::GetProcesses()) {
        try {
            $image = $process.MainModule.FileName
            if ($image -and $image.StartsWith($full.TrimEnd('\')+'\', [StringComparison]::OrdinalIgnoreCase)) {
                throw "Build em uso: $full"
            }
        } catch [System.ComponentModel.Win32Exception] {
            # Protected OS processes do not provide observable build ownership.
        } catch [System.InvalidOperationException] {
            # A process may finish during this read-only observation.
        } finally { $process.Dispose() }
    }
}
