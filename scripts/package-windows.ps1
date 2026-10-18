#requires -Version 5.1
[CmdletBinding()]
param([string]$BuildDirectory = 'build')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot/package-support.ps1"
$build = Get-PackageBuildDirectory $repo $BuildDirectory
# The build owner rejects live images; callers can first validate a new build,
# then package it incrementally after freezing the launcher and documentation.
$run = Join-Path $repo ('.cache/packages/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-PackageJson "$run/state.json" @{ state = 'incomplete'; phase = 'build'; publishable = $false }

Push-Location $repo
try {
    $before = Get-PackageSourceState $repo
    Write-PackageJson "$run/source-before.json" $before
    # Always compile and test. There is deliberately no unchecked reuse mode.
    & "$repo/build.cmd" --build-dir $build
    if ($LASTEXITCODE -ne 0) { throw 'Build/testes falharam; pacote nao criado.' }
    $after = Get-PackageSourceState $repo
    Write-PackageJson "$run/source-after.json" $after
    if ($before.fingerprint -ne $after.fingerprint) { throw 'Fontes mudaram durante o build; execute novamente.' }
    $info = Read-PackageJson "$build/deployment-info.json"
    if ($info.schemaVersion -ne 1 -or $info.configuration -ne 'Release' -or
        $info.compiler -ne 'MSVC' -or $info.architecture -ne 'x64') {
        throw 'Empacotamento requer build Release MSVC x64.'
    }
    $qtRoot = [IO.Path]::GetFullPath((Join-Path $info.qtBin '..'))
    $deploy = Join-Path $info.qtBin 'windeployqt.exe'
    $qtpaths = Join-Path $info.qtBin 'qtpaths6.exe'
    if (!(Test-Path -LiteralPath $qtpaths)) { $qtpaths = Join-Path $info.qtBin 'qtpaths.exe' }
    $qtVersion = (& $qtpaths --qt-version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $qtVersion -ne $info.qtVersion) { throw 'Kit Qt inconsistente com o build.' }

    $name = "DirectoryOpen-$($info.appVersion)-windows-x64-LOCAL-CANDIDATE"
    $package = Join-Path $run $name
    Write-PackageJson "$run/state.json" @{ state = 'incomplete'; phase = 'deploy'; publishable = $false }
    New-Item -ItemType Directory -Path $package | Out-Null
    # Never overwrite/delete another candidate. Keep partial output on any failure.
    Copy-Item -LiteralPath "$build/DirOpen.exe" -Destination $package
    $deployArgs = @('--release', '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw',
        '--no-compiler-runtime', '--qtpaths', $qtpaths, '--dir', $package, '--json', "$package/DirOpen.exe")
    $deployResult = Invoke-PackageNative $deploy $deployArgs
    Write-PackageText "$run/deployment-stderr.txt" $deployResult.stderr
    if ($deployResult.exitCode -ne 0) { throw "windeployqt falhou; veja $run" }
    $deploymentText = $deployResult.stdout
    $deployment = $deploymentText | ConvertFrom-Json
    Write-PackageText "$run/deployment.json" $deploymentText
    $origins = @{}
    foreach ($entry in $deployment.files) {
        # Qt's JSON 'target' is the destination DIRECTORY, not the final filename.
        $target = Join-Path $entry.target ([IO.Path]::GetFileName($entry.source))
        $relative = Get-PackageRelativePath $package $target
        $source = Get-Item -LiteralPath $entry.source
        if ($origins.ContainsKey($relative)) { throw "Destino duplicado no deployment: $relative" }
        $origin = if ($source.FullName.StartsWith($qtRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            'qt/' + (Get-PackageRelativePath $qtRoot $source.FullName)
        } else { 'external/' + $source.Name }
        $origins[$relative] = @{ origin = $origin; sourceSha256 = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    foreach ($required in @('Qt6Core.dll', 'Qt6Widgets.dll', 'Qt6Multimedia.dll', 'Qt6MultimediaWidgets.dll', 'Qt6Pdf.dll', 'Qt6PrintSupport.dll',
            'platforms/qwindows.dll', 'imageformats/qjpeg.dll', 'multimedia/ffmpegmediaplugin.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) { throw "Dependencia ausente: $required" }
    }
    if (@(Get-ChildItem -LiteralPath $package -Filter 'avcodec-*.dll').Count -eq 0) { throw 'FFmpeg nao foi implantado.' }
    Copy-Item -LiteralPath "$repo/LICENSE" -Destination "$package/LICENSE.txt"
    New-Item -ItemType Directory -Path "$package/docs", "$package/legal/qt-sbom" | Out-Null
    Copy-Item -LiteralPath "$repo/docs/USO.md", "$repo/docs/LANCAMENTOS.md", "$repo/docs/AVISOS-TERCEIROS.md", "$repo/docs/VALIDACAO-WINDOWS-INDEPENDENTE.md" -Destination "$package/docs"
    Copy-Item -LiteralPath "$repo/packaging/LEIA-ME-TESTE.md" -Destination "$package/LEIA-ME.md"
    # qt.conf pins lookup to this candidate when Qt was built with absolute paths.
    Write-PackageText "$package/qt.conf" "[Paths]`nPrefix=.`nPlugins=.`n"
    $sboms = @(Get-ChildItem -LiteralPath "$qtRoot/sbom" -Filter '*.spdx.json' -ErrorAction SilentlyContinue)
    $sbomNames = @()
    foreach ($sbom in $sboms) {
        $data = Read-PackageJson $sbom.FullName
        # Keep full producer documents for modules containing a deployed file.
        # Their package lists may include unused/debug/build-only components.
        $used = @($data.files | Where-Object {
            $candidateOrigin = 'qt/' + ($_.fileName -replace '^\./', '')
            @($origins.Values | Where-Object { $_.origin -eq $candidateOrigin }).Count -gt 0
        }).Count -gt 0
        if ($used) {
            Copy-Item -LiteralPath $sbom.FullName -Destination "$package/legal/qt-sbom"
            $sbomNames += $sbom.Name
        }
    }
    $files = @(Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = Get-PackageRelativePath $package $_.FullName
        $origin = if ($relative -eq 'DirOpen.exe') { @{ origin = 'application/build' } }
            elseif ($origins.ContainsKey($relative)) { $origins[$relative] }
            else { @{ origin = 'package/documentation-or-metadata' } }
        [ordered]@{ path = $relative; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); provenance = $origin }
    })
    $manifest = [ordered]@{
        schemaVersion = 1; product = 'Directory Open'; version = $info.appVersion
        state = 'local-candidate'; publishable = $false; createdUtc = [DateTime]::UtcNow.ToString('o')
        source = $after; build = @{ directory = Get-PackageRelativePath $repo $build; presentationProbe = $false; sourceBefore = $before; sourceAfter = $after }
        toolchain = @{ qtVersion = $info.qtVersion; qtBin = $info.qtBin; compiler = $info.compiler; compilerVersion = $info.compilerVersion; architecture = $info.architecture
            deploymentInfoSha256 = (Get-FileHash -LiteralPath "$build/deployment-info.json" -Algorithm SHA256).Hash.ToLowerInvariant() }
        verification = @{ buildAndCTest = 'passed'; independentWindows = 'not-run'; packagedRuntime = 'not-run' }
        prerequisites = @('Windows x64 compativel com o Qt usado', 'Microsoft Visual C++ Redistributable x64 compativel, instalado separadamente')
        pending = @('Revisar avisos/licencas de todos os componentes reais, incluindo FFmpeg e eventuais DLLs externas',
            'Fornecer textos, atribuicoes, fontes correspondentes/instrucoes e demais requisitos da licenca escolhida',
            'Validar este pacote em Windows sem Qt/Visual Studio')
        qtProducerSboms = $sbomNames; files = $files
    }
    # Deployment/document copying also consumes inventoried inputs. Never patch a stale manifest.
    if ((Get-PackageSourceState $repo).fingerprint -ne $after.fingerprint) { throw 'Fontes mudaram durante o empacotamento; gere candidato novo.' }
    Write-PackageJson "$package/package-manifest.json" $manifest
    $null = Assert-PackageIntegrity $package
    $zip = "$run/$name.zip"
    Write-PackageJson "$run/state.json" @{ state = 'incomplete'; phase = 'zip'; publishable = $false }
    Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
    $zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-PackageText "$zip.sha256" "$zipHash  $name.zip`n"
    Write-PackageJson "$run/state.json" @{ state = 'candidate-created'; publishable = $false; package = $name; zipSha256 = $zipHash }
    Write-Host "Candidato LOCAL (nao publicar): $zip"
    Write-Host "Validar: & ./scripts/verify-package.ps1 -PackageDirectory '$package'"
} catch {
    Write-PackageJson "$run/state.json" @{ state = 'incomplete'; publishable = $false; error = $_.Exception.Message }
    throw
} finally { Pop-Location }
