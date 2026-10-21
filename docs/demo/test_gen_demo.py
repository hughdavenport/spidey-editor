#!/usr/bin/env python3
"""gen_demo.py's files decode, through spidey_gfx's independent decoders, to the pictures it drew.

    python3 docs/demo/test_gen_demo.py
"""

import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", ".."))

import gen_demo  # noqa: E402
import spidey_gfx  # noqa: E402


class GenDemoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        gen_demo.main(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def read(self, name):
        with open(os.path.join(self.tmp.name, name), "rb") as f:
            return f.read()

    def test_foreground_tiles_round_trip(self):
        tiles = spidey_gfx.decode_tiles(self.read("BLOCKS.EGA"))
        self.assertEqual(tiles.shape, (512, 8, 8))
        for i, grid in enumerate(gen_demo.foreground_tiles()):
            self.assertEqual(tiles[i].tolist(), grid, f"tile {i:#x}")

    def test_scenery_tiles_round_trip(self):
        tiles = spidey_gfx.decode_tiles(self.read("SCENERY.EGA"))
        self.assertEqual(tiles.shape, (256, 8, 8))
        self.assertEqual(tiles[0x03].tolist(), gen_demo.SCENERY[0x03])

    def test_player_frame_is_the_standing_robot(self):
        frames = spidey_gfx.decode_sprites(self.read("SPIDS1.EGA"))
        self.assertEqual(frames.shape, (64, 21, 24))
        expected = gen_demo.picture(gen_demo.HERO_STAND, 24, 21)
        self.assertEqual(frames[24].tolist(), expected)   # render.c's PLAYER_FRAME
        self.assertEqual(frames[24][0][0], gen_demo.SPRITE_KEY)   # padding stays transparent

    def test_font_round_trips_in_both_modes(self):
        ega = spidey_gfx.decode_tiles(self.read("CHARSET.EGA"))
        self.assertEqual(ega.shape, (128, 8, 8))
        self.assertEqual(ega[0x41].tolist(), gen_demo.glyph("A"))
        cga = spidey_gfx.packed2(self.read("CHARSET.CGA")[0x41 * 16:0x42 * 16], 8, 8)
        self.assertEqual(cga.tolist(), gen_demo.glyph("A"))   # white (3) on black in both palettes

    def test_screens_round_trip_in_both_modes(self):
        for name, (grid, w, h) in gen_demo.screens().items():
            ega = spidey_gfx.decode_screen(self.read(name + ".EGA"), w, h)
            self.assertEqual(ega.tolist(), grid, name)
            cga = spidey_gfx.packed2(self.read(name + ".CGA"), w, h)
            self.assertEqual(cga.tolist(), [[gen_demo.CGA_OF[c] for c in row] for row in grid], name)

    def test_cga_sprites_carry_their_transparency_in_the_masks(self):
        stride = gen_demo.CGA_SPIDS_STRIDE
        frame = gen_demo.picture(gen_demo.HERO_STAND, 24, 21)
        pixels = spidey_gfx.packed2(self.read("SPIDS1.CGA")[24 * stride:25 * stride], 24, 21)
        mask = spidey_gfx.packed2(self.read("MASKS1.CGA")[24 * stride:25 * stride], 24, 21)
        for y in range(21):
            for x in range(24):
                if frame[y][x] == gen_demo.SPRITE_KEY:
                    self.assertEqual((mask[y][x], pixels[y][x]), (3, 0), (x, y))
                else:
                    self.assertEqual((mask[y][x], pixels[y][x]), (0, gen_demo.CGA_OF[frame[y][x]]), (x, y))

    def test_backgrounds_round_trip(self):
        backs = self.read("BACKS.SPL")
        self.assertEqual(spidey_gfx.backs_tilemap(backs, 1), gen_demo.night_sky())
        self.assertEqual(spidey_gfx.backs_tilemap(backs, 2), gen_demo.workshop())
        self.assertEqual(spidey_gfx.backs_tilemap(backs, 3), [0] * 704)   # absent block

    def test_movertab_follows_the_rooms_objects(self):
        table = self.read("MOVERTAB.DAT")
        self.assertEqual(len(table), 768)
        # Switch Demo: block 0, sprite 1, sprite 2; Store Room: sprite 0; Rooftop: sprite 0
        self.assertEqual(table[0:3], b"\x00\x00\x88")
        for n in range(1, 5):
            self.assertEqual(table[n * 3:n * 3 + 3], b"\x7f\xff\xb8", f"object {n}")
        self.assertEqual(table[15:], bytes(768 - 15))


if __name__ == "__main__":
    unittest.main()
