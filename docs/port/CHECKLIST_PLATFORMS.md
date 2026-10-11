# Platform checklist (macOS, Windows)

Run at every Kazarov PR that touches the shared port library, on a release build of the pinned
commit, with PAL data. Linux is covered by `scripts/port/matrix.sh`; fork CI covers the builds and
tests. Record the date, commit and results in the PR's notes.

Start the game: Windows `run.bat`; macOS `docs/MACOS.md` "Running".

- [ ] Starts with data and reaches the title menu (scripted or by pad).
- [ ] Resize the window by dragging: the picture follows, no stretch or stale frame.
- [ ] Options > Display > Window Mode: to Fullscreen and back to Window.
- [ ] Controller: menu navigation and walking in a town or the first dungeon.
- [ ] Audio audible: title music, menu sounds.
- [ ] Headless frame 120, hash recorded:
  - Windows: `port\build\windows-x64-release\darkcloud.exe --headless --frames 120 --screenshot shot.png --data data --save %TEMP%\dc-check` then `certutil -hashfile shot.png SHA256`
  - macOS: `port/build/macos-arm64-release/darkcloud --headless --frames 120 --screenshot shot.png --data data --save "$(mktemp -d)"` then `shasum -a 256 shot.png`

Lavapipe's frame-120 hash is `555d25b7…` (`.kazarov/baseline/port/manifest.txt`); another driver
need not match it, but its hash must not change between two PRs on the same machine.
