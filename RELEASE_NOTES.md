# Public Beta 1 release notes

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

## Test status

- 19/19 automated regression tests pass.
- Public headset playtesting completed successfully on the release candidate.
- Internal runtime ID: `OUTLAST2VR-NATIVE-VIEW-BED-CULL-PF17-20260910`.

## Known beta limitations

- Quest 3 with Virtual Desktop/VDXR is the primary verified configuration.
- Some original Xbox button glyphs remain visible.
- Scripted sequences may temporarily use authored native arm animation.
- Report isolated UE3 visibility issues with a location, screenshot/video, and
  `outlast2_vr_p34.log`.
