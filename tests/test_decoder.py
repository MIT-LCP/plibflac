#!/usr/bin/env python3

"""
Test cases for decoding using plibflac.
"""

import io
import os
import threading
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
        with plibflac.Decoder(self.data_path('100s.flac')) as decoder:
            self.assertEqual(decoder.channels, 2)
            self.assertEqual(decoder.sample_rate, 96000)
            self.assertEqual(decoder.bits_per_sample, 16)
            self.assertEqual(decoder.total_samples, 650000)

    def test_read_path(self):
        """
        Test reading from a filesystem path.
        """
        with plibflac.Decoder(self.data_path('100s.flac')) as decoder:
            self._test_read_sequential(decoder)

    def test_read_fileobj(self):
        """
        Test reading from a regular file object.
        """
        with open(self.data_path('100s.flac'), 'rb') as fileobj:
            with plibflac.Decoder(fileobj) as decoder:
                self._test_read_sequential(decoder)

            # exiting decoder context doesn't close file
            fileobj.seek(0)
            with plibflac.Decoder(fileobj) as decoder:
                self._test_read_sequential(decoder)

    def test_read_bytesio(self):
        """
        Test reading from a BytesIO object.
        """
        with open(self.data_path('100s.flac'), 'rb') as fileobj:
            memfileobj = io.BytesIO(fileobj.read())

        with plibflac.Decoder(memfileobj) as decoder:
            self._test_read_sequential(decoder)

    def test_read_pipe(self):
        """
        Test reading from a pipe.
        """
        rfd, wfd = os.pipe()
        with open(rfd, 'rb') as rpipe, open(wfd, 'wb') as wpipe:
            def copy_data():
                try:
                    with open(self.data_path('100s.flac'), 'rb') as fileobj:
                        wpipe.write(fileobj.read())
                finally:
                    wpipe.close()
            t = threading.Thread(target=copy_data)
            t.start()
            with plibflac.Decoder(rpipe) as decoder:
                self._test_read_sequential(decoder)
                with self.assertRaises(plibflac.Error):
                    decoder.seek(0)
            t.join()

    def _test_read_sequential(self, decoder):
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

    def test_read_random(self):
        """
        Test reading a FLAC file non-sequentially.
        """
        with plibflac.Decoder(self.data_path('100s.flac')) as decoder:
            decoder.seek(5000)
            samples_1b = decoder.read(5000)

            with self.assertRaises(plibflac.Error):
                decoder.seek(2 * decoder.total_samples)

            decoder.seek(0)
            samples_1a = decoder.read(5000)

        with plibflac.Decoder(self.data_path('100s.flac')) as decoder:
            samples_2 = decoder.read(10000)

        for s1a, s1b, s2 in zip(samples_1a, samples_1b, samples_2):
            self.assertEqual(list(s1a) + list(s1b), list(s2))

    def test_properties(self):
        """
        Test setting decoder properties.
        """
        # By default MD5 checking is disabled
        with plibflac.Decoder(self.data_path('100s.flac')) as decoder:
            with self.assertRaises(ValueError):
                # Setting property after open is forbidden
                decoder.md5_checking = True
            self.assertEqual(decoder.md5_checking, False)
            decoder.read(decoder.total_samples // 2)
            decoder.close()

        # MD5 checking should succeed if we read the entire file
        with plibflac.Decoder(self.data_path('100s.flac'),
                              md5_checking=True) as decoder:
            self.assertEqual(decoder.md5_checking, True)
            decoder.read(decoder.total_samples)
            decoder.close()

        # If we set md5_checking=True in constructor, and don't read
        # entire file, it should fail
        with plibflac.Decoder(self.data_path('100s.flac'),
                              md5_checking=True) as decoder:
            self.assertEqual(decoder.md5_checking, True)
            decoder.read(decoder.total_samples // 2)
            with self.assertRaises(plibflac.Error):
                decoder.close()

        # If we set md5_checking property before open, it should fail
        decoder = plibflac.Decoder(self.data_path('100s.flac'))
        decoder.md5_checking = True
        with decoder:
            self.assertEqual(decoder.md5_checking, True)
            decoder.read(decoder.total_samples // 2)
            with self.assertRaises(plibflac.Error):
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
