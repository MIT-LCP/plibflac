#!/usr/bin/env python3

from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            name="plibflac._io",
            sources=["src/_iomodule.c"],
            libraries=["FLAC"],
        ),
    ]
)
