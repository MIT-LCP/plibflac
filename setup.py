#!/usr/bin/env python3

from setuptools import Extension, setup

setup(
    name="plibflac",
    packages=["plibflac"],
    ext_modules=[
        Extension(
            name="plibflac._io",
            sources=["_iomodule.c"],
            libraries=["FLAC"],
        ),
    ],
)
