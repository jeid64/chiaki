
# RUN THIS FILE IN DEVELOPER COMMAND PROMPT FOR VISUAL STUDIO x64 TO SET ENV VARS CORRECTLY
# Copy fmmpeg-prefix from appveyor into repo root
# Install Qt 5.15.2

$BUILD_ROOT = Get-Location
$START_PATH = $env:Path
$PYTHON = (Get-Command "python").Source

Write-Output "BUILD_ROOT=$BUILD_ROOT"

$BUILD_ROOT = Get-Location
$ninjaDir = "ninja"
$yasmDir = "yasm"

$QT_PATH = "C:\\Qt\\5.15.2\\msvc2019_64"

if (!(Test-Path -Path $PYTHON))
{
	Write-Output '$PYTHON NOT FOUND AT ' + "${PYTHON}, aborting..."
	Exit 1
}

if (!(Test-Path -Path $QT_PATH))
{
	Write-Output '$QT_PATH NOT FOUND AT ' + "${QT_PATH}, aborting..."
	Exit 1
}

Write-Output "checking ninja..."
if (!(Test-Path -Path "${BUILD_ROOT}\\${ninjaDir}\\ninja.exe"))
{
	Write-Output "ninja not found, fetching ninja..."
	New-Item -Path $BUILD_ROOT -Name $ninjaDir -ItemType "directory"
	Invoke-WebRequest -Uri "https://github.com/ninja-build/ninja/releases/download/v1.9.0/ninja-win.zip" -OutFile "${ninjaDir}\\ninja-win.zip"
	Expand-Archive -Path "${ninjaDir}\\ninja-win.zip" -DestinationPath "${ninjaDir}"
	Remove-Item -Path "${ninjaDir}\\ninja-win.zip"
}

Write-Output "checking yasm..."
if (!(Test-Path -Path "${BUILD_ROOT}\\${yasmDir}\\yasm.exe"))
{
	Write-Output "yasm not found, fetching yasm..."
	New-Item -Path $BUILD_ROOT -Name $yasmDir -ItemType "directory"
	Invoke-WebRequest -Uri "http://www.tortall.net/projects/yasm/releases/yasm-1.3.0-win64.exe" -OutFile "${yasmDir}\\yasm.exe"
}

$env:Path += ";${BUILD_ROOT}\\$ninjaDir;${BUILD_ROOT}\\${yasmDir};${QT_PATH}\\bin"

Write-Output "fetching opus..."
git clone https://github.com/xiph/opus.git
Set-Location -Path "opus"
git checkout ad8fe90db79b7d2a135e3dfd2ed6631b0c5662ab

New-Item -ErrorAction Ignore -Path "build" -ItemType "directory"
Set-Location -Path "build"

Write-Output "building opus..."
cmake `
	-G Ninja `
	-DCMAKE_C_COMPILER=cl `
	-DCMAKE_BUILD_TYPE=Release `
	-DCMAKE_INSTALL_PREFIX="${BUILD_ROOT}\\opus-prefix" `
	..
ninja
ninja install

Set-Location -Path "${BUILD_ROOT}"

Write-Output "checking openssl..."
if (!(Test-Path -Path "${BUILD_ROOT}\\openssl-1.1"))
{
	Write-Output "openssl not found, fetching openssl..."
	Invoke-WebRequest -Uri "https://mirror.firedaemon.com/OpenSSL/openssl-1.1.1n.zip" -OutFile "openssl-1.1.1n.zip"
	Expand-Archive -Path "openssl-1.1.1n.zip" -DestinationPath .
	Remove-Item -Path "openssl-1.1.1n.zip"
}


#Delete and expand SDL root every build because otherwise cmake config will get confused.
$SDL_ROOT = "$BUILD_ROOT\\SDL2-2.0.10"
Write-Output "checking sdl2-zip..."
if (!(Test-Path -Path "$BUILD_ROOT\\SDL2-devel-2.0.10-VC.zip"))
{
	Write-Output "sdl2-zip not found, fetching sdl2..."
	Invoke-WebRequest -Uri "https://www.libsdl.org/release/SDL2-devel-2.0.10-VC.zip" -OutFile "SDL2-devel-2.0.10-VC.zip"
}
if (Test-Path -Path $SDL_ROOT)
{
	Remove-Item -Force -Recurse -Path $SDL_ROOT
}
Write-Output "unpacking sdl2..."
Expand-Archive -Path "SDL2-devel-2.0.10-VC.zip" -DestinationPath .

New-Item -Path "${SDL_ROOT}\\SDL2Config.cmake"

Write-Output "checking protoc..."
if (!(Test-Path -Path "$BUILD_ROOT\\protoc"))
{
	Write-Output "protoc not found, fetching protoc..."
	New-Item -Path $BUILD_ROOT -Name "protoc" -ItemType "directory"
	Set-Location -Path "protoc"
	Invoke-WebRequest -Uri "https://github.com/protocolbuffers/protobuf/releases/download/v3.9.1/protoc-3.9.1-win64.zip" -OutFile "protoc-3.9.1-win64.zip"
	Expand-Archive -Path "protoc-3.9.1-win64.zip" -DestinationPath .
	Remove-Item -Path "protoc-3.9.1-win64.zip"
}
Set-Location -Path "${BUILD_ROOT}"

$env:Path += ";${BUILD_ROOT}\\protoc\\bin"

Write-Output "installing python-protobuf..."
python -m pip install protobuf

New-Item -ErrorAction Ignore -Path "build" -ItemType "directory"
Set-Location -Path "build"

Write-Output "building chiaki..."
cmake `
	-G Ninja `
	-DCMAKE_C_COMPILER=cl `
	-DCMAKE_C_FLAGS="-we4013" `
	-DCMAKE_BUILD_TYPE=RelWithDebInfo `
	-DCMAKE_PREFIX_PATH="${BUILD_ROOT}\\ffmpeg-prefix;${BUILD_ROOT}\\opus-prefix;${BUILD_ROOT}\\openssl-1.1\\x64;${QT_PATH};${SDL_ROOT}" `
	-DPYTHON_EXECUTABLE="${PYTHON}" `
	-DCHIAKI_ENABLE_TESTS=ON `
	-DCHIAKI_ENABLE_CLI=OFF `
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON `
	-DSDL2_INCLUDE_DIRS="${SDL_ROOT}\\include" `
	-DSDL2_LIBRARIES="${SDL_ROOT}\\lib\\x64\\SDL2.lib" `
	-DSDL2_LIBDIR="${SDL_ROOT}\\lib\\x64" `
	-DFFMPEG_INCLUDE_DIRS="${BUILD_ROOT}\\ffmpeg-prefix\\include" `
	..

ninja
Write-Output "running Tests...."
.\test\chiaki-unit.exe 

Set-Location -Path $BUILD_ROOT

Write-Output "deploying...." 
New-Item -ErrorAction Ignore -Path "Chiaki" -ItemType "directory"
Copy-Item -Force -Path "${BUILD_ROOT}\\build\\gui\\chiaki.exe" -Destination "Chiaki"
Copy-Item -Force -Path "${BUILD_ROOT}\\build\\gui\\chiaki.pdb" -Destination "Chiaki"

"${QT_PATH}\\bin\\windeployqt.exe Chiaki\\chiaki.exe"

Copy-Item -Force -Path "${BUILD_ROOT}\\openssl-1.1\\x64\\bin\\libcrypto-1_1-x64.dll" -Destination "Chiaki"
Copy-Item -Force -Path "${BUILD_ROOT}\\openssl-1.1\x64\bin\libssl-1_1-x64.dll" -Destination "Chiaki"
Copy-Item -Force -Path "${SDL_ROOT}\\lib\\x64\\SDL2.dll" -Destination "Chiaki"

# Clean up our PATH
$env:Path = $START_PATH
