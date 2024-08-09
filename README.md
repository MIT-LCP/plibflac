plibflac
========

This package provides a Python library for reading and writing audio
files in FLAC (Free Lossless Audio Codec) format.

`plibflac` is implemented as a wrapper around the reference FLAC
implementation (libFLAC) written by Josh Coalson and maintained by
Xiph.Org.


Installation
------------

Install this package from PyPI by running:
```
pip install plibflac
```

If you want to build the package from source (e.g., from the git
repository), you will need to have a C compiler and the Python headers
installed.


Project goals
-------------

The main goal of this project is to provide a portable and efficient
interface for reading and writing raw sample data, in FLAC format,
from Python applications.

This package currently does not implement the complete functionality
provided by the FLAC library.  If there are particular missing
features that your application needs, please report them via GitHub
issues.


Example
-------

In the code below, we first open a FLAC file and print the first 10
samples of each channel.  Next, we read the entire stream and write it
out to a new FLAC file (re-encoding it using compression level 8.)

```
import plibflac

with plibflac.Decoder("input.flac") as decoder:
    samples = decoder.read(10)
    for i, s in enumerate(samples):
        print("First 10 samples of channel {}: {}".format(i, list(s)))

    decoder.seek(0)
    with plibflac.Encoder("output.flac", compression_level=8,
                          channels=decoder.channels,
                          bits_per_sample=decoder.bits_per_sample,
                          sample_rate=decoder.sample_rate) as encoder:
        while data := decoder.read(1000):
            encoder.write(data)
```
