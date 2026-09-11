# Installing Outlast 2 VR Beta 1

## Before installing

1. Install Outlast 2 through Steam and launch it normally once.
2. Close Outlast 2, SteamVR, and the Outlast 2 VR launcher.
3. Make sure an OpenXR runtime is installed. Quest 3 users should select VDXR
   in Virtual Desktop for the primary tested setup.

## Recommended manual installation

1. Download `Outlast2VR-Beta1-Windows-x64.zip` from GitHub Releases.
2. Open the Outlast 2 installation in Steam:
   **Library → Outlast 2 → Manage → Browse local files**.
3. Open `Binaries`, then `Win64`.
4. Extract **every file** from the release ZIP into that `Win64` folder.
5. Confirm these files are beside `Outlast2.exe`:
   - `dinput8.dll`
   - `openxr_loader.dll`
   - `Outlast2VR.exe`
   - `outlast2_vr_p32.ini`
   - `outlast2_vr_p35.ini`
   - `outlast2_vr_p37.ini`
6. Run `Outlast2VR.exe` and choose **PLAY VR**.

Do not run `Outlast2.exe` directly when you intend to use the launcher options.

## Optional PowerShell installer

The ZIP also contains `Install.ps1`. From PowerShell, run:

```powershell
.\Install.ps1 -GameDir "C:\Program Files (x86)\Steam\steamapps\common\Outlast 2\Binaries\Win64"
```

Change the path if the Steam library is elsewhere. The installer validates the
target, backs up existing mod files under Documents, copies the release, and
verifies each installed hash. It never modifies or replaces `Outlast2.exe`.

## Recommended first launch

- Face forward before pressing **PLAY VR**.
- Quest 3/Virtual Desktop: use VDXR, with color vibrance and gamma neutral for
  the first comparison.
- Start at 72 Hz if performance is uncertain, then increase only after testing.
- Use the launcher's **VR-safe** graphics option to disable motion blur and
  temporal anti-aliasing.
- Hold both stick clicks in gameplay to open VR settings/recenter controls.

## Troubleshooting

- **Launcher says the game is missing:** the launcher is not beside
  `Outlast2.exe`; move the complete release contents to `Binaries\Win64`.
- **OpenXR does not start:** select an active OpenXR runtime in Virtual Desktop,
  Meta Quest Link, or SteamVR, then run the launcher's VR check.
- **Immediate crash:** verify Steam game files, reinstall the complete ZIP, and
  confirm the executable version/hash listed in the README.
- **Visual or interaction bug:** attach `outlast2_vr_p34.log` from `Binaries\Win64`
  to a GitHub issue, with headset/runtime/GPU and exact reproduction steps.

Never post the game executable, game archives, extracted assets, or save files.
