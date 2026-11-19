// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

package main

// Turns the stock system tree into the ArchGX system tree. Mirrors
// installer/archgx-install.sh: add the payload, edit three lines of stock
// configuration, switch off Hyperkin's launcher programs.

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"path"
	"regexp"
	"sort"
	"strings"
)

// SHA-256 of the stock system partition (RubyMain-20250709, 98304 sectors)
const stockRootfsSHA256 = "ef6184b3968d185ce00688cbaa405555b5983327768a641e60fa41fd8ea99344"

var disableList = []string{
	"etc/check_dumper.sh", "etc/firmware_update.sh", "etc/partition_resize.sh", "etc/reset-btn.sh",
	"etc/start_tgdumper.sh", "etc/usbmount/mount.d/01_restart_retroarch",
	"etc/usbmount/mount.d/01_try_launch_retroarch", "etc/usbmount/umount.d/01_kill_retroarch",
	"usr/bin/bte_fw_update_andrew", "usr/bin/check_firmware", "usr/bin/menu", "usr/bin/tgdumper-loading",
}

func lookup(root *Node, p string) *Node {
	n := root
	for _, part := range strings.Split(strings.Trim(p, "/"), "/") {
		if part == "" {
			continue
		}
		if n == nil || n.Type != tDir {
			return nil
		}
		n = n.child(part)
	}
	return n
}

func mkdirAll(root *Node, p string, mtime uint32) *Node {
	n := root
	for _, part := range strings.Split(strings.Trim(p, "/"), "/") {
		if part == "" {
			continue
		}
		c := n.child(part)
		if c == nil {
			c = &Node{Name: part, Type: tDir, Mode: 0755, Mtime: mtime}
			n.Children = append(n.Children, c)
		}
		n = c
	}
	return n
}

// addPayload extracts a payload .tar.gz (made by make-release.sh) into the tree.
// Files are owned by root; existing directories keep their metadata.
func addPayload(root *Node, payloadTarGz []byte) (version string, err error) {
	zr, err := gzip.NewReader(bytes.NewReader(payloadTarGz))
	if err != nil {
		return "", err
	}
	tr := tar.NewReader(zr)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return "", err
		}
		name := strings.TrimPrefix(path.Clean("/"+h.Name), "/")
		if name == "" || name == "." {
			continue
		}
		mt := uint32(h.ModTime.Unix())
		switch h.Typeflag {
		case tar.TypeDir:
			d := mkdirAll(root, name, mt)
			if d.Type != tDir {
				return "", fmt.Errorf("%s is not a directory on the card", name)
			}
		case tar.TypeReg:
			data, err := io.ReadAll(tr)
			if err != nil {
				return "", err
			}
			dir, base := path.Split(name)
			parent := mkdirAll(root, dir, mt)
			n := parent.child(base)
			if n == nil {
				n = &Node{Name: base}
				parent.Children = append(parent.Children, n)
			}
			*n = Node{Name: base, Type: tFile, Mode: uint16(h.Mode & 0o7777), UID: 0, GID: 0, Mtime: mt, Data: data}
			if name == "etc/archgx-release" {
				version = strings.TrimSpace(string(data))
			}
		case tar.TypeSymlink:
			dir, base := path.Split(name)
			parent := mkdirAll(root, dir, mt)
			n := parent.child(base)
			if n == nil {
				n = &Node{Name: base}
				parent.Children = append(parent.Children, n)
			}
			*n = Node{Name: base, Type: tSymlink, Mode: 0777, Mtime: mt, Target: h.Linkname}
		default:
			return "", fmt.Errorf("unsupported payload entry %s", name)
		}
	}
	if version == "" {
		return "", errors.New("payload has no etc/archgx-release")
	}
	return version, nil
}

func editFile(root *Node, p string, edit func(string) string, check func(string) bool) error {
	n := lookup(root, p)
	if n == nil || n.Type != tFile {
		return fmt.Errorf("/%s not found on the card", p)
	}
	s := edit(string(n.Data))
	if !check(s) {
		return fmt.Errorf("could not edit /%s", p)
	}
	n.Data = []byte(s)
	return nil
}

// manifestPath lists the payload's files on the card, so that a later
// in-place update can remove files a newer version no longer ships.
const manifestPath = "etc/archgx-files"

// payloadFiles lists the regular files and symlinks in a payload .tar.gz.
func payloadFiles(payloadTarGz []byte) (map[string]bool, error) {
	zr, err := gzip.NewReader(bytes.NewReader(payloadTarGz))
	if err != nil {
		return nil, err
	}
	tr := tar.NewReader(zr)
	files := map[string]bool{}
	for {
		h, err := tr.Next()
		if err == io.EOF {
			return files, nil
		}
		if err != nil {
			return nil, err
		}
		if h.Typeflag == tar.TypeReg || h.Typeflag == tar.TypeSymlink {
			files[strings.TrimPrefix(path.Clean("/"+h.Name), "/")] = true
		}
	}
}

func removeFile(root *Node, p string) bool {
	dir, base := path.Split(p)
	parent := lookup(root, dir)
	if parent == nil || parent.Type != tDir {
		return false
	}
	for i, c := range parent.Children {
		if c.Name == base && c.Type != tDir {
			parent.Children = append(parent.Children[:i], parent.Children[i+1:]...)
			return true
		}
	}
	return false
}

// convert applies every ArchGX change to the stock tree, or updates a tree
// that already runs ArchGX in place: every step below can be repeated.
func convert(root *Node, payload []byte) (string, error) {
	files, err := payloadFiles(payload)
	if err != nil {
		return "", err
	}
	// files the previous ArchGX shipped and this one doesn't (never a stock
	// file: retroarch replaced Hyperkin's and is always in the payload)
	if old := lookup(root, manifestPath); old != nil && old.Type == tFile {
		for _, f := range strings.Split(string(old.Data), "\n") {
			f = strings.TrimSpace(f)
			if f != "" && !files[f] && f != "usr/bin/retroarch" {
				removeFile(root, f)
			}
		}
	}
	version, err := addPayload(root, payload)
	if err != nil {
		return "", err
	}
	list := make([]string, 0, len(files)+1)
	for f := range files {
		list = append(list, f)
	}
	list = append(list, manifestPath)
	sort.Strings(list)
	m := lookup(mkdirAll(root, "etc", 0), "archgx-files")
	if m == nil {
		m = &Node{Name: "archgx-files"}
		e := lookup(root, "etc")
		e.Children = append(e.Children, m)
	}
	*m = Node{Name: "archgx-files", Type: tFile, Mode: 0644, Mtime: lookup(root, "etc/archgx-release").Mtime, Data: []byte(strings.Join(list, "\n") + "\n")}
	dropDumper := regexp.MustCompile(`(?m)^[ \t]*source /etc/check_dumper\.sh[ \t]*\n`)
	if err := editFile(root, "etc/init.d/rcS", func(s string) string {
		s = dropDumper.ReplaceAllString(s, "")
		return strings.Replace(s, "/usr/bin/check_firmware > /dev/null &", "/etc/gx-start.sh > /dev/null 2>&1 &   # ArchGX", 1)
	}, func(s string) bool {
		return strings.Contains(s, "/etc/gx-start.sh") && !strings.Contains(s, "check_dumper") && !strings.Contains(s, "check_firmware")
	}); err != nil {
		return "", err
	}
	f3 := regexp.MustCompile(`(?m)^F3 = /etc/reset-btn\.sh`)
	if err := editFile(root, "etc/input-event-daemon.conf", func(s string) string {
		return f3.ReplaceAllString(s, "F3 = /etc/gx-reset.sh")
	}, func(s string) bool { return regexp.MustCompile(`(?m)^F3 = /etc/gx-reset\.sh`).MatchString(s) }); err != nil {
		return "", err
	}
	en := regexp.MustCompile(`(?m)^ENABLED=1`)
	if err := editFile(root, "etc/usbmount/usbmount.conf", func(s string) string {
		return en.ReplaceAllString(s, "ENABLED=0  # ArchGX mounts the HuCard reader itself")
	}, func(s string) bool { return regexp.MustCompile(`(?m)^ENABLED=0`).MatchString(s) }); err != nil {
		return "", err
	}
	for _, p := range disableList {
		if n := lookup(root, p); n != nil {
			n.Mode = 0
		}
	}
	return version, nil
}
