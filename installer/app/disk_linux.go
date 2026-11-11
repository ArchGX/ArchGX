// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

//go:build linux

package main

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

func candidateDisks() []disk {
	var out []disk
	ents, _ := os.ReadDir("/sys/block")
	for _, e := range ents {
		n := e.Name()
		if !(strings.HasPrefix(n, "sd") || strings.HasPrefix(n, "mmcblk")) {
			continue
		}
		size := int64(0)
		if b, err := os.ReadFile(filepath.Join("/sys/block", n, "size")); err == nil {
			s, _ := strconv.ParseInt(strings.TrimSpace(string(b)), 10, 64)
			size = s * 512
		}
		model, _ := os.ReadFile(filepath.Join("/sys/block", n, "device/model"))
		label := "/dev/" + n
		if m := strings.TrimSpace(string(model)); m != "" {
			label += " (" + m + ")"
		}
		out = append(out, disk{Path: "/dev/" + n, Label: label, Size: size})
	}
	return out
}

func needAdmin() string {
	if os.Geteuid() != 0 {
		return "Run this program with sudo:  sudo " + os.Args[0]
	}
	return ""
}

// prepareWrite: partitions of the card must not be mounted.
func prepareWrite(d disk) error {
	if d.Image {
		return nil
	}
	b, _ := os.ReadFile("/proc/mounts")
	for _, l := range strings.Split(string(b), "\n") {
		if strings.HasPrefix(l, d.Path) {
			return errorf("a partition of %s is mounted (%s): unmount it (e.g. eject it in the file manager without removing the card) and try again", d.Path, strings.Fields(l)[0])
		}
	}
	return nil
}

func elevate() bool { return false }
