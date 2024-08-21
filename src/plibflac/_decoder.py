"""
Internal functions for reading FLAC streams.
"""

import io

import _plibflac


class Decoder:
    """
    Decoder for a FLAC audio stream.

    A Decoder object reads compressed audio data from a FLAC file, and
    outputs the decompressed samples when `read` is called.

    To ensure resources are cleaned up, call `close` when the decoder
    is no longer needed, or use a ``with`` statement.

    Parameters
    ----------
    file : path-like object or binary file object
        Either the name of the input file, or an existing file object
        (which must be a readable binary file).
    md5_checking : bool, optional
        Whether to verify the stream's MD5 hash if possible.  If
        `md5_checking` is set to True, then `close` will raise an
        exception if the file appears to have been corrupted or
        truncated.  Most applications should leave this set to False
        for better performance.

    Attributes
    ----------
    channels : int
        The number of channels in the input stream (between one and
        eight).
    bits_per_sample : int
        The resolution of each sample in the input stream (between 4
        and 32).
    sample_rate : int
        The sampling frequency of the input stream, in samples per
        second.
    total_samples : int
        The length of the input stream, in samples (in other words,
        the total number of samples *per channel*).

    Notes
    -----
    Conceptually, a FLAC file can be thought of as a table of numbers,
    with up to eight columns (representing physical channels, such as
    left and right speakers) and any number of rows (representing
    equally-spaced points in time.)  Each number (sample) is an
    integer between ``-2**(bits_per_sample-1)`` and
    ``2**(bits_per_sample-1)-1``.

    Internally, the decoder maintains an input position corresponding
    to a sample number (or row in the table), starting at the
    beginning (sample number zero.)  When you call the `read` method,
    the specified number of samples (rows) are decoded and returned,
    and the input position is advanced.  To move the input position to
    a particular sample number, call the `seek` method.

    The `channels`, `bits_per_sample`, `sample_rate`, and
    `total_samples` attributes will all be zero until you call `seek`,
    `read`, or `read_metadata` for the first time.
    """

    def __init__(self, file, *, md5_checking=False):
        if isinstance(file, (str, bytes)) or hasattr(file, '__fspath__'):
            self._fileobj = open(file, 'rb')
            self._closefile = True
        else:
            self._fileobj = file
            self._closefile = False

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
            self.md5_checking = md5_checking
        except BaseException:
            if self._closefile:
                self._fileobj.close()
            raise

    def __enter__(self):
        self.open()
        self.read_metadata()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def open(self):
        """
        Initialize the decoder.

        The decoder will be initialized automatically if needed; most
        applications have no need to call this method.  If the decoder
        has already been opened, this method does nothing.
        """
        if not self._opened:
            try:
                if isinstance(self._fileobj, io.FileIO):
                    fd = self._fileobj.fileno()
                elif (isinstance(self._fileobj, (io.BufferedReader,
                                                 io.BufferedRandom))
                      and isinstance(self._fileobj.raw, io.FileIO)
                      and self._fileobj.seekable()):
                    fd = self._fileobj.fileno()
                    self._fileobj.seek(0, io.SEEK_CUR)
                else:
                    fd = -1
            except OSError:
                fd = -1
            self._decoder.open(fd)
            self._opened = True

    def close(self):
        """
        Close the decoder and free internal resources.

        If the Decoder was created by passing a filename, the input
        file will be closed.  If the Decoder was created by passing a
        file object, the caller is responsible for closing it.

        If the decoder has already been closed, this does nothing.

        This corresponds to the end of a ``with`` statement.

        Raises
        ------
        plibflac.error
            If the `md5_checking` property was set to True, and the
            input file appears corrupted.
        """
        try:
            if self._opened:
                self._opened = False
                self._decoder.close()
        finally:
            if self._closefile:
                self._closefile = False
                self._fileobj.close()

    def read_metadata(self):
        """
        Read and parse the stream metadata.

        When the decoder is first created, nothing is known about the
        input stream, and the `channels`, `bits_per_sample`,
        `sample_rate`, and `total_samples` attributes are set to zero.
        This method parses the stream metadata (without decoding any
        data) and sets those attributes accordingly.  If the stream
        metadata has already been parsed, this does nothing.

        This corresponds to the start of a ``with`` statement.

        Raises
        ------
        plibflac.error
            If the input does not contain a valid FLAC stream.
        """
        self.open()
        self._decoder.read_metadata()

    def read(self, n_samples):
        """
        Read and decode up to `n_samples` samples of each channel.

        This decodes and returns the next `n_samples` samples starting
        at the current input position.  All of the channels in the
        input stream are decoded together and returned as a group.

        When the end of the file is reached and there are no samples
        left, this method returns None.  Otherwise, the return value
        is a tuple of ``memoryview`` objects (one per channel); each
        of these is a one-dimensional array whose length is
        `n_samples` (or less, if the end of the file is reached.)

        Parameters
        ----------
        n_samples : int
            Maximum number of samples to return for each channel.

        Returns
        -------
        tuple of memoryviews, or None
            Arrays of decoded samples for each channel.

        Raises
        ------
        plibflac.error
            If the input stream is invalid and cannot be decoded.
        """
        self.open()
        return self._decoder.read(n_samples)

    def seek(self, sample_number):
        """
        Jump to a given sample number.

        This sets the input position so that a subsequent call to
        `read` will retrieve samples starting at `sample_number`.

        Parameters
        ----------
        sample_number : int
            New input sample number (zero is the start of the file).

        Raises
        ------
        plibflac.error
            If the input file is not seekable, or the given sample
            number is beyond the end of the file, or if the input
            stream is invalid and cannot be decoded.

        Notes
        -----
        If an exception is raised, the new input position is
        unspecified.
        """
        self.open()
        self._decoder.seek(sample_number)

    def _prop(name, doc=None):
        def _fget(self):
            return getattr(self._decoder, name)

        def _fset(self, value):
            setattr(self._decoder, name, value)

        return property(_fget, _fset, None, doc)

    channels = _prop(
        'channels',
        """
        Number of channels.

        A FLAC stream may contain between one and eight channels.
        This attribute is zero until you call `seek`, `read`, or
        `read_metadata` for the first time.
        """
    )
    bits_per_sample = _prop(
        'bits_per_sample',
        """
        Resolution of each sample in bits.

        The resolution of a FLAC stream can range from 4 to 32 bits
        per sample.  (All channels have the same resolution.)  This
        determines the range of sample values; each sample is between
        ``-2**(bits_per_sample-1)`` and ``2**(bits_per_sample-1)-1``.

        This attribute is zero until you call `seek`, `read`, or
        `read_metadata` for the first time.
        """
    )
    sample_rate = _prop(
        'sample_rate',
        """
        Sampling frequency, in samples per second (Hz).

        The sampling frequency of a FLAC stream can range from 1 to
        655350 samples per second.  (All channels have the same
        sampling frequency.)  This attribute is zero until you call
        `seek`, `read`, or `read_metadata` for the first time.
        """
    )
    total_samples = _prop(
        'total_samples',
        """
        Length of the stream, in samples.

        This attribute indicates the total number of samples for each
        channel in the stream.  (All channels have the same length.)
        This attribute is zero until you call `seek`, `read`, or
        `read_metadata` for the first time.
        """
    )
    md5_checking = _prop(
        'md5_checking',
        """
        True if the decoder should try to verify the stream's MD5 hash.

        A FLAC file may include an MD5 hash of the contents, which can
        be used to verify that the file hasn't been corrupted.  To
        verify the hash, the caller must set this attribute to True
        before calling `open`; then read the entire stream from
        beginning to end; then call `close`, which will raise an
        exception if the hash is wrong.

        This attribute must be set before opening the stream.
        """
    )
