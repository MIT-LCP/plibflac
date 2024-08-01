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
            decoder = plibflac.Decoder(fileobj)
            decoder.open()
            data = decoder.read(1000)
            self.assertIsNone(data)
            decoder.close()

    def test_read_metadata(self):
        """
        Test reading metadata from a FLAC file.
        """
        with open(self.data_path('100s.flac'), 'rb') as fileobj:
            decoder = plibflac.Decoder(fileobj)
            decoder.open()

            self.assertEqual(decoder.channels, 0)
            self.assertEqual(decoder.sample_rate, 0)
            self.assertEqual(decoder.bits_per_sample, 0)
            self.assertEqual(decoder.total_samples, 0)

            decoder.read_metadata()

            self.assertEqual(decoder.channels, 2)
            self.assertEqual(decoder.sample_rate, 96000)
            self.assertEqual(decoder.bits_per_sample, 16)
            self.assertEqual(decoder.total_samples, 650000)

            decoder.close()

    def test_read_sequential(self):
        """
        Test reading a FLAC file sequentially.
        """
        with open(self.data_path('100s.flac'), 'rb') as fileobj:
            decoder = plibflac.Decoder(fileobj)
            decoder.open()

            # Samples 0 to 10 (unbuffered)
            samples = decoder.read(10)
            self.assertEqual([list(x) for x in samples], [
                [995, 995, 995, 995, 995, 995, 995, 995, 1000, 997],
                [1011, 1011, 1011, 1011, 1011, 1011, 1011, 1011, 1008, 1008],
            ])

            # Samples 10 to 20 (buffered)
            samples = decoder.read(10)
            self.assertEqual([list(x) for x in samples], [
                [995, 994, 992, 993, 992, 989, 988, 987, 990, 993],
                [1007, 1007, 1009, 1010, 1010, 1011, 1013, 1014, 1014, 1016],
            ])

            # Samples 20 to 30 (buffered)
            samples = decoder.read(10)
            self.assertEqual([list(x) for x in samples], [
                [989, 988, 986, 988, 993, 997, 993, 986, 983, 977],
                [1016, 1013, 1009, 1008, 1007, 1010, 1008, 1008, 1006, 1005],
            ])

            # Samples 4090 to 4100 (partially buffered)
            decoder.read(4060)
            samples = decoder.read(10)
            self.assertEqual([list(x) for x in samples], [
                [971, 975, 978, 976, 976, 976, 975, 980, 980, 981],
                [979, 983, 984, 982, 980, 982, 981, 986, 986, 987],
            ])

            # Samples 4100 to 650000
            samples = decoder.read(decoder.total_samples)
            self.assertEqual(len(samples[0]), decoder.total_samples - 4100)

            # End of file
            samples = decoder.read(10)
            self.assertIsNone(samples)

            decoder.close()

    def data_path(self, name):
        return os.path.join(os.path.dirname(__file__), 'data', name)


class TestMisc(unittest.TestCase):
    def test_version(self):
        """
        Test package version information.
        """
        self.assertIsInstance(plibflac.flac_version(), str)
        self.assertIsInstance(plibflac.flac_vendor(), str)


if __name__ == '__main__':
    unittest.main()
