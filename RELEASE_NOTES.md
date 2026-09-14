# Public Beta 1.1 release notes

## Included

- OpenXR full-view VR gameplay
- Comfortable startup, menu, options, and loading-screen presentation
- Quest/Touch motion-tracked arms with native animation handoffs
- Native camcorder, night vision, zoom, batteries, recordings, and microphone
- Native interactions for pickups, doors, switches, hiding, movable objects,
  batteries, notes, and bandages
- School and non-school upper-body IK transitions
- Head/body anchoring for scripted sequences
- Desktop single-eye mirror for recording
- sRGB color correction and VR-safe graphics option
- PF17 native-view alignment for interaction targeting and visibility
- Root-cellar sleep progression correction
- PF18 fail-visible VDXR handoff: gameplay falls back to a full-view OpenXR
  projection when strict renderer matching is unavailable; it never uses the
  menu/loading panel.
- Sharp compatibility/performance renderer is now the default, removing the two
  full-resolution depth reconstruction passes that were enabled by default in
  Beta 1. Experimental depth stereo remains selectable in the launcher.
- Optional PF19 in-headset startup sequence: “StrikeZone Presents” and
  “Outlast 2 VR” appear through slow, beat-timed comfort fades before normal
  startup continues. It can be disabled directly on the launch page.

## Performance improvements in 1.1

- **Compatibility / performance is now the default renderer.** Beta 1 started
  with experimental depth stereo, which reconstructed both eyes with two
  additional full-resolution shader passes every frame. The new default makes
  one exact copy of the completed gameplay frame to both eyes, reducing GPU
  work and producing a sharper image on systems that previously reported
  30–40 FPS and blur.
- **Experimental depth stereo is opt-in.** It remains available in launcher
  Options for powerful GPUs and comparison testing, but no longer lowers the
  out-of-box performance of the public build.
- **The expensive persistent-object culling scan remains disabled.** That scan
  could cause a severe startup hitch lasting several minutes. The lighter
  hardware-occlusion correction stays enabled for gameplay visibility.
- **VDXR gameplay now fails visible.** If strict camera/capture matching is not
  accepted by a particular GPU or driver, the mod switches to a direct
  full-view OpenXR gameplay submission instead of leaving gameplay visible
  only on the desktop.
- **VR-safe graphics remain recommended.** This disables motion blur, temporal
  anti-aliasing and other effects that are expensive or unstable in this VR
  renderer. Start at 72 Hz and raise headset refresh only after performance is
  stable.

## Test status

- 19/19 automated regression tests pass.
- Public headset playtesting completed successfully on the release candidate.
- Internal runtime ID: `OUTLAST2VR-BETA11-INTRO-PF19-20260914`.

## Known beta limitations

- Quest 3 with Virtual Desktop/VDXR is the primary verified configuration.
- Compatibility mode prioritizes clarity, performance, and runtime support but
  does not provide binocular depth. Depth stereo requires additional GPU work.
- Some original Xbox button glyphs remain visible.
- Scripted sequences may temporarily use authored native arm animation.
- Report isolated UE3 visibility issues with a location, screenshot/video, and
  `outlast2_vr_p34.log`.
