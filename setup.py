#!/usr/bin/env python3

from setuptools import Extension, setup

setup(
    name="plibflac",
    package_dir={'': 'src'},
    packages=["plibflac"],
    ext_modules=[
        Extension(
            name="_plibflac",
            sources=["src/_plibflacmodule.c"],
            libraries=["FLAC"],
        ),
    ],
)
