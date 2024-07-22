#!/usr/bin/env python3

"""
Low-level test cases for the _plibflac module.
"""

import gc
import os
import platform
import unittest
import weakref

import _plibflac


@unittest.skipUnless(platform.python_implementation() == 'CPython',
                     "relies on CPython garbage collector")
class TestGC(unittest.TestCase):
    def setUp(self):
        gc.disable()

    def tearDown(self):
        gc.enable()

    def test_decoder_simple(self):
        """
        Test decoder memory management using reference counting.

        This test creates a typical decoder object that points to a
        file object.  Upon deleting the variables, both objects should
        be freed, without needing to invoke the garbage collector.
        """
        fileobj = open(os.devnull, 'rb')
        decoder = _plibflac.decoder(fileobj)
        fileobj.close()
        fileobj_ref = weakref.ref(fileobj)
        del decoder, fileobj
        self.assertIsNone(fileobj_ref())

    def test_decoder_circular(self):
        """
        Test decoder memory management using garbage collection.

        This test creates a decoder object that points to a file
        object, which has an attribute pointing back to the decoder.
        Upon deleting the variables, both objects will still be alive
        (since they hold references to each other.)  Upon invoking the
        garbage collector, the objects should be freed.
        """
        fileobj = open(os.devnull, 'rb')
        decoder = _plibflac.decoder(fileobj)
        fileobj.close()
        fileobj_ref = weakref.ref(fileobj)
        fileobj.my_decoder = decoder
        del decoder, fileobj
        self.assertIsNotNone(fileobj_ref())
        gc.collect()
        self.assertIsNone(fileobj_ref())

    def test_encoder_simple(self):
        """
        Test encoder memory management using reference counting.

        This test creates a typical encoder object that points to a
        file object.  Upon deleting the variables, both objects should
        be freed, without needing to invoke the garbage collector.
        """
        fileobj = open(os.devnull, 'wb')
        encoder = _plibflac.encoder(fileobj)
        fileobj.close()
        fileobj_ref = weakref.ref(fileobj)
        del encoder, fileobj
        self.assertIsNone(fileobj_ref())

    def test_encoder_circular(self):
        """
        Test encoder memory management using garbage collection.

        This test creates an encoder object that points to a file
        object, which has an attribute pointing back to the encoder.
        Upon deleting the variables, both objects will still be alive
        (since they hold references to each other.)  Upon invoking the
        garbage collector, the objects should be freed.
        """
        fileobj = open(os.devnull, 'wb')
        encoder = _plibflac.encoder(fileobj)
        fileobj.close()
        fileobj_ref = weakref.ref(fileobj)
        fileobj.my_encoder = encoder
        del encoder, fileobj
        self.assertIsNotNone(fileobj_ref())
        gc.collect()
        self.assertIsNone(fileobj_ref())


if __name__ == '__main__':
    unittest.main()
