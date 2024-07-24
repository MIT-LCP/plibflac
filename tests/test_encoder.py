#!/usr/bin/env python3

"""
Test cases for encoding using plibflac.
"""

import io
import unittest

import plibflac


class TestEncoder(unittest.TestCase):
    def test_write_empty(self):
        """
        Test encoding an empty stream into memory.
        """
        fileobj = io.BytesIO()

        encoder = plibflac.encoder(fileobj)
        encoder.open()
        encoder.close()

        fileobj.seek(0)
        decoder = plibflac.decoder(fileobj)
        decoder.open()

        decoder.read_metadata()
        # default is CD-format audio
        self.assertEqual(decoder.channels, 2)
        self.assertEqual(decoder.bits_per_sample, 16)
        self.assertEqual(decoder.sample_rate, 44100)
        self.assertEqual(decoder.total_samples, 0)

        data = decoder.read(1000)
        self.assertIsNone(data)

        decoder.close()


if __name__ == '__main__':
    unittest.main()
