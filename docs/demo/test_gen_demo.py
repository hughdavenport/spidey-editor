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
