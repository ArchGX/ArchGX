// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

//go:build darwin

package main

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
)

func candidateDisks() []disk {
	var out []disk
	for i := 0; i < 32; i++ {
		p := fmt.Sprintf("/dev/disk%d", i)
		if _, err := os.Stat(p); err != nil {
			continue
		}
		label := p
		if b, err := exec.Command("diskutil", "info", p).Output(); err == nil {
			for _, l := range strings.Split(string(b), "\n") {
				if strings.Contains(l, "Device / Media Name:") {
					label += " (" + strings.TrimSpace(strings.SplitN(l, ":", 2)[1]) + ")"
				}
			}
		}
		// the raw device is much faster than the buffered one
		out = append(out, disk{Path: fmt.Sprintf("/dev/rdisk%d", i), Label: label})
	}
	return out
}

func needAdmin() string {
	if os.Geteuid() != 0 {
		return "Run this program with sudo in Terminal:  sudo " + os.Args[0]
	}
	return ""
}

// prepareWrite unmounts the card's volumes (macOS mounts the FAT partition).
func prepareWrite(d disk) error {
	if d.Image {
		return nil
	}
	whole := strings.Replace(d.Path, "/dev/rdisk", "/dev/disk", 1)
	if out, err := exec.Command("diskutil", "unmountDisk", whole).CombinedOutput(); err != nil {
		return errorf("could not unmount %s: %s", whole, strings.TrimSpace(string(out)))
	}
	return nil
}

func elevate() bool { return false }
