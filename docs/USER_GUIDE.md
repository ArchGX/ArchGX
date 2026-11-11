# ArchGX user guide

## Buttons at a glance

| Where | Button | Does |
|---|---|---|
| Library | Up / Down | Move through the list |
| Library | Left / Right | Page up / down |
| Library | I or RUN | Play the game (or open the highlighted entry) |
| Library | SELECT | Rename or delete the game |
| In a game | **Hold RUN + SELECT** | Open the ArchGX menu (the game pauses) |
| Menus | I / RUN | Choose |
| Menus | II / SELECT | Back |
| Console | RESET button | Save the game's battery memory and return to the library |
| Console | 4:3 / 16:9 switch | Change the picture shape, live |

A USB keyboard works too: arrows, Enter, Escape (back), Tab (SELECT).

## The Game Library

The library lists every game in the card's `games/` folder by name. The five you played most recently come first, marked **Recent**; the rest follow A–Z. SuperGrafx games are marked **SGX** and CD games **CD**.

**Starting a game**
- If a game has a save, you're asked: **Resume last save**, **Start new game** or **Cancel**. Resume continues from your newest save slot.
- **SELECT** on a game opens its options: **Rename**, **Delete**, and for HuCards **Mark as SuperGrafx** (or back) and **Use as CD System Card**. Use these when a dump wasn't recognised automatically.
- Rename changes only the name shown. The file and its saves stay matched, and naming an unknown HuCard also records it for the game database (see [GAMEDB.md](GAMEDB.md)).

**Entries at the bottom of the list**
- **Play the HuCard in the slot**: shown while a cart is inserted.
- **Controller test**: shows each connected pad. The d-pad, SELECT, RUN, II and I light up when pressed. Hold SELECT + RUN for a second to go back.
- **About**: ArchGX version, number of games and save states, free space on the card.
- **RetroArch (advanced)**: RetroArch's full menu, for advanced settings. Quit RetroArch to come back.
- **Restart console**.

## HuCards

Insert a HuCard and ArchGX takes over:
1. **Reading HuCard**: the reader reads the cart, then ArchGX copies the result, showing a percentage.
2. **Identifying game**: by checksum and header, against the built-in database plus your own entries.
3. The game is stored in the library as `<Title>.pce`, or `cart-<checksum>.pce` if unknown, and starts.

**Other cart behaviour**
- A cart that's already in the library isn't stored twice.
- Pulling the cart during a game saves its battery memory and returns to the library.
- **Unknown carts** (homebrew, repros, prototypes) show as "Unknown HuCard (checksum)". Rename them to give them a name.
- **Bad reads:** dirty contacts can produce a wrong read, which shows up as an unknown cart. Delete it, clean the cart and reinsert it.

ArchGX mounts the reader read-only and never sends it commands.

**SuperGrafx HuCards** (Daimakaimura, Aldynes, Battle Ace, Madou King Granzort, 1941 Counter Attack, Darius Plus) are stored as `<Title>.sgx` and run on the Beetle SuperGrafx core. Copied `.sgx` files work the same way.

**System Cards** are not played: inserting one saves it to `bios/` so CD games can use it (message "System Card saved"). If a card isn't recognised, it shows up as an unknown HuCard: choose **Use as CD System Card** in its options. Its first 256 KB are saved to `bios/` and it leaves the library.

## CD games (beta)

Copy CD games into `games/`, either loose or one folder per game (one folder level is scanned):
- `.cue` with its `.bin` tracks, `.chd`, or `.ccd` with `.img`/`.sub`;
- multi-disc games: an `.m3u` playlist listing the discs. Only the playlist is shown.

CD games need a **System Card** image in the card's `bios/` folder. Either insert your System Card (it is saved automatically) or copy a dump named `syscard3.pce` (Super CD-ROM² 3.0), `syscard3u.pce` (TurboGrafx Super System Card), `syscard2.pce`, `syscard2u.pce` or `syscard1.pce`. The best one present is used, in that order. ArchGX doesn't include any BIOS files.

Deleting a CD game removes its tracks too, and its folder once empty. Saves, save states and rewind work as for HuCards.

## The in-game menu

Hold **RUN + SELECT** during a game. The game pauses behind the menu.

| Item | What it does |
|---|---|
| **Resume** | Back to the game |
| **Save State** | Four slots with a picture of the moment you saved. Choosing a used slot overwrites it |
| **Load State** | Pick a slot by its picture; starts on your newest save |
| **Rewind** | While you play, a snapshot is taken every 10 seconds. The last two minutes are shown as pictures ("15 s ago"). Pick one to go back to that moment. Snapshots are kept in memory and cleared when you quit |
| **Screenshot** | Saves the current picture to `screenshots/<game>-001.png` on the card |
| **Options** | See below |
| **Restart Game** | Resets the game (asks first) |
| **RetroArch Menu** | RetroArch's own full menu. Use its *Resume* to return |
| **Quit Game** | Back to the library (asks first; unsaved progress is lost) |

## Options

Changes apply when you leave the Options screen. They are remembered for all games.

| Option | Choices |
|---|---|
| Aspect ratio | 4:3 or 16:9. The front switch also changes it; whichever you changed last wins |
| Screen filter | Off, **Scanlines** (TV-style dark lines), **Composite TV** (soft colour bleed of a composite cable) |
| Smoothing | Off (sharp pixels) or On (softened) |
| Volume | 10 steps; 0 is mute |
| Reduce flicker | Lets more sprites show at once, so games flicker less. A few games may behave differently |
| Turbo I / Turbo II | Holding the button fires repeatedly, like the turbo switches on the original pads |

## The card

The large partition of the SD card (shown as `RETRONGX` on a PC) holds everything that is yours:

| Path | Contents |
|---|---|
| `games/` | Your games (`.pce`, `.sgx`, `.cue`, `.chd`, `.ccd`, `.m3u`; CD games may sit in one folder each). Copy your own dumps here from a PC |
| `bios/` | CD System Card images (`syscard3.pce` …) |
| `saves/` | Battery saves (`.srm`) and save states (`.state1`–`.state4` with `.png` pictures) |
| `screenshots/` | Screenshots from the in-game menu |
| `titles.txt` | Names shown in the library (file name, a tab, then the name) |
| `recent.txt` | Recently played order |
| `archgx-settings.cfg` | Options menu settings |
| `gamedb-local.csv` | Your own game-database entries; opens in any spreadsheet |
| `logs/` | Diagnostics: `gx-start.log`, `retroarch.log` (and `.1`, the game before), `ingame.log`, `launcher.log` |

**Recovery:** an empty file named `reset-retroarch-settings` on this partition restores RetroArch's stock settings at the next start.

## When something goes wrong

The console recovers by itself:
- if the display system stops, ArchGX restarts it;
- if that fails, the console reboots.

For a bug report, take the `logs/` folder from the card and open an issue on GitHub.
