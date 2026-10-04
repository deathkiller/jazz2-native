# AssetPacker for the web

The AssetPacker compiled to WebAssembly, with a page that swaps the game content of a Nintendo 64 (`.z64`), Dreamcast
(`.cdi`) or PlayStation 2 (`.iso`) image of Jazz² Resurrection. The user provides the image and the `Source` folder
of their copy of Jazz Jackrabbit 2; the page runs

    AssetPacker swap-content <image> <new image> --source=<Source> --content=/Content

in a worker, which converts the original game files for the console the image is for and writes a new image with
them. The game's own content (the repository's `Content`, without the `.po` sources) is baked into the module, and
nothing leaves the browser.

Nothing in the conversion needs a console SDK: the Nintendo 64 formats are written by encoders built into the tool
(`Wav64Writer`, `Xm64Converter`, `Mpeg1Encoder`) rather than by libdragon's `audioconv64`/`videoconv64` and `ffmpeg`.

## Building

With the Emscripten SDK activated (`source <emsdk>/emsdk_env.sh`):

    emcmake cmake -S . -B build/assetpacker-web -G Ninja -D CMAKE_BUILD_TYPE=Release -D NCINE_BUILD_GAME=OFF
    cmake --build build/assetpacker-web

`NCINE_BUILD_GAME=OFF` leaves the game out, so only the tool is built (the ShaderCompiler, the other offline tool,
is never built with Emscripten). libopenmpt and LZ4 are downloaded and compiled along with it
(`NCINE_DOWNLOAD_DEPENDENCIES`, on by default); `NCINE_CONTENT_DIR` names the content that is baked in.

The Emscripten workflow (`.github/workflows/emscripten.yml`) builds it the same way and uploads the files listed below
as the `Jazz2_AssetPacker_Web` artifact.

## Deploying

`build/assetpacker-web/Utilities/AssetPacker/` then holds everything the site needs, and can be served as it is:

| File | |
|---|---|
| `index.html` | The page (copied from this directory) |
| `assetpacker-worker.js` | The worker that runs the module (copied from this directory) |
| `AssetPacker.js`, `AssetPacker.wasm` | The module |
| `AssetPacker.data` | The baked-in game content, loaded by the module at start |

The files have to be served over HTTP(S) - a page opened from `file://` cannot start the worker - with `.wasm` as
`application/wasm`. No special headers are needed (the module uses no threads, so it does not depend on
`SharedArrayBuffer` and cross-origin isolation), so any static hosting works. For a quick local test:

    cd build/assetpacker-web/Utilities/AssetPacker && python3 -m http.server

## Notes

- The user's files are mounted read-only through `WORKERFS`, so they are read from disk as the tool needs them rather
  than copied into memory. The converted tree is kept in memory while the image is written (up to roughly 150 MB),
  and the new image is collected into blobs, which the browser may keep on disk.
- A Dreamcast image is about 740 MB, so the browser needs that much room for the result on top of the converted tree.
  A Nintendo 64 conversion takes the longest (around a minute on a desktop), as its music and cinematics are encoded
  for the console.
- The module can also be run with Node.js for testing, with host directories mounted through `NODEFS`.
