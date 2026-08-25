<#
.SYNOPSIS
    Developer entry point for Wumpo.

.DESCRIPTION
    Wraps configure/build/test/format/lint so none of them require a Developer
    PowerShell window: the MSVC environment is located and entered automatically
    through vswhere.

.EXAMPLE
    .\scripts\dev.ps1 build
    .\scripts\dev.ps1 test
    .\scripts\dev.ps1 run -- --demo
    .\scripts\dev.ps1 format
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('configure', 'build', 'test', 'run', 'format', 'lint', 'clean')]
    [string]$Command = 'build',

    [ValidateSet('debug', 'release', 'asan', 'ci-core')]
    [string]$Preset = 'debug',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build\$Preset"

function Enter-MsvcEnvironment {
    # cl.exe never lands in the global PATH; it lives in the VS developer
    # environment. Entering it here keeps every other command preset-agnostic.
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio with the C++ workload; see docs/development/toolchain.md"
    }

    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) {
        throw "No Visual Studio installation with the C++ workload found; see docs/development/toolchain.md"
    }

    # VsDevCmd.bat calls vswhere by bare name, so its directory must be on PATH
    # or the shell prints a spurious "not recognized" error while still working.
    $installerDir = Split-Path -Parent $vswhere
    if ($env:PATH -notlike "*$installerDir*") { $env:PATH = "$installerDir;$env:PATH" }

    $devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    Set-Location $repoRoot
}

function Get-Tool {
    # winget adds its installs to the machine PATH, which existing shells do not
    # pick up until they are restarted. Falling back to the default install
    # locations means a fresh install works in the shell that ran it.
    param([Parameter(Mandatory = $true)][string]$Name)

    $found = Get-Command $Name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }

    $fallbacks = @(
        (Join-Path $env:ProgramFiles "LLVM\bin\$Name.exe"),
        (Join-Path $env:ProgramFiles "CMake\bin\$Name.exe")
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) { return $candidate }
    }

    throw "$Name not found. See docs/development/toolchain.md, then restart the shell."
}

function Get-SourceFiles {
    Get-ChildItem -Path (Join-Path $repoRoot 'src'), (Join-Path $repoRoot 'emulator'),
                        (Join-Path $repoRoot 'tools'), (Join-Path $repoRoot 'tests') `
                  -Include '*.cpp', '*.hpp' -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'third_party' }
}

function Invoke-Configure {
    Enter-MsvcEnvironment
    cmake --preset $Preset @Rest
    if ($LASTEXITCODE -ne 0) { Write-Output "configure failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
}

switch ($Command) {
    'configure' { Invoke-Configure }

    'build' {
        if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) { Invoke-Configure }
        Enter-MsvcEnvironment
        cmake --build --preset $Preset @Rest
        if ($LASTEXITCODE -ne 0) { Write-Output "build failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
    }

    'test' {
        Enter-MsvcEnvironment
        if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) { Invoke-Configure }
        cmake --build --preset $Preset
        if ($LASTEXITCODE -ne 0) { Write-Output "build failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
        ctest --preset $Preset @Rest
        if ($LASTEXITCODE -ne 0) { Write-Output "tests failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
    }

    'run' {
        $exe = Join-Path $buildDir 'emulator\wumpo.exe'
        if (-not (Test-Path $exe)) { throw "$exe not built yet - run: .\scripts\dev.ps1 build" }
        # Strip a leading '--' so both `run -- --demo` and `run --demo` work.
        $runArgs = if ($Rest -and $Rest[0] -eq '--') { $Rest[1..($Rest.Length - 1)] } else { $Rest }
        & $exe @runArgs
    }

    'format' {
        $files = Get-SourceFiles
        if (-not $files) { Write-Output 'No sources to format.'; break }
        $clangFormat = Get-Tool clang-format
        & $clangFormat -i --style=file @($files.FullName)
        if ($LASTEXITCODE -ne 0) { Write-Output "clang-format failed (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
        Write-Output "Formatted $($files.Count) file(s)."
    }

    'lint' {
        # clang-tidy needs compile_commands.json, which only exists after configure.
        $db = Join-Path $buildDir 'compile_commands.json'
        if (-not (Test-Path $db)) { Invoke-Configure }
        $files = Get-SourceFiles | Where-Object { $_.Extension -eq '.cpp' }
        if (-not $files) { Write-Output 'No sources to lint.'; break }
        $clangTidy = Get-Tool clang-tidy
        & $clangTidy -p $buildDir --warnings-as-errors='*' @($files.FullName)
        if ($LASTEXITCODE -ne 0) { Write-Output "clang-tidy reported problems (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
        Write-Output "Linted $($files.Count) file(s)."
    }

    'clean' {
        if (Test-Path $buildDir) {
            Remove-Item -Recurse -Force $buildDir
            Write-Output "Removed $buildDir"
        } else {
            Write-Output "Nothing to clean."
        }
    }
}
