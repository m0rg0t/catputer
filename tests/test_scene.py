from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools import prepare_scene


@unittest.skipIf(prepare_scene.Image is None, "Pillow is required for scene asset tests")
class ScenePackingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        image = prepare_scene.Image
        assert image is not None
        self.background_path = self.root / "background.png"
        background = image.new("RGB", (480, 270))
        background.putdata(
            [
                ((x * 3) % 256, (y * 5) % 256, (x + y) % 256)
                for y in range(270)
                for x in range(480)
            ]
        )
        background.save(self.background_path)
        self.cat_path = self.root / "cat-sheet.png"
        cat_sheet = image.new("RGBA", (384, 288), (0, 0, 0, 0))
        for row in range(2):
            for column in range(3):
                left = column * 128
                top = row * 144
                for y in range(24, 120):
                    for x in range(24, 104):
                        cat_sheet.putpixel((left + x, top + y), (220, 130 + row * 10, 90 + column * 10, 255))
        cat_sheet.save(self.cat_path)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_pack_has_native_sizes_fixed_palette_and_transparency(self) -> None:
        data = prepare_scene.build_scene(self.background_path, self.cat_path)
        self.assertEqual(len(data["background_indices"]), 240 * 135)
        self.assertEqual(len(data["cat_indices"]), 6)
        self.assertTrue(all(len(frame) == 64 * 72 for frame in data["cat_indices"]))
        self.assertEqual(tuple(data["palette_rgb"][:16]), prepare_scene.UI_PALETTE_RGB)
        self.assertEqual(len(data["palette_rgb"]), 64)
        self.assertIn(255, data["cat_indices"][0])
        self.assertTrue(any(value >= 16 for value in data["background_indices"]))

    def test_pack_is_deterministic(self) -> None:
        first = prepare_scene.build_scene(self.background_path, self.cat_path)
        second = prepare_scene.build_scene(self.background_path, self.cat_path)
        self.assertEqual(first["background_indices"], second["background_indices"])
        self.assertEqual(first["cat_indices"], second["cat_indices"])
        self.assertEqual(first["palette_rgb"], second["palette_rgb"])

    def test_write_outputs_has_include_previews_and_manifest(self) -> None:
        data = prepare_scene.build_scene(self.background_path, self.cat_path)
        include = self.root / "firmware/generated_scene_data.inc"
        runtime = self.root / "runtime/imagegen-v1"
        manifest = prepare_scene.write_outputs(data, include, runtime, self.root)
        self.assertEqual(manifest["target"]["cat_frame_count"], 6)
        self.assertEqual(manifest["index_format"]["transparent_cat_index"], 255)
        self.assertEqual(json.loads((runtime / "scene-manifest.json").read_text())["asset_version"], "imagegen-v1")
        with prepare_scene.Image.open(runtime / "background-indexed.png") as rendered_background:
            self.assertEqual(rendered_background.size, (240, 135))
        with prepare_scene.Image.open(runtime / "cat-sheet-indexed.png") as rendered_cat:
            self.assertEqual(rendered_cat.size, (192, 144))
        with prepare_scene.Image.open(runtime / "scene-preview.png") as rendered_preview:
            self.assertEqual(rendered_preview.size, (240, 135))
        with prepare_scene.Image.open(runtime / "cat-sheet-indexed.png") as rendered_cat:
            # The source fixture gives every frame a different fill.  Checking
            # the center of each tile catches frame-major/scanline-major swaps.
            for frame_index in range(6):
                row, column = divmod(frame_index, 3)
                sheet_pixel = rendered_cat.getpixel((column * 64 + 32, row * 72 + 32))
                frame_pixel = data["cat_indices"][frame_index][32 * 64 + 32]
                self.assertEqual(sheet_pixel, frame_pixel)
        rendered = include.read_text()
        self.assertIn("kGeneratedSceneBackground[240 * 135]", rendered)
        self.assertIn("kGeneratedSceneCat[6][64 * 72]", rendered)
        self.assertIn("kGeneratedScenePalette[64]", rendered)
        self.assertIn("kGeneratedSceneOriginX", rendered)

    def test_rejects_wrong_background_aspect_ratio(self) -> None:
        image = prepare_scene.Image
        assert image is not None
        wrong = self.root / "wrong.png"
        image.new("RGB", (256, 256)).save(wrong)
        with self.assertRaisesRegex(prepare_scene.SceneError, "aspect ratio"):
            prepare_scene.build_scene(wrong, self.cat_path)

    def test_rejects_cat_without_alpha(self) -> None:
        image = prepare_scene.Image
        assert image is not None
        wrong = self.root / "wrong-cat.png"
        image.new("RGB", (192, 144)).save(wrong)
        with self.assertRaisesRegex(prepare_scene.SceneError, "alpha channel or"):
            prepare_scene.build_scene(self.background_path, wrong)

    def test_accepts_rgb_cat_sheet_with_magenta_chroma_key(self) -> None:
        image = prepare_scene.Image
        assert image is not None
        keyed = self.root / "keyed-cat.png"
        cat_sheet = image.new("RGB", (192, 144), (249, 3, 250))
        for row in range(2):
            for column in range(3):
                left = column * 64
                top = row * 72
                for y in range(16, 58):
                    for x in range(16, 48):
                        cat_sheet.putpixel((left + x, top + y), (220, 130, 90))
        cat_sheet.save(keyed)
        data = prepare_scene.build_scene(self.background_path, keyed)
        self.assertIn(255, data["cat_indices"][0])


if __name__ == "__main__":
    unittest.main()
