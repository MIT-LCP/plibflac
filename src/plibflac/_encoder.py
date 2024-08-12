"""
Internal functions for writing FLAC streams.
"""

import _plibflac


class Encoder:
    """
    Encoder for a FLAC audio stream.

    An Encoder object takes sequences of raw audio samples (passed to
    `write`), and writes them in compressed form to a FLAC file.

    To ensure that the output is fully written and resources are
    cleaned up, call `close` when the encoder is no longer needed, or
    use a ``with`` statement.

    Parameters
    ----------
    file : path-like object or binary file object
        Either the name of the output file, or an existing file object
        (which must be a writable binary file).
    channels : int, optional
        The number of channels in the output stream (between one and
        eight).
    bits_per_sample : int, optional
        The resolution of each sample in the output stream (between 4
        and 32).
    sample_rate : int, optional
        The sampling frequency of the output stream, in samples per
        second.
    total_samples_estimate : int, optional
        The estimated length of the output stream, in samples (in
        other words, the total number of samples *per channel*).
    compression_level : int, optional
        The compression level (between zero and eight).  Larger
        numbers will make encoding slower and result in a smaller
        output file.

    Other Parameters
    ----------------
    streamable_subset : bool, optional
        True to restrict compression options to the "Streamable
        Subset" of the FLAC format.
    verify : bool, optional
        True to verify the output as it is generated, for debugging.
    blocksize : int, optional
        The number of samples per channel per block.
    do_mid_side_stereo : bool, optional
        True to attempt use of difference formats for stereo streams;
        false to encode each channel independently.
    loose_mid_side_stereo : bool, optional
        True to adaptively select a stereo format; false to
        test all possible formats for every block.
    apodization : str, optional
        A string describing the windowing function(s) used for
        optimizing compression parameters.
    max_lpc_order : int, optional
        The maximum order for optimized linear predictors, or zero to
        use only fixed predictors.
    qlp_coeff_precision : int, optional
        The precision, in bits, of the optimized linear predictor
        coefficients.
    do_qlp_coeff_prec_search : bool, optional
        True to test multiple possible coefficient precision values;
        false to use only the specified precision.
    do_exhaustive_model_search : bool, optional
        True to exhaustively test possible LPC orders; false to
        estimate the best order.
    min_residual_partition_order : int, optional
        The minimum partition order for subdividing residual blocks.
    max_residual_partition_order : int, optional
        The maximum partition order for subdividing residual blocks.

    Notes
    -----
    Properties of the encoder (including the stream properties as well
    as compression options) can either be set as keyword arguments to
    the constructor, or can be set after creating the Encoder object
    and before calling the `open` or `write` method.  Properties
    cannot be changed after calling `open` or `write`.

    Setting the `compression_level` property sets default values for
    the other compression options, which can then be overridden.
    """
    def __init__(self, file, *,
                 channels=2,
                 bits_per_sample=16,
                 sample_rate=44100,
                 total_samples_estimate=None,
                 compression_level=5,
                 streamable_subset=True,
                 verify=False,
                 blocksize=None,
                 do_mid_side_stereo=None,
                 loose_mid_side_stereo=None,
                 apodization=None,
                 max_lpc_order=None,
                 qlp_coeff_precision=None,
                 do_qlp_coeff_prec_search=None,
                 do_exhaustive_model_search=None,
                 min_residual_partition_order=None,
                 max_residual_partition_order=None):
        if isinstance(file, (str, bytes)) or hasattr(file, '__fspath__'):
            self._fileobj = open(file, 'wb')
            self._closefile = True
        else:
            self._fileobj = file
            self._closefile = False

        self._opened = False

        if not (hasattr(self._fileobj, 'readinto') and
                hasattr(self._fileobj, 'writable') and
                hasattr(self._fileobj, 'seekable')):
            raise TypeError("file must be a filesystem path or a binary file "
                            "object, not {!r}".format(type(self._fileobj)))

        options = {
            'channels': channels,
            'bits_per_sample': bits_per_sample,
            'sample_rate': sample_rate,
            'total_samples_estimate': total_samples_estimate,
            'compression_level': compression_level,
            'streamable_subset': streamable_subset,
            'verify': verify,
            'blocksize': blocksize,
            'do_mid_side_stereo': do_mid_side_stereo,
            'loose_mid_side_stereo': loose_mid_side_stereo,
            'apodization': apodization,
            'max_lpc_order': max_lpc_order,
            'qlp_coeff_precision': qlp_coeff_precision,
            'do_qlp_coeff_prec_search': do_qlp_coeff_prec_search,
            'do_exhaustive_model_search': do_exhaustive_model_search,
            'min_residual_partition_order': min_residual_partition_order,
            'max_residual_partition_order': max_residual_partition_order,
        }

        try:
            if not self._fileobj.writable():
                raise ValueError("file is not writable")

            self._encoder = _plibflac.encoder(self._fileobj)
            for name, value in options.items():
                if value is not None:
                    setattr(self, name, value)
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
        """
        Initialize the encoder and write the file metadata.

        The encoder will be initialized automatically if needed; most
        applications have no need to call this method.  If the encoder
        has already been opened, this method does nothing.

        This corresponds to the start of a ``with`` statement.

        Raises
        ------
        plibflac.error
            If the encoder properties are invalid or inconsistent.
        """
        if not self._opened:
            self._encoder.open()
            self._opened = True

    def close(self):
        """
        Finish encoding and free internal resources.

        All remaining data that has not yet been encoded will be
        encoded and written to the output file, and the file metadata
        will be updated (if the output file is seekable.)  If the
        encoder has already been closed, this does nothing.

        If the Encoder was created by passing a filename, the output
        file will be flushed and closed.  If the Encoder was created
        by passing a file object, the caller is responsible for
        closing it.

        This corresponds to the end of a ``with`` statement.

        Raises
        ------
        plibflac.error
            If an error occurred while encoding the remaining output
            data.
        """
        try:
            if self._opened:
                self._opened = False
                self._encoder.close()
        finally:
            if self._closefile:
                self._closefile = False
                self._fileobj.close()

    def write(self, samples):
        """
        Encode and write data to the output file.

        The argument must contain an array of samples for every
        channel, which will be encoded and written as a group.  The
        encoder will automatically divide samples into blocks as
        needed; the length of the arrays need not be a multiple of
        `blocksize`, but must be the same for every channel.

        Each sample array must be a one-dimensional buffer of 32-bit
        signed integers.  Sample arrays may be of type
        ``numpy.ndarray``, ``array.array``, ``memoryview``, or similar
        types.

        Parameters
        ----------
        samples : sequence of array-like objects
            Sequence of sample arrays.

        Raises
        ------
        plibflac.error
            If an error occurred while encoding the output data.
        """
        self.open()
        self._encoder.write(samples)

    def _prop(name, doc=None):
        def _fget(self):
            return getattr(self._encoder, name)

        def _fset(self, value):
            setattr(self._encoder, name, value)

        return property(_fget, _fset, None, doc)

    channels = _prop(
        'channels',
        """
        Number of channels.

        A FLAC stream may contain between one and eight channels.  The
        default value is 2.  This attribute must be set before opening
        the stream.
        """
    )
    bits_per_sample = _prop(
        'bits_per_sample',
        """
        Resolution of each sample in bits.

        The resolution of a FLAC stream can range from 4 to 32 bits
        per sample.  (All channels have the same resolution.)  This
        determines the range of sample values; each sample must be
        between ``-2**(bits_per_sample-1)`` and
        ``2**(bits_per_sample-1)-1``.  The default resolution is 16
        bits.

        This attribute must be set before opening the stream.
        """
    )
    sample_rate = _prop(
        'sample_rate',
        """
        Sampling frequency, in samples per second (Hz).

        The sampling frequency of a FLAC stream can range from 1 to
        655350 samples per second.  (All channels have the same
        sampling frequency.)  The default value is 44100.

        This attribute must be set before opening the stream.
        """
    )
    total_samples_estimate = _prop(
        'total_samples_estimate',
        """
        Estimated length of the stream, in samples.

        This attribute must be set before opening the stream.
        """
    )
    compression_level = _prop(
        'compression_level',
        """
        Level of compression to perform.

        The compression level is an integer between zero and eight.
        This corresponds to the ``-0`` through ``-8`` options for the
        ``flac`` command-line tool.

        Each level corresponds to a predefined set of compression
        options, where level 0 gives the fastest and (usually) worst
        compression, and level 8 gives the slowest and (usually) best
        compression.  Details of the compression levels can be found
        in the FLAC documentation, and may be subject to change
        between releases.

        This attribute must be set before opening the stream.
        """
    )
    streamable_subset = _prop(
        'streamable_subset',
        """
        True to use only the "Streamable Subset" of the FLAC format.

        Setting this to False corresponds to the ``--lax`` option for
        the ``flac`` command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    verify = _prop(
        'verify',
        """
        True to verify the output as it is generated, for debugging.

        This attribute must be set before opening the stream.
        """
    )
    blocksize = _prop(
        'blocksize',
        """
        Number of samples per channel per block.

        This corresponds to the ``-b`` option for the ``flac``
        command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    do_mid_side_stereo = _prop(
        'do_mid_side_stereo',
        """
        True to enable use of difference formats for stereo streams.

        Together with `loose_mid_side_stereo`, this corresponds to the
        ``-m`` or ``-M`` option for the ``flac`` command-line tool.
        This attribute has no effect unless `channels` equals 2.

        This attribute must be set before opening the stream.
        """
    )
    loose_mid_side_stereo = _prop(
        'loose_mid_side_stereo',
        """
        True to adaptively select a stereo format.

        True corresponds to the ``-M`` option for the ``flac``
        command-line tool, and False corresponds to the ``-m`` option.
        This attribute has no effect unless `channels` equals 2 and
        `do_mid_side_stereo` is True.

        This attribute must be set before opening the stream.
        """
    )
    apodization = _prop(
        'apodization',
        """
        Windowing function(s) used for optimizing compression parameters.

        Multiple functions can be specified, separated by semicolons.
        This corresponds to the ``-A`` option for the ``flac``
        command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    max_lpc_order = _prop(
        'max_lpc_order',
        """
        Maximum order for optimized linear predictors.

        This corresponds to the ``-l`` option for the ``flac``
        command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    qlp_coeff_precision = _prop(
        'qlp_coeff_precision',
        """
        Precision of optimized linear predictor coefficients.

        This corresponds to the ``-q`` option for the ``flac``
        command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    do_qlp_coeff_prec_search = _prop(
        'do_qlp_coeff_prec_search',
        """
        True to test multiple possible coefficient precision values.

        This corresponds to the ``-p`` option for the ``flac``
        command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    do_exhaustive_model_search = _prop(
        'do_exhaustive_model_search',
        """
        True to exhaustively test possible LPC orders.

        This corresponds to the ``-e`` option for the ``flac``
        command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    min_residual_partition_order = _prop(
        'min_residual_partition_order',
        """
        Minimum partition order for subdividing residual blocks.

        Together with `max_residual_partition_order`, this corresponds
        to the ``-r`` option for the ``flac`` command-line tool.

        This attribute must be set before opening the stream.
        """
    )
    max_residual_partition_order = _prop(
        'max_residual_partition_order',
        """
        Maximum partition order for subdividing residual blocks.

        Together with `min_residual_partition_order`, this corresponds
        to the ``-r`` option for the ``flac`` command-line tool.

        This attribute must be set before opening the stream.
        """
    )
