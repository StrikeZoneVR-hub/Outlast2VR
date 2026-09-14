# Beta release checklist

- [ ] Confirm the repository contains no Outlast 2 game files or extracted assets.
- [ ] Run `BUILD.cmd` and confirm all 19 tests pass.
- [ ] Verify launcher diagnostics show **Beta 1.1 RC1 / PF18**.
- [ ] Test startup, menus, options, loading, new game, and continued save.
- [ ] Test normal gameplay, scripted scenes, school transitions, and arm tracking.
- [ ] Test pickups, notes, batteries, bandages, doors, switches, movable objects,
      hiding/peeking, recordings, microphone, camcorder, night vision, and zoom.
- [ ] Test root-cellar bed progression and confirm no character spinning.
- [ ] Test the release ZIP from a clean game folder.
- [ ] Verify the release ZIP SHA-256 checksum.
- [ ] Tag the source commit `v0.1.1-beta.1` after headset validation.
- [ ] Create a GitHub **pre-release** and attach only the complete Windows ZIP and
      checksum file.
