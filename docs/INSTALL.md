# Installing ArchGX

ArchGX is installed by an app that runs on your computer. It changes **one** partition of the RetroN GX's SD card: the system partition. It reads the system that's already on your card, adds ArchGX, and writes it back.

The installer never touches:
- the bootloader;
- the kernel;
- the settings partition;
- the partition with your games and saves.

## What you need

- A Hyperkin RetroN GX and **its SD card**, with the stock firmware `RubyMain-20250709`. The installer checks this and refuses anything else.
- A computer with Windows, macOS or Linux, and an SD card reader.
- The installer for your computer, from the [Releases page](https://github.com/ArchGX/ArchGX/releases):

| Computer | Download |
|---|---|
| Windows 10/11 | `ArchGX-Installer-GP1-Windows.exe` |
| Mac with Apple chip (M1 and later) | `ArchGX-Installer-GP1-macOS-AppleSilicon` |
| Mac with Intel chip | `ArchGX-Installer-GP1-macOS-Intel` |
| Linux (x86-64) | `ArchGX-Installer-GP1-Linux` |

**Before you start:** copy anything you care about off the card's large partition (shown as a drive when you plug the card in). The installer leaves it alone, but a backup is always wise.

## Windows

1. Put the RetroN GX's SD card in your computer's card reader.
2. Double-click **`ArchGX-Installer-GP1-Windows.exe`**.
   - If Windows says *"Windows protected your PC"*, click **More info**, then **Run anyway**. ArchGX isn't signed with a paid certificate.
   - Allow it to make changes (administrator access is needed to write to the card).
3. The installer finds the card by itself and checks it. Type **Y** and press Enter.
4. When it says **Done**, eject the card, put it in the console and switch it on.

## Mac

1. Put the SD card in your Mac's card reader.
2. Open **Terminal** (Applications → Utilities) and type these commands, pressing Enter after each:

   ```sh
   cd ~/Downloads
   chmod +x ArchGX-Installer-GP1-macOS-*
   xattr -d com.apple.quarantine ArchGX-Installer-GP1-macOS-* 2>/dev/null
   sudo ./ArchGX-Installer-GP1-macOS-AppleSilicon     # or ...-macOS-Intel
   ```

   Enter your Mac password when asked.
3. Type **Y** and press Enter. The installer unmounts the card's drive by itself before writing.
4. When it says **Done**, eject the card, put it in the console and switch it on.

## Linux

```sh
chmod +x ArchGX-Installer-GP1-Linux
sudo ./ArchGX-Installer-GP1-Linux
```

Unmount (eject) the card's drive first if your desktop mounted it.

## The backup it makes

On the first install, the installer saves the card's original system partition **next to the installer**, as `archgx-stock-rootfs.img` (48 MB).

**Keep that file.** It lets you:
- **Uninstall:** run the installer with `--restore`, or see "Uninstalling" below. It writes the stock system back.
- **Update to a newer ArchGX:** run the newer installer with the backup in the same folder.

## First start

On a freshly flashed card, the storage partition is first enlarged to fill the card. This takes a few seconds, once. Any files already on that partition are kept.

Games and saves from the stock firmware (its `data/` folder) are copied into the ArchGX library once. Nothing is deleted.

Then the **Game Library** appears. Insert a HuCard, or copy `.pce` files to the card's `games/` folder. See the [User guide](USER_GUIDE.md).

## Updating

Run the installer of the new version with `archgx-stock-rootfs.img` in the same folder. Your games, saves and settings stay on the card.

## Uninstalling

Put `archgx-stock-rootfs.img` next to the installer, then run it with `--restore`:

| System | Command |
|---|---|
| Windows | Open Command Prompt in the folder, then `ArchGX-Installer-GP1-Windows.exe --restore` |
| Mac | `sudo ./ArchGX-Installer-GP1-macOS-AppleSilicon --restore` |
| Linux | `sudo ./ArchGX-Installer-GP1-Linux --restore` |

Your games and saves stay on the card; the stock firmware just won't show the ArchGX library.

## Advanced: the installer script

`archgx-GP1.tar.gz` contains the same installation as a shell script, plus the files it installs. Use it on Linux or WSL, or on card image files:

```sh
tar xzf archgx-GP1.tar.gz && cd archgx-GP1
sudo ./installer/archgx-install.sh check   /dev/sdX
sudo ./installer/archgx-install.sh install /dev/sdX            # or a card image file
sudo ./installer/archgx-install.sh restore /dev/sdX --stock archgx-stock-rootfs.img
```

It needs `squashfs-tools` and `util-linux`. The installer app also works on image files: `--image card.img`.

## Troubleshooting

| Problem | What to do |
|---|---|
| "No RetroN GX card found" | Check the card is in the reader. On Windows, make sure you allowed administrator access. Some USB card readers need a moment: press Enter to look again. |
| "not the stock RetroN GX firmware" | The card has a different system. Flash the official stock image (Hyperkin support provides it), then try again. If you have a different official version, please open an issue. |
| "this card already runs ArchGX" | To update or uninstall, put `archgx-stock-rootfs.img` from the first install next to the installer. |
| "a partition ... is mounted" (Linux) | Eject the card's drive in the file manager (keep the card in the reader) and run again. |
| Writing or verification fails | Try again, or try another card reader. If the card no longer starts, run the installer with `--restore`. |
| Black screen or stuck at start | Put the card in your computer and look in the `logs/` folder on the large partition. Please attach `gx-start.log`, `retroarch.log` and `launcher.log` to an issue. |
| A HuCard reads wrongly | Clean the cart's contacts and reinsert it. Wrong reads appear in the library as "Unknown HuCard"; delete them there (SELECT on the game). |
| RetroArch settings got messed up | Create an empty file named `reset-retroarch-settings` on the card's large partition. The stock RetroArch settings come back at the next start. |
