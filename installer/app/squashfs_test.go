package main

import (
	"os"
	"testing"
)

// Round trip of the stock image and full conversion; paths from env.
func TestConvert(t *testing.T) {
	stock, err := os.ReadFile(os.Getenv("STOCK"))
	if err != nil {
		t.Skip("STOCK not set")
	}
	root, sb, err := ReadSquashfs(stock)
	if err != nil {
		t.Fatal(err)
	}
	rt := WriteSquashfs(root, sb.BlockSize, sb.ModTime)
	os.WriteFile(os.Getenv("OUT")+".roundtrip", rt, 0644)
	payload, _ := os.ReadFile(os.Getenv("PAYLOAD"))
	v, err := convert(root, payload)
	if err != nil {
		t.Fatal(err)
	}
	out := WriteSquashfs(root, sb.BlockSize, sb.ModTime)
	os.WriteFile(os.Getenv("OUT"), out, 0644)
	t.Logf("%s: %d bytes", v, len(out))
}
