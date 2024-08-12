"""
Internal functions for writing FLAC streams.
"""

import _plibflac


class Encoder:
    def __init__(self, file, *,
                 channels=2,
                 bits_per_sample=16,
                 sample_rate=44100,
                 total_samples_estimate=None,
                 compression_level=None,
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
        if not self._opened:
            self._encoder.open()
            self._opened = True

    def close(self):
        if self._opened:
            self._encoder.close()
            self._opened = False
        if self._closefile:
            self._fileobj.close()
            self._closefile = False

    def write(self, samples):
        self.open()
        return self._encoder.write(samples)

    def _prop(name, doc=None):
        def fget(self):
            return getattr(self._encoder, name)

        def fset(self, value):
            setattr(self._encoder, name, value)

        return property(fget, fset, None, doc)

    channels = _prop('channels')
    bits_per_sample = _prop('bits_per_sample')
    sample_rate = _prop('sample_rate')
    total_samples_estimate = _prop('total_samples_estimate')
    streamable_subset = _prop('streamable_subset')
    verify = _prop('verify')
    compression_level = _prop('compression_level')
    blocksize = _prop('blocksize')
    do_mid_side_stereo = _prop('do_mid_side_stereo')
    loose_mid_side_stereo = _prop('loose_mid_side_stereo')
    apodization = _prop('apodization')
    max_lpc_order = _prop('max_lpc_order')
    qlp_coeff_precision = _prop('qlp_coeff_precision')
    do_qlp_coeff_prec_search = _prop('do_qlp_coeff_prec_search')
    do_exhaustive_model_search = _prop('do_exhaustive_model_search')
    min_residual_partition_order = _prop('min_residual_partition_order')
    max_residual_partition_order = _prop('max_residual_partition_order')
