// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

//go:build windows

package main

import (
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

func candidateDisks() []disk {
	var out []disk
	for i := 0; i < 32; i++ {
		p := fmt.Sprintf(`\\.\PhysicalDrive%d`, i)
		out = append(out, disk{Path: p, Label: fmt.Sprintf("Disk %d", i)})
	}
	return out
}

// isAdmin: opening the first physical disk needs administrator rights.
func isAdmin() bool {
	f, err := os.Open(`\\.\PhysicalDrive0`)
	if err != nil {
		return !os.IsPermission(err)
	}
	f.Close()
	return true
}

func needAdmin() string {
	if !isAdmin() {
		return "Please run this program as administrator (right-click it, then \"Run as administrator\")."
	}
	return ""
}

// elevate restarts the program with administrator rights (UAC prompt).
// Returns true if a new elevated copy was started.
func elevate() bool {
	if isAdmin() {
		return false
	}
	exe, err := os.Executable()
	if err != nil {
		return false
	}
	verb, _ := syscall.UTF16PtrFromString("runas")
	file, _ := syscall.UTF16PtrFromString(exe)
	cwd, _ := os.Getwd()
	dir, _ := syscall.UTF16PtrFromString(cwd)
	args := ""
	for _, a := range os.Args[1:] {
		args += syscall.EscapeArg(a) + " "
	}
	params, _ := syscall.UTF16PtrFromString(args)
	shell := syscall.NewLazyDLL("shell32.dll").NewProc("ShellExecuteW")
	r, _, _ := shell.Call(0, uintptr(unsafe.Pointer(verb)), uintptr(unsafe.Pointer(file)),
		uintptr(unsafe.Pointer(params)), uintptr(unsafe.Pointer(dir)), 1)
	return r > 32
}

// prepareWrite: nothing to do. ArchGX only writes the system partition, which
// Windows does not mount (it cannot read SquashFS).
func prepareWrite(d disk) error { return nil }
