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
            data = decoder.read(1000)
            self.assertIsNone(data)


if __name__ == '__main__':
    unittest.main()
