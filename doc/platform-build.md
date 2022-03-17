
# Platform-specific build instructions

## Fedora

On Fedora, build dependencies can be installed via:

```
sudo dnf install cmake make qt5-qtmultimedia-devel qt5-qtsvg-devel qt5-qtbase-gui ffmpeg-devel opus-devel openssl-devel python3-protobuf protobuf-c protobuf-devel qt5-rpm-macros SDL2-devel libevdev-devel systemd-devel
```

Then, Chiaki builds just like any other CMake project:
```
git submodule update --init
mkdir build && cd build
cmake ..
make
```

In order to utilize hardware decoding, necessary VA-API component needs to be installed separately depending on your GPU. For example on Fedora:

* **Intel**: `libva-intel-driver`(majority laptop and desktop) OR `libva-intel-hybrid-driver`(most netbook with Atom processor)
* **AMD**: Already part of default installation
* **Nvidia**: `libva-vdpau-driver`

## Windows

CI Windows builds are done on AppVeyor within MSYS2 using this script: [scripts/appveyor.sh](../scripts/appveyor.sh).

### Local Windows Builds
Pre-requisites:
1. You need to install QT 5.15.2 (from Qt site or elsewhere, specifically msvc2019_64 packages, but recommend everything in that version).
2. Local Windows builds don't support building ffmpeg, so you need to obtain ffmpeg-prefix folder from appveyor build and copy into the root of the repository.
3. git and python3 must be installed and in the PATH
4. To setup VS developer powershell to use x64 build environment, right click on "Developer PowerShell for VS 2019" in start menu and select "Open File Location". Make a copy of the shortcut and rename to "Developer PowerShell x64 for VS 2019" and edit the properties. Update the "Target" to something include `-DevCmdArguments '-arch=x64'` at the end. EX:

`C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -noe -c "&{Import-Module """C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"""; Enter-VsDevShell {vsInstanceID} -DevCmdArguments '-arch=x64'}"`

Once you've met the pre-requisites, launch the x64 Developer Command Prompt from your shortcut (it should show up in start menu) and navigate to the root of the repository.

If you haven't built before, you'll need to initialize the git submodules before first build:
```
git submodule update --init
```

You can now run builds with:
```
    .\scripts\build-windows.ps1
```

Output will be in the newly created `Chiaki` directory