# Prebaked GXP shader cache (PS Vita)

`ShadersGxm.bin` is a pack of **compiled** sceGxm shader binaries. It is packaged into the VPK at
`Content/Shaders/ShadersGxm.bin`, which is where `GxmShaderCache::PrebakedPath` looks for it, and only
the Vita build packages it (see `cmake/ncine_extra_sources.cmake`) — a GXP is Vita machine code.

It exists because there is no offline sceGxm shader compiler: the Cg sources in
`Sources/Shaders/Generated/CgGeneratedShaders.h` are compiled on the console by `libshacccg.suprx`,
which cost about **3.3 s of a 6.8 s cold start**. With the pack shipped, that becomes a few ms.

## Refreshing it

The pack can only be produced by a console, so the flow is: run the game, then pull back what it wrote.

1. Run the Vita build and go through the parts of the game you want covered — see *Coverage* below.
2. Copy `ux0:/data/jazz2/Cache/Shaders/ShadersGxm.bin` off the console (vitacompanion's FTP on port
   1337, or any other means) over this file.
3. Commit it.

Nothing needs to be kept in sync by hand. Each entry is keyed by a 64-bit hash of the exact Cg source
string it was compiled from, so:

- a pack that predates a `.shader` change is **incomplete, not wrong** — the changed stages miss and are
  recompiled on the console, everything else is still a hit;
- a stale entry cannot be *found*, only orphaned, and orphans are dropped when the console rewrites its
  own pack past `PruneThreshold`;
- a pack built by a **different `libshacccg.suprx`** is rejected as a whole, by the compiler fingerprint
  in its header, rather than handing one driver another compiler's output.

A pack that is missing, truncated or rejected just means the console compiles what it needs, so this file
is never something a build has to have.

## Coverage

A run only compiles the stages it actually reaches, and the pack is written from what the run used, so
what you play is what gets cached. The 51 entries committed here come from a run that reached the main
menu; playing further and re-pulling can only add to them.

**Commit the file exactly as the console wrote it — do not try to "clean" it.** It is tempting to drop
entries that no source in `CgGeneratedShaders.h` hashes to, on the assumption they are leftovers from an
older revision of a `.shader`. That is wrong twice over, and a pack regenerated from an empty cache
proves it - every one of the following is in it:

- **the batched shaders**, whose source is built at *runtime*: `GxmShaderProgram::PatchBatchSize()`
  rewrites the `#define BATCH_SIZE <n>` line to the batch size the device settled on (32, from
  `GxmRhiCapabilities`) and the key hashes the *patched* string, so it matches nothing in the header;
- **the device's own clear and present shaders**, whose four Cg sources are string literals in
  `GxmDevice.cpp` (`ClearVertexSource`, `ClearFragmentSource`, `PresentVertexSource`,
  `PresentFragmentSource`) and are not generated artifacts at all.

Pruning against the header alone deletes precisely those - the batched vertex stages and the two shaders
every single frame goes through - which is the opposite of the intent.

Changing the batch ceiling changes every batched shader's key, so the old entries stop being findable and
their replacements are compiled on the next run. The tidy way to refresh after such a change is to delete
this file *and* the console's own pack and let one run rebuild it from nothing, rather than editing the
binary.

The same reasoning is why partial coverage is not worth chasing hard: anything absent compiles on first
use and lands in the console's own writable pack, so a gap costs a one-off hitch rather than correctness.
Two groups are simply unreachable on this platform and will never appear in a pack:

- the nine rescale shaders and `Antialiasing` — `DISABLE_RESCALE_SHADERS` is forced ON for Vita (see
  `cmake/ncine_options.cmake`), so *Options > Graphics > Rescale Mode* has nothing to select;
- `DefaultImGui` — `NCINE_WITH_IMGUI` is off by default.
