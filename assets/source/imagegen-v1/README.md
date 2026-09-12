# Imagegen room and cat · v1

These original masters were created for Cardputer ADV Lofi with the **built-in OpenAI imagegen tool** on 2026-09-13. No CLI fallback was used. The room and orange tabby were designed for this project; no artwork from Lofi Girl or the earlier Lofi Cat project was supplied to the generator.

| Master | Purpose | Exact prompt |
| --- | --- | --- |
| [background.png](background.png) | Rainy room, warm lamp, books and an empty reading bench; 1672 × 941 | [Background prompt](background-prompt.txt) |
| [cat-sheet-original.png](cat-sheet-original.png) | Six cat poses in a 3 × 2 grid; 1536 × 1024 | [Cat prompt](cat-sheet-prompt.txt) |
| [cat-sheet.png](cat-sheet.png) | Selected sheet with a magenta key background for sprite conversion | [Background cleanup prompt](cat-key-prompt.txt) |

The first cat output painted a checkerboard instead of providing an alpha channel. A targeted imagegen edit replaced it with a magenta background and removed floating motion marks. The original output is retained. The converter keys the magenta with a fixed color tolerance because the generated background is visually uniform but is not exactly RGB 255,0,255. The indexed runtime PNG uses actual transparency.

Run `python3 tools/prepare_scene.py` from the repository root with the pinned Pillow dependency installed. It splits the six frames, preserves their aspect ratio, scales them into 64 × 72 containers, converts the room to 240 × 135, and jointly quantizes the artwork to 48 colors. Sixteen unchanged UI colors complete the 64-entry palette. All resizing uses nearest-neighbor sampling and palette conversion uses no dithering.

The generated C++ include is compiled into firmware flash. The runtime folder contains the indexed background, transparent cat sheet, palette, still composition and conversion manifest. They are preparation previews; screenshots and GIFs in `docs/media` are exported from the actual shared C++ renderer.

Frame order is left to right, top row then bottom row: resting, half-blink, closed blink, returning, upward glance/tail, rest variation. Playback spends most of its time in the resting pose, with short blinks and occasional movement. Still-motion mode fixes the scene.

[provenance.json](provenance.json) records source dimensions, hashes and the generation/edit sequence. [The runtime manifest](../../runtime/imagegen-v1/scene-manifest.json) records device dimensions, palette and placement. Exact prompts preserve the creative brief; image generation itself is not deterministic. The checked-in masters and pinned converter are the reproducible build inputs.
