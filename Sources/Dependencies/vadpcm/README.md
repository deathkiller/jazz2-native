This directory vendors the VADPCM codec, the encoder half of the format the Nintendo 64 mixer decodes on the RSP.
AssetPacker uses it to write libdragon's `.wav64` files and the samples inside `.xm64` modules without libdragon's
`audioconv64` (see `Sources/Utilities/AssetPacker/Wav64Writer.cpp`).

Upstream repository: https://github.com/depp/vadpcm
Vendored from commit: `3c462832e84294166b2034b8c420de4150753d12`, by way of the copy libdragon keeps in
`tools/audioconv64/vadpcm` (libdragon commit `55aac1355773169d87e45424b772faf0dfb9432a`), which adds the
optional residual clamp of `struct vadpcm_params`.

Only the upstream `codec/` subtree is kept here (under `codec/`), unmodified. It is licensed under the Mozilla
Public License 2.0 (see `LICENSE.txt`), which applies to these files only.
