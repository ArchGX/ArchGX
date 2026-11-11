// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
	"unicode/utf16"
)

const sector = 512

// disk is a card (raw device) or a card image file.
type disk struct {
	Path  string // what is opened for I/O
	Label string // shown to the user
	Size  int64  // bytes, 0 if unknown
	Image bool
}

type partition struct {
	Name  string
	First uint64 // LBA
	Last  uint64 // LBA, inclusive
}

func readAt(f io.ReaderAt, off int64, n int) ([]byte, error) {
	b := make([]byte, n)
	_, err := f.ReadAt(b, off)
	return b, err
}

// readGPT returns the partitions of a GPT disk.
func readGPT(f io.ReaderAt) ([]partition, error) {
	h, err := readAt(f, sector, sector)
	if err != nil {
		return nil, err
	}
	if string(h[0:8]) != "EFI PART" {
		return nil, errors.New("no GPT")
	}
	entLBA := binary.LittleEndian.Uint64(h[72:])
	count := binary.LittleEndian.Uint32(h[80:])
	esize := binary.LittleEndian.Uint32(h[84:])
	if count == 0 || count > 256 || esize < 128 || esize > 1024 {
		return nil, errors.New("unexpected GPT")
	}
	total := int(count * esize)
	total = (total + sector - 1) / sector * sector
	b, err := readAt(f, int64(entLBA)*sector, total)
	if err != nil {
		return nil, err
	}
	var parts []partition
	for i := 0; i < int(count); i++ {
		e := b[i*int(esize):]
		first := binary.LittleEndian.Uint64(e[32:])
		last := binary.LittleEndian.Uint64(e[40:])
		if first == 0 && last == 0 {
			continue
		}
		u := make([]uint16, 36)
		for j := range u {
			u[j] = binary.LittleEndian.Uint16(e[56+2*j:])
		}
		name := strings.TrimRight(string(utf16.Decode(u)), "\x00")
		parts = append(parts, partition{Name: name, First: first, Last: last})
	}
	return parts, nil
}

// retronLayout returns the rootfs partition if the disk looks like a RetroN GX card.
func retronLayout(parts []partition) (partition, bool) {
	have := map[string]partition{}
	for _, p := range parts {
		have[p.Name] = p
	}
	for _, n := range []string{"uboot", "trust", "boot", "rootfs", "userdata"} {
		if _, ok := have[n]; !ok {
			return partition{}, false
		}
	}
	return have["rootfs"], true
}

func humanSize(b int64) string {
	if b <= 0 {
		return "size unknown"
	}
	return fmt.Sprintf("%.1f GB", float64(b)/1e9)
}

// findCards checks every candidate disk for the RetroN GX partition layout.
func findCards() ([]disk, []string) {
	var found []disk
	var notes []string
	for _, d := range candidateDisks() {
		f, err := os.Open(d.Path)
		if err != nil {
			if os.IsPermission(err) {
				notes = append(notes, "no permission to read "+d.Label)
			}
			continue
		}
		parts, err := readGPT(f)
		f.Close()
		if err != nil {
			continue
		}
		if _, ok := retronLayout(parts); ok {
			found = append(found, d)
		}
	}
	return found, notes
}
