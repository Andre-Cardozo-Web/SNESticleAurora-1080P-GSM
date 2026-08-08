# Pinned SIO2 and storage IRX modules

These binaries are embedded in every SNESticle Revive build. The complete
SIO2 transport, memory-card and input group is kept together so an SDK update
cannot silently combine incompatible driver revisions. This also covers MMCE
and MX4SIO, which are not exported by every historical PS2SDK installation.

The five core PS2SDK modules and MX4SIO were rebuilt from commit `e228ff7b`;
each result was verified byte-identical to the official PS2DEV release.

The complete USB/BDM group is pinned for the same reason. In particular,
`usbd_mini.irx` is the pre-rewrite FreeUsbd implementation restored for OPL
and other BDM loaders, rather than the newer full `usbd.irx` that regressed on
some real USB devices. The three BDM/FatFs modules were taken together from
PS2SDK commit `f08e889f` and verified byte-identical to its official PS2DEV
release. `bdmfs_fatfs.irx` was built with FAT16, FAT32 and exFAT enabled.

| File | Size | SHA-256 | Upstream source / license |
|---|---:|---|---|
| `sio2man.irx` | 5,241 bytes | `44748d1c67b22132c026dd05bb06314bcbb5318a3f12835fd388f4e2b3126986` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/sio/sio2man), Academic Free License 2.0 |
| `mcman.irx` | 72,101 bytes | `5bb7d332523add2a834374998e5dd6268c9b8a05dbff3346bff334e7d2023dd7` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/memorycard/mcman), Academic Free License 2.0 |
| `mcserv.irx` | 8,197 bytes | `9f1b2ee6eb5f7c1f56ce225100824d85bc615eda3dac4f6be00b5f9f6d3c8924` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/memorycard/mcserv), Academic Free License 2.0 |
| `padman.irx` | 36,741 bytes | `463fcb30cc4192dce7b4a0ffb8b24b47b3cb0057908c58e4542edebb91e6898e` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/input/padman), Academic Free License 2.0 |
| `mtapman.irx` | 7,781 bytes | `dd8e131cb1911d5649452814e880089e94e10606109355774b8d1c7cbf044bdc` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/sio/mtapman), Academic Free License 2.0 |
| `mmceman.irx` | 17,033 bytes | `e6f3695dc8cbc3c63de567f292100b8ebea9fe08e3a9440da40f7b4c508d9df7` | [`ps2-mmce/mmceman` commit `db3e93f0`](https://github.com/ps2-mmce/mmceman/tree/db3e93f0fdbcf882f88da110cbd9b7db188ec17a), MIT. Rebuilt from that commit and verified byte-identical to the current [`wLaunchELF_R3Z` reference binary](https://github.com/saildot4k/wLaunchELF_R3Z/blob/6f35bccab2eb1fce4a039b4edb9406bca96ef733/iop/__precompiled/mmceman.irx). |
| `mx4sio_bd.irx` | 11,841 bytes | `761972f0154e9fcf4fde2b7feb69a923bee53f8592fcc5553027be32f1c4a991` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/sio/mx4sio_bd), Academic Free License 2.0. Rebuilt from that commit and verified byte-identical to the official PS2DEV release binary (MX4SIO v1.2). |
| `cdfs_stream.irx` | 11,313 bytes | `d6b8fd4d983743b6f936e81d4de2785cc13b8f6e2ea2dc28861fce3372b432d5` | Streaming fork of [`ps2dev/ps2sdk` commit `f08e889f`](https://github.com/ps2dev/ps2sdk/tree/f08e889fef8ab361f863c44ebe78212ced2839ca/iop/cdvd/cdfs), Academic Free License 2.0. Modified source and build notes are in `src/modules/cdfs_stream/`. |
| `usbd_mini.irx` | 21,877 bytes | `04e34ef54c5e2f12c299db01f93dd7fce940df944d1c1dcfa1a696fd7fdf24ca` | FreeUsbd restored by [`ps2dev/ps2sdk` commit `af80575`](https://github.com/ps2dev/ps2sdk/tree/af80575ff01e5bc61662cfb0aa756b9189e113e9/iop/usb/usbd_mini), based on the last pre-rewrite tree (`2dc6b32f`) with USBD 1.2 compatibility stubs; Academic Free License 2.0. Rebuilt from that exact commit. |
| `bdm.irx` | 10,745 bytes | `1059a8edc4cc9971ac6b462dd4420756163b87a5c017ddac36395e309d15e6c4` | [`ps2dev/ps2sdk` commit `f08e889f`](https://github.com/ps2dev/ps2sdk/tree/f08e889fef8ab361f863c44ebe78212ced2839ca/iop/fs/bdm), Academic Free License 2.0. |
| `bdmfs_fatfs.irx` | 36,205 bytes | `597b23addfdb35346544d742d9e4e67edadb3b064e7c03b9784fbf71cb9e37d6` | [`ps2dev/ps2sdk` commit `f08e889f`](https://github.com/ps2dev/ps2sdk/tree/f08e889fef8ab361f863c44ebe78212ced2839ca/iop/fs/bdmfs_fatfs), FatFs R0.15 + Academic Free License 2.0; FAT16/FAT32/exFAT enabled. |
| `usbmass_bd.irx` | 12,681 bytes | `fcfb583298cda02064b38146de465df0dc708d5b364f553700138384bc74276b` | [`ps2dev/ps2sdk` commit `f08e889f`](https://github.com/ps2dev/ps2sdk/tree/f08e889fef8ab361f863c44ebe78212ced2839ca/iop/usb/usbmass_bd), Academic Free License 2.0. |

Every path remains overridable from the Make command line for driver
development. A normal build fails clearly if any pinned file is missing.
