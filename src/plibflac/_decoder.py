"""
Internal functions for reading FLAC streams.
"""

import os

import _plibflac


class Decoder:
    def __init__(self, file, **options):
        try:
            path = os.fspath(file)
        except TypeError:
            self._fileobj = file
            self._closefile = False
        else:
            self._fileobj = open(path, 'rb')
            self._closefile = True

        self._opened = False

        if not (hasattr(self._fileobj, 'readinto') and
                hasattr(self._fileobj, 'readable') and
                hasattr(self._fileobj, 'seekable')):
            raise TypeError("file must be a filesystem path or a binary file "
                            "object, not {!r}".format(type(self._fileobj)))

        try:
            if not self._fileobj.readable():
                raise ValueError("file is not readable")

            self._decoder = _plibflac.decoder(self._fileobj)
            for name, value in options.items():
                setattr(self._decoder, name, value)
        except BaseException:
            if self._closefile:
                self._fileobj.close()
            raise

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def open(self):
        if not self._opened:
            self._decoder.open()
            self._opened = True

    def close(self):
        if self._opened:
            self._decoder.close()
            self._opened = False
        if self._closefile:
            self._fileobj.close()
            self._closefile = False

    def read_metadata(self):
        self.open()
        return self._decoder.read_metadata()

    def read(self, n_samples):
        self.open()
        return self._decoder.read(n_samples)

    def seek(self, sample_number):
        self.open()
        return self._decoder.seek(sample_number)

    def _prop(name, doc=None):
        def fget(self):
            return getattr(self._decoder, name)

        def fset(self, value):
            setattr(self._decoder, name, value)

        return property(fget, fset, None, doc)

    channels = _prop('channels')
    bits_per_sample = _prop('bits_per_sample')
    sample_rate = _prop('sample_rate')
    total_samples = _prop('total_samples')
