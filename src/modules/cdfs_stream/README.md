# Streaming CDFS driver

This is a modified copy of PS2SDK's `iop/cdvd/cdfs` module from commit
`f08e889fef8ab361f863c44ebe78212ced2839ca` (CDFS 2.2), licensed under the
Academic Free License 2.0. The upstream copyright and full license are
preserved in this directory.

The stock module reads a directory into `TocEntry entries[256]` in `dopen`,
which silently prevents `dread` from exposing later files. This fork replaces
that array with a 16 KiB ISO9660/Joliet sector window per open directory and
parses one record at a time. It therefore has no entry-count ceiling and uses
less IOP memory than the old four 256-entry tables.

This fork also replaces the inherited blocking `sceCdDiskReady(0)` and
`sceCdSync(0)` calls (plus their 32-by-32 retry cascade) with bounded polling.
A timed-out command is aborted and returned to iomanX as an I/O error instead
of freezing the caller or pretending that a stale/zeroed sector was read.
Directory reads distinguish end-of-directory from media errors, `getstat`
uses normal ioman return values, and ISO9660/Joliet descriptors and filenames
are bounds-checked before being exported.

The prebuilt result is `irx/cdfs_stream.irx` and is embedded in the main ELF.
To reproduce it from a PS2SDK source checkout, replace that checkout's
`iop/cdvd/cdfs` directory with this directory (keeping the `src/` layout), then
run:

```sh
make -C iop/cdvd/cdfs clean all PS2SDKSRC="$PWD"
```
