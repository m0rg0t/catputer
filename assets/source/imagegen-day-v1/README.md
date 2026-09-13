# Sunny room · imagegen-day-v1

The daytime master was created with the **built-in OpenAI imagegen tool** on 2026-09-13, editing the original Catputer night background. No CLI fallback was used. It keeps the window, bench and empty cat seat while adding a blue sky, sun and warm daylight. No external character or room artwork was supplied.

- [Final background](background.png): 1672 × 941 RGB.
- [Exact generation prompt](background-prompt.txt).
- [Source provenance and hashes](provenance.json).
- [Unchanged six-pose cat source](../imagegen-v1/cat-sheet.png).

The shared renderer uses the day scene for Rainy and Sunny, and the original night scene for Cozy and Night. Rainy adds moving rain; Sunny has no rain. The scene follows the audible mood at its musical boundary. Both scenes are bundled in flash and need no SD card.

Rebuild the day assets from the repository root with the pinned Pillow dependency:

```sh
python3 tools/prepare_scene.py \
  --background assets/source/imagegen-day-v1/background.png \
  --cat-sheet assets/source/imagegen-v1/cat-sheet.png \
  --version imagegen-day-v1 \
  --output-include firmware/src/ui/generated_day_scene_data.inc \
  --runtime-dir assets/runtime/imagegen-day-v1 \
  --symbol-prefix kGeneratedDayScene
```

The converter resizes with nearest-neighbor sampling and no dithering, jointly quantizes the room and cat to 48 art colors, and keeps all 16 UI colors unchanged. The 240 × 135 background and six 64 × 72 cat frames use a separate day palette. Transparency masks and cat placement match the night scene. Each frame selects its own palette; there is still only one indexed framebuffer on the device.

[Runtime preparation preview](../../runtime/imagegen-day-v1/scene-preview.png) and [conversion manifest](../../runtime/imagegen-day-v1/scene-manifest.json) describe the assets. The screenshots and GIFs in `docs/media` are captures of the shared C++ renderer, separately labeled as desktop evidence. Image generation is not deterministic; the checked-in master, cat source and pinned converter are the reproducible inputs.
