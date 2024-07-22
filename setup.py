#!/usr/bin/env python3

import re

from setuptools import Extension, setup

with open('pyproject.toml') as f:
    _version = re.search('^version *= *"(.*?)"', f.read(), re.M)[1]

setup(
    name="plibflac",
    version=_version,
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
