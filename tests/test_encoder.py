#!/usr/bin/env python3

"""
Test cases for encoding using plibflac.
"""

import array
import io
import random
import unittest

import plibflac


def _random_array(seed, length, min_value, max_value):
    rng = random.Random(seed)
    samples = (rng.randint(min_value, max_value) for _ in range(length))
    return array.array('i', samples)


class TestEncoder(unittest.TestCase):
    def test_write_empty(self):
        """
        Test encoding an empty stream into memory.
        """
        fileobj = io.BytesIO()

        encoder = plibflac.Encoder(fileobj)
        encoder.open()
        encoder.close()

        fileobj.seek(0)
        decoder = plibflac.Decoder(fileobj)
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

    def test_write_random(self):
        """
        Test encoding some random sample data into memory.
        """
        fileobj = io.BytesIO()

        rng = random.Random(1)
        channel0 = _random_array(1234, 5000, -32768, 32767)
        channel1 = _random_array(5678, 5000, -32768, 32767)

        encoder = plibflac.Encoder(fileobj)
        encoder.open()
        encoder.write([channel0, channel1])
        encoder.close()

        fileobj.seek(0)
        decoder = plibflac.Decoder(fileobj)
        decoder.open()

        decoder.read_metadata()
        # default is CD-format audio
        self.assertEqual(decoder.channels, 2)
        self.assertEqual(decoder.bits_per_sample, 16)
        self.assertEqual(decoder.sample_rate, 44100)
        self.assertEqual(decoder.total_samples, 5000)

        data = decoder.read(5000)
        self.assertEqual(len(data), 2)
        self.assertEqual(data[0], channel0)
        self.assertEqual(data[1], channel1)

        data = decoder.read(1000)
        self.assertIsNone(data)

        decoder.close()


if __name__ == '__main__':
    unittest.main()
