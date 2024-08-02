"""
Functions for reading and writing FLAC streams.
"""

from _plibflac import __version__
from _plibflac import Error
from _plibflac import flac_vendor
from _plibflac import flac_version
from plibflac._decoder import Decoder
from plibflac._encoder import Encoder
