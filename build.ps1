param([switch]$Tests, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$projectPath = $PSScriptRoot
$zigCommand = Get-Command zig -ErrorAction SilentlyContinue
if ($zigCommand) {
    $zigPath = $zigCommand.Source
} else {
    $zigPath = Join-Path $projectPath '..\toolchain\zig-windows-x86_64-0.13.0\zig.exe'
    if (-not (Test-Path -LiteralPath $zigPath)) {
        throw 'Zig not found. Install Zig 0.13.0 in PATH, or extract it to ../toolchain/zig-windows-x86_64-0.13.0.'
    }
}
$buildPath = Join-Path $projectPath 'build'
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$optimize = if ($Debug) { '-O0' } else { '-Os' }
Push-Location $projectPath
try {
    if ($Tests) {
        & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -DUNICODE -D_UNICODE -DNOMINMAX= -Isrc tests/core_tests.cpp src/core.cpp -o "$buildPath\core_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Core test compilation failed.' }
        & "$buildPath\core_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
        $qaPath = Join-Path $buildPath 'qa'
        New-Item -ItemType Directory -Force -Path $qaPath | Out-Null
        & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -DUNICODE -D_UNICODE -DNOMINMAX= -municode -Isrc tests/platform_tests.cpp src/platform.cpp src/core.cpp -luser32 -lgdi32 -o "$qaPath\platform_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Platform test compilation failed.' }
        & "$qaPath\platform_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Platform tests failed.' }
        & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -DUNICODE -D_UNICODE -DNOMINMAX= -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00 -Isrc tests/pixel_editor_tests.cpp src/pixel_editor.cpp -luser32 -lgdi32 -o "$qaPath\pixel_editor_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Pixel editor test compilation failed.' }
        & "$qaPath\pixel_editor_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Pixel editor tests failed.' }
        & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -Isrc tests/settings_session_tests.cpp src/settings_session.cpp -o "$qaPath\settings_session_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Settings-session test compilation failed.' }
        & "$qaPath\settings_session_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Settings-session tests failed.' }
    }
    Push-Location src
    try {
        & $zigPath rc /fo "$buildPath\app.res" app.rc
        if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    } finally { Pop-Location }
    if ($Tests) {
        $guiTestPath = Join-Path $buildPath 'qa\gui'
        New-Item -ItemType Directory -Force -Path $guiTestPath | Out-Null
        & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -DUNICODE -D_UNICODE -DNOMINMAX= -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00 -static -Isrc tests/pixel_editor_modal_tests.cpp "$buildPath\app.res" -luser32 -lgdi32 -o "$guiTestPath\pixel_editor_modal_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Modal editor test compilation failed.' }
        & "$guiTestPath\pixel_editor_modal_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Modal editor tests failed.' }
        & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -DUNICODE -D_UNICODE -DNOMINMAX= -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00 -static -Isrc tests/native_ui_tests.cpp src/core.cpp src/platform.cpp src/pixel_editor.cpp src/settings_session.cpp "$buildPath\app.res" -luser32 -lgdi32 -lcomctl32 -lshell32 -lcomdlg32 -lole32 -ldwmapi -lwinmm -luxtheme -o "$guiTestPath\native_ui_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Native property-sheet test compilation failed.' }
        & "$guiTestPath\native_ui_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Native property-sheet tests failed.' }
    }
    & $zigPath c++ -target x86_64-windows-gnu -std=c++17 $optimize -DNDEBUG -DUNICODE -D_UNICODE -DNOMINMAX= -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00 -municode '-Wl,--subsystem,windows' -static -Isrc src/main.cpp src/core.cpp src/platform.cpp src/pixel_editor.cpp src/settings_session.cpp "$buildPath\app.res" -luser32 -lgdi32 -lcomctl32 -lshell32 -lcomdlg32 -lole32 -ldwmapi -lwinmm -luxtheme -o "$buildPath\HuntCrosshair-v1.9.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    Get-Item -LiteralPath "$buildPath\HuntCrosshair-v1.9.exe" | Select-Object FullName,Length
} finally { Pop-Location }
