#!/usr/bin/env python3

"""
Test cases for decoding using plibflac.
"""

import os
import unittest

import plibflac


class TestDecoder(unittest.TestCase):
    def test_read_null(self):
        """
        Test reading /dev/null as a raw FLAC stream.
        """
        with open(os.devnull, 'rb') as fileobj:
            decoder = plibflac.decoder(fileobj)
            decoder.open()
            data = decoder.read(1000)
            self.assertIsNone(data)
            decoder.close()

    def test_read_metadata(self):
        """
        Test reading metadata from a FLAC file.
        """
        with open(self.data_path('100s.flac'), 'rb') as fileobj:
            decoder = plibflac.decoder(fileobj)
            decoder.open()

            self.assertEqual(decoder.channels, 0)
            self.assertEqual(decoder.sample_rate, 0)
            self.assertEqual(decoder.bits_per_sample, 0)

            decoder.read_metadata()

            self.assertEqual(decoder.channels, 2)
            self.assertEqual(decoder.sample_rate, 96000)
            self.assertEqual(decoder.bits_per_sample, 16)

            decoder.close()

    def data_path(self, name):
        return os.path.join(os.path.dirname(__file__), 'data', name)


if __name__ == '__main__':
    unittest.main()
