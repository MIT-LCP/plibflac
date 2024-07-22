#!/usr/bin/env python3

from setuptools import Extension, setup

setup(
    name="plibflac",
    packages=["plibflac"],
    ext_modules=[
        Extension(
            name="_plibflac",
            sources=["_plibflacmodule.c"],
            libraries=["FLAC"],
        ),
    ],
)
