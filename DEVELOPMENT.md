# Development

Outlast 2 VR Beta 1 targets 64-bit Windows and the verified retail executable
listed in the main README. Runtime hooks reject signatures they do not
recognize rather than guessing.

## Requirements

- Windows 10 or 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake and Ninja (both are available through Visual Studio)
- A legal PC installation of Outlast 2 for headset testing

## Build and test

Run `BUILD.cmd`, or use a Developer PowerShell:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
```

Outputs are written to `build/bin`:

- `dinput8.dll` — VR runtime proxy
- `Outlast2VR.exe` — launcher with embedded visual assets

The project dynamically loads `openxr_loader.dll`; the loader binary is not
committed to this repository. Public release packages use the separately
verified Khronos loader recorded in their checksum file.

## Safe development rules

1. Preserve the last tested release before changing renderer or input hooks.
2. Give every test build a unique internal build ID.
3. Validate exact executable bytes and reflected metadata before hooking.
4. Never write camera/input/pawn state merely because a pointer looked valid.
5. Run the complete test suite and then test menus, gameplay, camcorder, night
   vision, interactions, scripted scenes, school transitions, and save loading.
6. Never commit Outlast 2 game files or extracted assets.
