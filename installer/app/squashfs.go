// SPDX-License-Identifier: GPL-3.0-or-later
// Part of ArchGX (https://github.com/ArchGX/ArchGX)

package main

// A small SquashFS 4.0 (gzip) reader and writer: enough to unpack the
// console's system partition, change it and pack it again, on any PC,
// without squashfs-tools. Supports directories, regular files, symlinks,
// device nodes, fifos and sockets; no xattrs (the stock image has none).

import (
	"bytes"
	"compress/zlib"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"sort"
)

const (
	sqMagic     = 0x73717368
	metaSize    = 8192
	noFragment  = 0xFFFFFFFF
	uncompBit   = 1 << 24
	noTable     = 0xFFFFFFFFFFFFFFFF
	flagNoXattr = 0x0200
)

const (
	tDir = iota + 1
	tFile
	tSymlink
	tBlk
	tChr
	tFifo
	tSock
	tLDir
	tLFile
	tLSymlink
	tLBlk
	tLChr
	tLFifo
	tLSock
)

// Node is one entry of the file tree.
type Node struct {
	Name     string
	Type     int // basic type: tDir, tFile, tSymlink, tBlk, tChr, tFifo, tSock
	Mode     uint16
	UID, GID uint32
	Mtime    uint32
	Data     []byte  // file contents
	Target   string  // symlink target
	Rdev     uint32  // device number (squashfs encoding)
	Children []*Node // directories
}

func (n *Node) child(name string) *Node {
	for _, c := range n.Children {
		if c.Name == name {
			return c
		}
	}
	return nil
}

type superblock struct {
	Magic, InodeCount, ModTime, BlockSize, FragCount uint32
	Compression, BlockLog, Flags, IDCount            uint16
	VMajor, VMinor                                   uint16
	RootInode, BytesUsed                             uint64
	IDTable, XattrTable, InodeTable, DirTable        uint64
	FragTable, ExportTable                           uint64
}

// ---------------------------------------------------------------- reader

type reader struct {
	img   []byte
	sb    superblock
	ids   []uint32
	frags []fragEntry
}

type fragEntry struct {
	Start  uint64
	Size   uint32
	Unused uint32
}

func inflate(b []byte) ([]byte, error) {
	zr, err := zlib.NewReader(bytes.NewReader(b))
	if err != nil {
		return nil, err
	}
	defer zr.Close()
	return io.ReadAll(zr)
}

// metaBlock reads the metadata block at pos; returns data and the next block position.
func (r *reader) metaBlock(pos uint64) ([]byte, uint64, error) {
	if pos+2 > uint64(len(r.img)) {
		return nil, 0, errors.New("metadata out of range")
	}
	h := binary.LittleEndian.Uint16(r.img[pos:])
	size := uint64(h & 0x7FFF)
	raw := r.img[pos+2 : pos+2+size]
	if h&0x8000 != 0 {
		return raw, pos + 2 + size, nil
	}
	d, err := inflate(raw)
	return d, pos + 2 + size, err
}

// metaStream reads `n` bytes of metadata starting at block `blk` (absolute), offset `off`.
func (r *reader) metaRead(blk uint64, off int, n int) ([]byte, error) {
	var out []byte
	for len(out) < off+n {
		d, next, err := r.metaBlock(blk)
		if err != nil {
			return nil, err
		}
		out = append(out, d...)
		blk = next
	}
	return out[off : off+n], nil
}

// metaCursor reads metadata sequentially, crossing block boundaries.
type metaCursor struct {
	r   *reader
	blk uint64
	buf []byte
}

func (r *reader) cursor(blk uint64, off int) (*metaCursor, error) {
	d, next, err := r.metaBlock(blk)
	if err != nil {
		return nil, err
	}
	return &metaCursor{r: r, blk: next, buf: d[off:]}, nil
}

func (c *metaCursor) read(n int) ([]byte, error) {
	for len(c.buf) < n {
		d, next, err := c.r.metaBlock(c.blk)
		if err != nil {
			return nil, err
		}
		c.buf = append(c.buf, d...)
		c.blk = next
	}
	out := c.buf[:n]
	c.buf = c.buf[n:]
	return out, nil
}

func (c *metaCursor) u16() uint16 { b, _ := c.read(2); return binary.LittleEndian.Uint16(b) }
func (c *metaCursor) u32() uint32 { b, _ := c.read(4); return binary.LittleEndian.Uint32(b) }
func (c *metaCursor) u64() uint64 { b, _ := c.read(8); return binary.LittleEndian.Uint64(b) }

func (r *reader) lookupTable(start uint64, count int, entSize int) ([]byte, error) {
	total := count * entSize
	nblocks := (total + metaSize - 1) / metaSize
	var out []byte
	for i := 0; i < nblocks; i++ {
		p := binary.LittleEndian.Uint64(r.img[start+uint64(8*i):])
		d, _, err := r.metaBlock(p)
		if err != nil {
			return nil, err
		}
		out = append(out, d...)
	}
	if len(out) < total {
		return nil, errors.New("short lookup table")
	}
	return out[:total], nil
}

// ReadSquashfs parses a SquashFS image into a tree.
func ReadSquashfs(img []byte) (*Node, superblock, error) {
	r := &reader{img: img}
	if err := binary.Read(bytes.NewReader(img), binary.LittleEndian, &r.sb); err != nil {
		return nil, r.sb, err
	}
	sb := r.sb
	if sb.Magic != sqMagic || sb.VMajor != 4 || sb.Compression != 1 {
		return nil, sb, errors.New("not a gzip SquashFS 4.0 image")
	}
	idb, err := r.lookupTable(sb.IDTable, int(sb.IDCount), 4)
	if err != nil {
		return nil, sb, err
	}
	for i := 0; i < int(sb.IDCount); i++ {
		r.ids = append(r.ids, binary.LittleEndian.Uint32(idb[4*i:]))
	}
	if sb.FragCount > 0 {
		fb, err := r.lookupTable(sb.FragTable, int(sb.FragCount), 16)
		if err != nil {
			return nil, sb, err
		}
		for i := 0; i < int(sb.FragCount); i++ {
			r.frags = append(r.frags, fragEntry{
				Start: binary.LittleEndian.Uint64(fb[16*i:]),
				Size:  binary.LittleEndian.Uint32(fb[16*i+8:]),
			})
		}
	}
	root, err := r.readInode(sb.RootInode, "")
	return root, sb, err
}

func (r *reader) dataBlock(pos uint64, size uint32) ([]byte, error) {
	n := uint64(size &^ uncompBit)
	raw := r.img[pos : pos+n]
	if size&uncompBit != 0 {
		return raw, nil
	}
	return inflate(raw)
}

func (r *reader) readInode(ref uint64, name string) (*Node, error) {
	blk := r.sb.InodeTable + (ref >> 16)
	c, err := r.cursor(blk, int(ref&0xFFFF))
	if err != nil {
		return nil, err
	}
	typ := int(c.u16())
	n := &Node{Name: name}
	n.Mode = c.u16()
	uidx, gidx := c.u16(), c.u16()
	if int(uidx) >= len(r.ids) || int(gidx) >= len(r.ids) {
		return nil, errors.New("bad id index")
	}
	n.UID, n.GID = r.ids[uidx], r.ids[gidx]
	n.Mtime = c.u32()
	c.u32() // inode number
	switch typ {
	case tDir, tLDir:
		n.Type = tDir
		var start uint32
		var size uint32
		var off uint16
		if typ == tDir {
			start = c.u32()
			c.u32() // nlink
			size = uint32(c.u16())
			off = c.u16()
			c.u32() // parent
		} else {
			c.u32() // nlink
			size = c.u32()
			start = c.u32()
			c.u32() // parent
			c.u16() // index count (entries not needed)
			off = c.u16()
			if x := c.u32(); x != 0xFFFFFFFF {
				return nil, fmt.Errorf("%s: xattrs are not supported", name)
			}
		}
		if size <= 3 {
			return n, nil
		}
		dc, err := r.cursor(r.sb.DirTable+uint64(start), int(off))
		if err != nil {
			return nil, err
		}
		left := int(size) - 3
		for left > 0 {
			count := int(dc.u32()) + 1
			istart := dc.u32()
			inum := dc.u32()
			_ = inum
			left -= 12
			for i := 0; i < count; i++ {
				ioff := dc.u16()
				dc.u16() // inode number delta
				dc.u16() // type
				nsz := int(dc.u16()) + 1
				nb, err := dc.read(nsz)
				if err != nil {
					return nil, err
				}
				left -= 8 + nsz
				child, err := r.readInode(uint64(istart)<<16|uint64(ioff), string(nb))
				if err != nil {
					return nil, err
				}
				n.Children = append(n.Children, child)
			}
		}
	case tFile, tLFile:
		n.Type = tFile
		var start, size uint64
		var frag, foff uint32
		if typ == tFile {
			start = uint64(c.u32())
			frag = c.u32()
			foff = c.u32()
			size = uint64(c.u32())
		} else {
			start = c.u64()
			size = c.u64()
			c.u64() // sparse
			c.u32() // nlink
			frag = c.u32()
			foff = c.u32()
			if x := c.u32(); x != 0xFFFFFFFF {
				return nil, fmt.Errorf("%s: xattrs are not supported", name)
			}
		}
		bs := uint64(r.sb.BlockSize)
		nblocks := size / bs
		if frag == noFragment && size%bs != 0 {
			nblocks++
		}
		data := make([]byte, 0, size)
		pos := start
		for i := uint64(0); i < nblocks; i++ {
			sz := c.u32()
			if sz == 0 { // sparse block
				want := bs
				if size-uint64(len(data)) < bs {
					want = size - uint64(len(data))
				}
				data = append(data, make([]byte, want)...)
				continue
			}
			d, err := r.dataBlock(pos, sz)
			if err != nil {
				return nil, fmt.Errorf("%s: %v", name, err)
			}
			data = append(data, d...)
			pos += uint64(sz &^ uncompBit)
		}
		if frag != noFragment {
			if int(frag) >= len(r.frags) {
				return nil, errors.New("bad fragment index")
			}
			f := r.frags[frag]
			d, err := r.dataBlock(f.Start, f.Size)
			if err != nil {
				return nil, err
			}
			tail := size - uint64(len(data))
			data = append(data, d[foff:uint64(foff)+tail]...)
		}
		if uint64(len(data)) != size {
			return nil, fmt.Errorf("%s: size mismatch", name)
		}
		n.Data = data
	case tSymlink, tLSymlink:
		n.Type = tSymlink
		c.u32() // nlink
		sz := int(c.u32())
		t, err := c.read(sz)
		if err != nil {
			return nil, err
		}
		n.Target = string(t)
	case tBlk, tChr, tLBlk, tLChr:
		n.Type = tBlk
		if typ == tChr || typ == tLChr {
			n.Type = tChr
		}
		c.u32() // nlink
		n.Rdev = c.u32()
	case tFifo, tSock, tLFifo, tLSock:
		n.Type = tFifo
		if typ == tSock || typ == tLSock {
			n.Type = tSock
		}
		c.u32()
	default:
		return nil, fmt.Errorf("%s: unsupported inode type %d", name, typ)
	}
	return n, nil
}

// ---------------------------------------------------------------- writer

type metaWriter struct {
	out bytes.Buffer // compressed blocks
	cur bytes.Buffer // current uncompressed block
}

func compress(b []byte) []byte {
	var z bytes.Buffer
	zw, _ := zlib.NewWriterLevel(&z, zlib.BestCompression)
	zw.Write(b)
	zw.Close()
	return z.Bytes()
}

func (m *metaWriter) flushBlock() {
	if m.cur.Len() == 0 {
		return
	}
	raw := m.cur.Bytes()
	c := compress(raw)
	var h [2]byte
	if len(c) < len(raw) {
		binary.LittleEndian.PutUint16(h[:], uint16(len(c)))
		m.out.Write(h[:])
		m.out.Write(c)
	} else {
		binary.LittleEndian.PutUint16(h[:], uint16(len(raw))|0x8000)
		m.out.Write(h[:])
		m.out.Write(raw)
	}
	m.cur.Reset()
}

// pos returns the reference (compressed block start, offset) of the next byte.
func (m *metaWriter) pos() (uint32, uint16) {
	return uint32(m.out.Len()), uint16(m.cur.Len())
}

func (m *metaWriter) write(b []byte) {
	for len(b) > 0 {
		room := metaSize - m.cur.Len()
		if room > len(b) {
			room = len(b)
		}
		m.cur.Write(b[:room])
		b = b[room:]
		if m.cur.Len() == metaSize {
			m.flushBlock()
		}
	}
}

func (m *metaWriter) finish() []byte {
	m.flushBlock()
	return m.out.Bytes()
}

type writer struct {
	bs      uint32
	data    bytes.Buffer // everything after the superblock up to the tables
	frags   []fragEntry
	fragBuf bytes.Buffer
	ids     []uint32
	idIdx   map[uint32]uint16
	inodes  metaWriter
	dirs    metaWriter
	ino     uint32
	count   uint32
}

type fileLoc struct {
	start uint64
	sizes []uint32
	frag  uint32
	foff  uint32
}

func (w *writer) id(v uint32) uint16 {
	if i, ok := w.idIdx[v]; ok {
		return i
	}
	i := uint16(len(w.ids))
	w.ids = append(w.ids, v)
	w.idIdx[v] = i
	return i
}

func (w *writer) flushFrag() {
	if w.fragBuf.Len() == 0 {
		return
	}
	raw := w.fragBuf.Bytes()
	start := uint64(96 + w.data.Len())
	c := compress(raw)
	var size uint32
	if len(c) < len(raw) {
		w.data.Write(c)
		size = uint32(len(c))
	} else {
		w.data.Write(raw)
		size = uint32(len(raw)) | uncompBit
	}
	w.frags = append(w.frags, fragEntry{Start: start, Size: size})
	w.fragBuf.Reset()
}

func (w *writer) writeData(d []byte) fileLoc {
	loc := fileLoc{start: uint64(96 + w.data.Len()), frag: noFragment}
	bs := int(w.bs)
	full := len(d) / bs
	for i := 0; i < full; i++ {
		blk := d[i*bs : (i+1)*bs]
		c := compress(blk)
		if len(c) < len(blk) {
			w.data.Write(c)
			loc.sizes = append(loc.sizes, uint32(len(c)))
		} else {
			w.data.Write(blk)
			loc.sizes = append(loc.sizes, uint32(len(blk))|uncompBit)
		}
	}
	tail := d[full*bs:]
	if len(tail) > 0 {
		if w.fragBuf.Len()+len(tail) > bs {
			w.flushFrag()
		}
		loc.frag = uint32(len(w.frags))
		loc.foff = uint32(w.fragBuf.Len())
		w.fragBuf.Write(tail)
	}
	return loc
}

type dirEnt struct {
	name  string
	blk   uint32
	off   uint16
	ino   uint32
	btype uint16
}

func le16(b *bytes.Buffer, v uint16) { binary.Write(b, binary.LittleEndian, v) }
func le32(b *bytes.Buffer, v uint32) { binary.Write(b, binary.LittleEndian, v) }
func le64(b *bytes.Buffer, v uint64) { binary.Write(b, binary.LittleEndian, v) }

// writeTree writes inodes depth-first (children before parents); returns the
// inode's reference and number.
func (w *writer) writeNode(n *Node, locs map[*Node]fileLoc, parentIno uint32) (uint32, uint16, uint32) {
	var hdr bytes.Buffer
	header := func(typ uint16, ino uint32) {
		le16(&hdr, typ)
		le16(&hdr, n.Mode&0x0FFF)
		le16(&hdr, w.id(n.UID))
		le16(&hdr, w.id(n.GID))
		le32(&hdr, n.Mtime)
		le32(&hdr, ino)
	}
	switch n.Type {
	case tDir:
		// children first; directory inode numbers are assigned after them
		kids := append([]*Node(nil), n.Children...)
		sort.Slice(kids, func(i, j int) bool { return kids[i].Name < kids[j].Name })
		var ents []dirEnt
		subdirs := 0
		for _, k := range kids {
			if k.Type == tDir {
				subdirs++
			}
		}
		// a directory is numbered before its children so they can name their parent
		myIno := w.nextIno()
		for _, k := range kids {
			blk, off, ino := w.writeNode(k, locs, myIno)
			ents = append(ents, dirEnt{name: k.Name, blk: blk, off: off, ino: ino, btype: uint16(k.Type)})
		}
		// directory listing
		dblk, doff := w.dirs.pos()
		size := 0
		for i := 0; i < len(ents); {
			j := i
			for j < len(ents) && j-i < 256 && ents[j].blk == ents[i].blk &&
				int64(ents[j].ino)-int64(ents[i].ino) <= 32767 && int64(ents[j].ino)-int64(ents[i].ino) >= -32768 {
				j++
			}
			var b bytes.Buffer
			le32(&b, uint32(j-i-1))
			le32(&b, ents[i].blk)
			le32(&b, ents[i].ino)
			for _, e := range ents[i:j] {
				le16(&b, e.off)
				le16(&b, uint16(int16(int64(e.ino)-int64(ents[i].ino))))
				le16(&b, e.btype)
				le16(&b, uint16(len(e.name)-1))
				b.WriteString(e.name)
			}
			size += b.Len()
			w.dirs.write(b.Bytes())
			i = j
		}
		fsize := uint32(size + 3)
		nlink := uint32(2 + subdirs)
		blk, off := w.inodes.pos()
		if fsize <= 0xFFFF {
			header(tDir, myIno)
			le32(&hdr, dblk)
			le32(&hdr, nlink)
			le16(&hdr, uint16(fsize))
			le16(&hdr, doff)
			le32(&hdr, parentIno)
		} else {
			header(tLDir, myIno)
			le32(&hdr, nlink)
			le32(&hdr, fsize)
			le32(&hdr, dblk)
			le32(&hdr, parentIno)
			le16(&hdr, 0) // no index
			le16(&hdr, doff)
			le32(&hdr, 0xFFFFFFFF)
		}
		w.inodes.write(hdr.Bytes())
		return blk, off, myIno
	case tFile:
		ino := w.nextIno()
		loc := locs[n]
		blk, off := w.inodes.pos()
		if loc.start < 1<<32 && len(n.Data) < 1<<32 {
			header(tFile, ino)
			le32(&hdr, uint32(loc.start))
			le32(&hdr, loc.frag)
			le32(&hdr, loc.foff)
			le32(&hdr, uint32(len(n.Data)))
		} else {
			header(tLFile, ino)
			le64(&hdr, loc.start)
			le64(&hdr, uint64(len(n.Data)))
			le64(&hdr, 0)
			le32(&hdr, 1)
			le32(&hdr, loc.frag)
			le32(&hdr, loc.foff)
			le32(&hdr, 0xFFFFFFFF)
		}
		for _, s := range loc.sizes {
			le32(&hdr, s)
		}
		w.inodes.write(hdr.Bytes())
		return blk, off, ino
	case tSymlink:
		ino := w.nextIno()
		blk, off := w.inodes.pos()
		header(tSymlink, ino)
		le32(&hdr, 1)
		le32(&hdr, uint32(len(n.Target)))
		hdr.WriteString(n.Target)
		w.inodes.write(hdr.Bytes())
		return blk, off, ino
	case tBlk, tChr:
		ino := w.nextIno()
		blk, off := w.inodes.pos()
		header(uint16(n.Type), ino)
		le32(&hdr, 1)
		le32(&hdr, n.Rdev)
		w.inodes.write(hdr.Bytes())
		return blk, off, ino
	default: // fifo, socket
		ino := w.nextIno()
		blk, off := w.inodes.pos()
		header(uint16(n.Type), ino)
		le32(&hdr, 1)
		w.inodes.write(hdr.Bytes())
		return blk, off, ino
	}
}

// Inode numbers only need to be unique (1..count).
func (w *writer) nextIno() uint32 { w.ino++; return w.ino }

func countNodes(n *Node) uint32 {
	c := uint32(1)
	for _, k := range n.Children {
		c += countNodes(k)
	}
	return c
}

func (w *writer) table(entries []byte) (metaBlocks []byte, lookup []byte, startOffsets []uint64) {
	var m metaWriter
	var starts []uint32
	for i := 0; i < len(entries); i += metaSize {
		end := i + metaSize
		if end > len(entries) {
			end = len(entries)
		}
		b, _ := m.pos()
		starts = append(starts, b)
		m.write(entries[i:end])
		m.flushBlock()
	}
	blocks := m.out.Bytes()
	var lk bytes.Buffer
	for _, s := range starts {
		le64(&lk, uint64(s)) // relative; fixed up by caller
	}
	return blocks, lk.Bytes(), nil
}

// WriteSquashfs packs a tree into a SquashFS 4.0 gzip image (block size bs).
func WriteSquashfs(root *Node, bs uint32, mtime uint32) []byte {
	w := &writer{bs: bs, idIdx: map[uint32]uint16{}}
	// 1. file data, in tree order
	locs := map[*Node]fileLoc{}
	seen := map[[32]byte]fileLoc{} // identical files share their data
	var walk func(n *Node)
	walk = func(n *Node) {
		kids := append([]*Node(nil), n.Children...)
		sort.Slice(kids, func(i, j int) bool { return kids[i].Name < kids[j].Name })
		for _, k := range kids {
			if k.Type == tFile {
				sum := sha256.Sum256(k.Data)
				if l, ok := seen[sum]; ok && len(k.Data) > 0 {
					locs[k] = l
					continue
				}
				locs[k] = w.writeData(k.Data)
				seen[sum] = locs[k]
			}
		}
		for _, k := range kids {
			if k.Type == tDir {
				walk(k)
			}
		}
	}
	walk(root)
	w.flushFrag()
	total := countNodes(root)

	// 2. inodes and directories
	rblk, roff, _ := w.writeNode(root, locs, total+1)
	inodeTable := w.inodes.finish()
	dirTable := w.dirs.finish()

	// 3. assemble
	var img bytes.Buffer
	img.Write(make([]byte, 96))
	img.Write(w.data.Bytes())
	sb := superblock{Magic: sqMagic, InodeCount: total, ModTime: mtime, BlockSize: bs,
		FragCount: uint32(len(w.frags)), Compression: 1, Flags: flagNoXattr,
		VMajor: 4, VMinor: 0, XattrTable: noTable, ExportTable: noTable}
	for b := uint32(1); b < bs; b <<= 1 {
		sb.BlockLog++
	}
	sb.InodeTable = uint64(img.Len())
	img.Write(inodeTable)
	sb.DirTable = uint64(img.Len())
	img.Write(dirTable)
	sb.RootInode = uint64(rblk)<<16 | uint64(roff)

	writeLookup := func(entries []byte) uint64 {
		blocksStart := uint64(img.Len())
		blocks, lk, _ := w.table(entries)
		img.Write(blocks)
		tableStart := uint64(img.Len())
		for i := 0; i+8 <= len(lk); i += 8 {
			rel := binary.LittleEndian.Uint64(lk[i:])
			le64(&img, blocksStart+rel)
		}
		return tableStart
	}
	if len(w.frags) > 0 {
		var fe bytes.Buffer
		for _, f := range w.frags {
			le64(&fe, f.Start)
			le32(&fe, f.Size)
			le32(&fe, 0)
		}
		sb.FragTable = writeLookup(fe.Bytes())
	} else {
		sb.FragTable = noTable
		sb.Flags |= 0x0010 // no fragments
	}
	var ide bytes.Buffer
	for _, id := range w.ids {
		le32(&ide, id)
	}
	sb.IDCount = uint16(len(w.ids))
	sb.IDTable = writeLookup(ide.Bytes())
	sb.BytesUsed = uint64(img.Len())
	// pad to 4 KiB like mksquashfs
	if r := img.Len() % 4096; r != 0 {
		img.Write(make([]byte, 4096-r))
	}
	out := img.Bytes()
	var sbb bytes.Buffer
	binary.Write(&sbb, binary.LittleEndian, sb)
	copy(out, sbb.Bytes())
	return out
}
