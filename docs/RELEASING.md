# Release checklist (maintainers)

## Build and package
1. Update `VERSION`, `CHANGELOG.md`, and `ARCHGX_VERSION` in `src/pce-launcher/pce-launcher.c`.
2. `scripts/make-sysroot.sh <stock-rootfs> sysroot` (once).
3. `scripts/build.sh sysroot <stock-rootfs> build`. It must end with "payload ready" and no glibc errors.
4. Install on a copy of a stock card image:

   ```sh
   sudo installer/archgx-install.sh install card.img --payload build/payload --backup-dir bk
   ```

   Then check:
   - `check` reports the new version;
   - `restore` gives back a byte-identical stock image.
5. Hardware test on a spare card:
   - first start (storage enlarged), library, insert a HuCard;
   - save/load/rewind/screenshot, options (all filters), aspect switch;
   - RESET button, pull a cart mid-game, RetroArch Menu, Controller test, About.
6. `scripts/make-release.sh build dist`. Upload `dist/archgx-<VERSION>.tar.gz` and its `.sha256` to a GitHub release on tag `<VERSION>`.

## Rules for every release
- **No manufacturer files.** The release must contain only `payload/` (ArchGX files), the installer, checksums and documents. Never attach card images, `rootfs` dumps or anything from `stock-rootfs/`.
- **No games or BIOS files**, in the repository or in releases, including screenshots that would need game files to make. Use the test pattern for menu screenshots.
- **Corresponding source:** the tag must contain the exact patch and build recipe used for the binaries (GPL-3.0).
