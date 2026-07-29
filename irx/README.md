# Pinned SIO2 and storage IRX modules

These binaries are embedded in every SNESticle Revive build. The complete
SIO2 transport, memory-card and input group is kept together so an SDK update
cannot silently combine incompatible driver revisions. This also covers MMCE
and MX4SIO, which are not exported by every historical PS2SDK installation.

The five core PS2SDK modules and MX4SIO were rebuilt from commit `e228ff7b`;
each result was verified byte-identical to the official PS2DEV release.

| File | Size | SHA-256 | Upstream source / license |
|---|---:|---|---|
| `sio2man.irx` | 5,241 bytes | `44748d1c67b22132c026dd05bb06314bcbb5318a3f12835fd388f4e2b3126986` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/sio/sio2man), Academic Free License 2.0 |
| `mcman.irx` | 72,101 bytes | `5bb7d332523add2a834374998e5dd6268c9b8a05dbff3346bff334e7d2023dd7` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/memorycard/mcman), Academic Free License 2.0 |
| `mcserv.irx` | 8,197 bytes | `9f1b2ee6eb5f7c1f56ce225100824d85bc615eda3dac4f6be00b5f9f6d3c8924` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/memorycard/mcserv), Academic Free License 2.0 |
| `padman.irx` | 36,741 bytes | `463fcb30cc4192dce7b4a0ffb8b24b47b3cb0057908c58e4542edebb91e6898e` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/input/padman), Academic Free License 2.0 |
| `mtapman.irx` | 7,781 bytes | `dd8e131cb1911d5649452814e880089e94e10606109355774b8d1c7cbf044bdc` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/sio/mtapman), Academic Free License 2.0 |
| `mmceman.irx` | 17,033 bytes | `e6f3695dc8cbc3c63de567f292100b8ebea9fe08e3a9440da40f7b4c508d9df7` | [`ps2-mmce/mmceman` commit `db3e93f0`](https://github.com/ps2-mmce/mmceman/tree/db3e93f0fdbcf882f88da110cbd9b7db188ec17a), MIT. Rebuilt from that commit and verified byte-identical to the current [`wLaunchELF_R3Z` reference binary](https://github.com/saildot4k/wLaunchELF_R3Z/blob/6f35bccab2eb1fce4a039b4edb9406bca96ef733/iop/__precompiled/mmceman.irx). |
| `mx4sio_bd.irx` | 11,841 bytes | `761972f0154e9fcf4fde2b7feb69a923bee53f8592fcc5553027be32f1c4a991` | [`ps2dev/ps2sdk` commit `e228ff7b`](https://github.com/ps2dev/ps2sdk/tree/e228ff7b61a12ad1192a49338754534362a26e58/iop/sio/mx4sio_bd), Academic Free License 2.0. Rebuilt from that commit and verified byte-identical to the official PS2DEV release binary (MX4SIO v1.2). |

Every path remains overridable from the Make command line for driver
development. A normal build fails clearly if any pinned file is missing.
