# Installing ArchGX

ArchGX is installed by a script that changes **one** partition of the RetroN GX's SD card: the system partition ("rootfs"). It takes the system that's already on your card, adds ArchGX's files, switches off Hyperkin's own launcher programs, and writes it back.

The installer never touches:
- the bootloader;
- the kernel;
- the settings partition;
- the partition that holds your games and saves.

## What you need

- A Hyperkin RetroN GX and **its SD card** with the stock firmware `RubyMain-20250709`. The installer checks this and refuses anything else.
- A PC with **Linux**, or **Windows 10/11 with WSL** (Ubuntu).
- An SD card reader.
- The tools `squashfs-tools` and `util-linux`, which nearly every Linux system has. On Ubuntu:

  ```sh
  sudo apt install squashfs-tools util-linux
  ```

- The release archive `archgx-GP1.tar.gz` from the [Releases page](https://github.com/ArchGX/ArchGX/releases). Check it against `archgx-GP1.tar.gz.sha256`.

## 1. Back up the whole card first

The installer saves the original system partition on its own. A full copy of the card is still the safest undo for anything.

**Linux:** find the card with `lsblk` (for example `/dev/sdb`; make sure it's the card and not your own disk), then:

```sh
sudo dd if=/dev/sdX of=retron-gx-backup.img bs=4M status=progress
```

**Windows:** use [Win32 Disk Imager](https://sourceforge.net/projects/win32diskimager/): pick the card's drive letter, a file name, then **Read**.

## 2. Install

### Linux (card in a reader)

```sh
tar xzf archgx-GP1.tar.gz
cd archgx-GP1
sudo ./installer/archgx-install.sh check   /dev/sdX
sudo ./installer/archgx-install.sh install /dev/sdX
```

`check` tells you whether the card has the supported stock system. `install`:
1. saves the original system partition as `archgx-stock-rootfs.img`, in the current folder;
2. builds the ArchGX system partition from it;
3. asks you to type `yes`;
4. writes the new partition and verifies it.

**Keep `archgx-stock-rootfs.img`.** It undoes the install, and you need it for updates.

Unmount the card's partitions before installing, if your desktop mounted them automatically.

### Windows (WSL)

WSL can't always reach SD card readers, so work on a card image instead:

1. In Win32 Disk Imager, **Read** the card into `card.img`, for example in `C:\retrongx\`.
2. In Ubuntu (WSL):

   ```sh
   sudo apt install squashfs-tools util-linux
   cd /mnt/c/retrongx
   tar xzf archgx-GP1.tar.gz
   sudo ./archgx-GP1/installer/archgx-install.sh install card.img
   ```

3. In Win32 Disk Imager, **Write** `card.img` back to the card.

## 3. First start

Put the card in the console and switch it on.

On a freshly flashed card, the storage partition is first enlarged to fill the card. This takes a few seconds, once. Any files already on that partition are kept: they're copied to memory and put back.

Games and saves from the stock firmware (its `data/` folder) are copied into the ArchGX library once. Nothing is deleted.

Then the **Game Library** appears. Insert a HuCard, or copy `.pce` files to the card's `games/` folder. See the [User guide](USER_GUIDE.md).

## Updating to a newer ArchGX

```sh
sudo ./installer/archgx-install.sh install /dev/sdX --stock archgx-stock-rootfs.img
```

On a card that already runs ArchGX, the installer rebuilds from your stock backup. Your games, saves and settings stay on the card.

## Uninstalling

```sh
sudo ./installer/archgx-install.sh restore /dev/sdX --stock archgx-stock-rootfs.img
```

This writes the original system partition back. Your games and saves stay on the card; the stock firmware just won't show the ArchGX library.

## Troubleshooting

| Problem | What to do |
|---|---|
| `unknown system partition` | The card doesn't have the stock `RubyMain-20250709` system. Restore your card backup or the stock image, then try again. If you have a different official firmware version, please open an issue. |
| `is mounted: unmount it first` | Unmount the card's partitions (`sudo umount /dev/sdX*`) and run again. |
| `no partition named "rootfs"` | Wrong device, or not a RetroN GX card. Check with `lsblk`. |
| Black screen or stuck at start | Put the card in your PC and look in the `logs/` folder on the large partition. Please attach `gx-start.log`, `retroarch.log` and `launcher.log` to an issue. |
| A HuCard reads wrongly | Clean the cart's contacts and reinsert it. Wrong reads appear in the library as "Unknown HuCard"; delete them there (SELECT on the game). |
| RetroArch settings got messed up | Create an empty file named `reset-retroarch-settings` on the card's large partition. The stock RetroArch settings come back at the next start. |
