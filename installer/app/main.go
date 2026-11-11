// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

// ArchGX installer: finds the RetroN GX SD card, checks its system partition,
// backs it up, builds the ArchGX system from it and writes it back.
// Nothing from the console's manufacturer is included in this program: it
// works only on the system that is already on the user's card.
//
//	archgx-installer                 interactive (double-click on Windows)
//	archgx-installer --image F.img   work on a card image file instead
//	archgx-installer --restore       put the stock system back (from the backup)
//	archgx-installer --yes           do not ask
package main

import (
	"bufio"
	"bytes"
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

//go:embed payload.tar.gz
var payload []byte

//go:embed VERSION
var appVersion string

const backupName = "archgx-stock-rootfs.img"

var in = bufio.NewReader(os.Stdin)

func errorf(f string, a ...any) error { return fmt.Errorf(f, a...) }

func say(f string, a ...any) { fmt.Printf(f+"\n", a...) }

func ask(prompt string) string {
	fmt.Print(prompt)
	s, _ := in.ReadString('\n')
	return strings.TrimSpace(s)
}

func pause() {
	if len(os.Args) == 1 { // started by double-click: keep the window open
		ask("\nPress Enter to close this window.")
	}
}

func fail(err error) {
	say("\nERROR: %v", err)
	pause()
	os.Exit(1)
}

func sum(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }

// backupDir: next to the program, or the current folder if that is not writable.
func backupDir() string {
	if exe, err := os.Executable(); err == nil {
		d := filepath.Dir(exe)
		if f, err := os.CreateTemp(d, ".archgx-write-test"); err == nil {
			f.Close()
			os.Remove(f.Name())
			return d
		}
	}
	d, _ := os.Getwd()
	return d
}

func main() {
	image := flag.String("image", "", "work on a card image file instead of a card")
	restore := flag.Bool("restore", false, "write the stock system partition back from the backup")
	yes := flag.Bool("yes", false, "do not ask for confirmation")
	stock := flag.String("stock", "", "stock backup file (default: "+backupName+" next to this program)")
	flag.Parse()
	version := strings.TrimSpace(appVersion)

	say("ArchGX installer %s  -  https://github.com/ArchGX/ArchGX", version)
	say("Open-source firmware for the Hyperkin RetroN GX (not affiliated with Hyperkin).\n")

	// ---- find the card -------------------------------------------------------
	var d disk
	if *image != "" {
		d = disk{Path: *image, Label: *image, Image: true}
	} else {
		if msg := needAdmin(); msg != "" {
			if elevate() {
				return // an elevated copy takes over
			}
			fail(errors.New(msg))
		}
		for {
			say("Looking for a RetroN GX SD card...")
			cards, _ := findCards()
			if len(cards) == 1 {
				d = cards[0]
				break
			}
			if len(cards) > 1 {
				for i, c := range cards {
					say("  %d) %s %s", i+1, c.Label, humanSize(c.Size))
				}
				var n int
				fmt.Sscan(ask("Which one? "), &n)
				if n >= 1 && n <= len(cards) {
					d = cards[n-1]
					break
				}
				continue
			}
			say("No RetroN GX card found. Put the console's SD card in a card reader on this computer.")
			if ask("Press Enter to look again, or type q to quit: ") == "q" {
				return
			}
		}
	}
	say("Card: %s %s", d.Label, humanSize(d.Size))

	f, err := os.Open(d.Path)
	if err != nil {
		fail(err)
	}
	parts, err := readGPT(f)
	if err != nil {
		fail(fmt.Errorf("%s is not a RetroN GX card (%v)", d.Label, err))
	}
	rp, ok := retronLayout(parts)
	if !ok {
		fail(fmt.Errorf("%s is not a RetroN GX card", d.Label))
	}
	partBytes := int64(rp.Last-rp.First+1) * sector
	cur, err := readAt(f, int64(rp.First)*sector, int(partBytes))
	f.Close()
	if err != nil {
		fail(fmt.Errorf("reading the card: %v", err))
	}

	state := "unknown"
	var curVersion string
	if sum(cur) == stockRootfsSHA256 {
		state = "stock"
	} else if root, _, err := ReadSquashfs(cur); err == nil {
		if n := lookup(root, "etc/archgx-release"); n != nil {
			state = "archgx"
			curVersion = strings.TrimSpace(string(n.Data))
		}
	}
	switch state {
	case "stock":
		say("System on the card: stock RetroN GX firmware (supported).")
	case "archgx":
		say("System on the card: %s.", curVersion)
	default:
		fail(errors.New("the card's system is not the stock RetroN GX firmware (RubyMain-20250709) this installer supports"))
	}

	// ---- the stock system: from the card, or from the backup -----------------
	bpath := *stock
	if bpath == "" {
		bpath = filepath.Join(backupDir(), backupName)
	}
	var base []byte
	if state == "stock" {
		base = cur
		if b, err := os.ReadFile(bpath); err == nil && sum(b) == stockRootfsSHA256 {
			say("Stock backup already saved: %s", bpath)
		} else {
			if err := os.WriteFile(bpath, cur, 0644); err != nil {
				fail(fmt.Errorf("could not save the stock backup to %s: %v", bpath, err))
			}
			say("Saved a backup of the stock system: %s\n  Keep this file: it lets you undo the install and update ArchGX later.", bpath)
		}
	} else {
		b, err := os.ReadFile(bpath)
		if err != nil || sum(b) != stockRootfsSHA256 {
			fail(fmt.Errorf("this card already runs ArchGX. To update or uninstall, the stock backup %s is needed: put it next to this program", backupName))
		}
		base = b
	}

	// ---- what to do ----------------------------------------------------------
	var newSys []byte
	if *restore {
		if state == "stock" {
			say("The card already has the stock system. Nothing to do.")
			pause()
			return
		}
		newSys = base
		say("\nThis will put the stock RetroN GX system back. Your games and saves stay on the card.")
	} else {
		say("\nPreparing %s...", version)
		root, sb, err := ReadSquashfs(base)
		if err != nil {
			fail(fmt.Errorf("reading the stock system: %v", err))
		}
		v, err := convert(root, payload)
		if err != nil {
			fail(err)
		}
		newSys = WriteSquashfs(root, sb.BlockSize, uint32(time.Now().Unix()))
		if int64(len(newSys)) > partBytes {
			fail(fmt.Errorf("the new system (%d bytes) does not fit the partition (%d bytes)", len(newSys), partBytes))
		}
		// check our own output before touching the card
		if chk, _, err := ReadSquashfs(newSys); err != nil || lookup(chk, "etc/archgx-release") == nil {
			fail(fmt.Errorf("self-check of the new system failed: %v", err))
		}
		say("Ready: %s (%.1f of %.1f MB).", v, float64(len(newSys))/1e6, float64(partBytes)/1e6)
		say("\nThis changes only the console's system partition. Your games and saves stay on the card.")
	}

	if !*yes {
		if a := strings.ToLower(ask("Type Y and press Enter to write to the card: ")); a != "y" && a != "yes" {
			say("Cancelled. Nothing was written.")
			pause()
			return
		}
	}

	// ---- write and verify ----------------------------------------------------
	if err := prepareWrite(d); err != nil {
		fail(err)
	}
	padded := newSys
	if r := len(padded) % sector; r != 0 {
		padded = append(padded, make([]byte, sector-r)...)
	}
	w, err := os.OpenFile(d.Path, os.O_RDWR, 0)
	if err != nil {
		fail(fmt.Errorf("opening the card for writing: %v", err))
	}
	say("Writing...")
	const chunk = 1 << 20
	for off := 0; off < len(padded); off += chunk {
		end := off + chunk
		if end > len(padded) {
			end = len(padded)
		}
		if _, err := w.WriteAt(padded[off:end], int64(rp.First)*sector+int64(off)); err != nil {
			w.Close()
			fail(fmt.Errorf("writing to the card failed: %v\nThe card may need the stock system restored: run this program again with the backup next to it", err))
		}
	}
	w.Sync()
	back, err := readAt(w, int64(rp.First)*sector, len(padded))
	w.Close()
	if err != nil || !bytes.Equal(back, padded) {
		fail(errors.New("verification after writing failed. Run this program again; if it keeps failing, try another card reader"))
	}
	say("Written and verified.")
	if *restore {
		say("\nDone: the stock system is back.")
	} else {
		say("\nDone! Put the card back in your RetroN GX and switch it on.")
		say("On a newly flashed card the first start takes a few seconds longer while the storage is prepared.")
	}
	pause()
}
