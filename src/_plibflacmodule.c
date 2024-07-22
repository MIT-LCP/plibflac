#include <stddef.h>
#include <string.h>
#include <limits.h>

#include <Python.h>
#include <structmember.h>

#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>

/* PyBUF_READ and PyBUF_WRITE were not formally added to the limited
   API until 3.11, but PyMemoryView_FromMemory is stable since 3.3
   (https://github.com/python/cpython/issues/98680) */
#ifndef PyBUF_READ
# define PyBUF_READ 0x100
#endif
#ifndef PyBUF_WRITE
# define PyBUF_WRITE 0x200
#endif

/****************************************************************/

#if INT_MAX == 0x7fffffff
# define INT32_FORMAT "i"
#elif LONG_MAX == 0x7fffffff
# define INT32_FORMAT "l"
#else
# error "type of int32 is unknown!"
#endif

static PyObject *ErrorObject;

static FLAC__uint64
Long_AsUint64(PyObject *n)
{
    unsigned long long v = PyLong_AsUnsignedLongLong(n);
    if (v > (FLAC__uint64) -1 && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_OverflowError,
                        "Python int too large to convert to uint64");
        return (FLAC__uint64) -1;
    }
    return v;
}

/****************************************************************/
/* Decoder objects */

typedef struct {
    PyObject_HEAD
    PyObject            *fileobj;
    FLAC__StreamDecoder *decoder;
    char                 seekable;
    char                 eof;

    PyObject            *out_byteobjs[FLAC__MAX_CHANNELS];
    Py_ssize_t           out_count;
    Py_ssize_t           out_remaining;

    FLAC__int32         *buf_samples[FLAC__MAX_CHANNELS];
    Py_ssize_t           buf_start;
    Py_ssize_t           buf_count;
    Py_ssize_t           buf_size;

    struct {
        unsigned int channels;
        unsigned int bits_per_sample;
        unsigned long sample_rate;
    } out_attr, buf_attr;
} DecoderObject;

static PyObject *Decoder_Type;

static FLAC__StreamDecoderReadStatus
decoder_read(const FLAC__StreamDecoder *decoder,
             FLAC__byte                 buffer[],
             size_t                    *bytes,
             void                      *client_data)
{
    DecoderObject *self = client_data;
    size_t max = *bytes, n;
    PyObject *memview, *count;

    memview = PyMemoryView_FromMemory((void *) buffer, max, PyBUF_WRITE);
    count = PyObject_CallMethod(self->fileobj, "readinto", "(O)", memview);
    *bytes = n = count ? PyLong_AsSize_t(count) : (size_t) -1;
    Py_XDECREF(memview);
    Py_XDECREF(count);

    if (PyErr_Occurred()) {
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    } else if (n == 0) {
        *bytes = 0;
        self->eof = 1;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    } else if (n > max) {
        PyErr_SetString(PyExc_ValueError, "invalid result from readinto");
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    } else {
        *bytes = n;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }
}

static FLAC__StreamDecoderSeekStatus
decoder_seek(const FLAC__StreamDecoder *decoder,
             FLAC__uint64               absolute_byte_offset,
             void                      *client_data)
{
    DecoderObject *self = client_data;
    PyObject *dummy;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;

    self->eof = 0;
    dummy = PyObject_CallMethod(self->fileobj, "seek", "(K)",
                                (unsigned long long) absolute_byte_offset);
    Py_XDECREF(dummy);

    if (PyErr_Occurred())
        return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    else
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus
decoder_tell(const FLAC__StreamDecoder *decoder,
             FLAC__uint64              *absolute_byte_offset,
             void                      *client_data)
{
    DecoderObject *self = client_data;
    PyObject *result;
    FLAC__uint64 pos;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;

    result = PyObject_CallMethod(self->fileobj, "tell", "()");
    pos = result ? Long_AsUint64(result) : (FLAC__uint64) -1;
    Py_XDECREF(result);

    if (PyErr_Occurred()) {
        return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
    } else {
        *absolute_byte_offset = pos;
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }
}

static FLAC__StreamDecoderLengthStatus
decoder_length(const FLAC__StreamDecoder *decoder,
               FLAC__uint64              *stream_length,
               void                      *client_data)
{
    DecoderObject *self = client_data;
    PyObject *oldpos = NULL, *newpos = NULL, *dummy = NULL;
    FLAC__uint64 pos;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;

    oldpos = PyObject_CallMethod(self->fileobj, "tell", "()");
    if (oldpos != NULL)
        newpos = PyObject_CallMethod(self->fileobj, "seek", "(ii)", 0, 2);
    if (newpos != NULL)
        dummy = PyObject_CallMethod(self->fileobj, "seek", "(O)", oldpos);
    pos = newpos ? Long_AsUint64(newpos) : (FLAC__uint64) -1;
    Py_XDECREF(oldpos);
    Py_XDECREF(newpos);
    Py_XDECREF(dummy);

    if (PyErr_Occurred()) {
        return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
    } else {
        *stream_length = pos;
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }
}

static FLAC__bool
decoder_eof(const FLAC__StreamDecoder *decoder,
            void                      *client_data)
{
    DecoderObject *self = client_data;
    return self->eof;
}

static int
write_out_samples(DecoderObject  *self,
                  FLAC__int32   **buffer,
                  unsigned int    channels,
                  Py_ssize_t      offset,
                  Py_ssize_t      count)
{
    Py_ssize_t size;
    unsigned int i;
    FLAC__int32 *p;

    if (self->out_count == 0) {
        size = self->out_remaining * sizeof(FLAC__int32);
        for (i = 0; i < channels; i++) {
            Py_CLEAR(self->out_byteobjs[i]);
            self->out_byteobjs[i] = PyByteArray_FromStringAndSize(NULL, size);
            if (self->out_byteobjs[i] == NULL)
                return -1;
        }
    }

    for (i = 0; i < channels; i++) {
        p = (FLAC__int32 *) PyByteArray_AsString(self->out_byteobjs[i]);
        if (p == NULL)
            return -1;
        memcpy(&p[self->out_count],
               &buffer[i][offset],
               count * sizeof(FLAC__int32));
    }

    self->out_count += count;
    self->out_remaining -= count;
    return 0;
}

static FLAC__StreamDecoderWriteStatus
decoder_write(const FLAC__StreamDecoder *decoder,
              const FLAC__Frame         *frame,
              const FLAC__int32 * const  buffer[],
              void                      *client_data)
{
    DecoderObject *self = client_data;
    Py_ssize_t blocksize, out_count, buf_count;
    unsigned int channels, i;

    blocksize = frame->header.blocksize;
    out_count = self->out_remaining;
    if (out_count > blocksize)
        out_count = blocksize;
    if (out_count > 0 && self->out_count > 0 &&
        (self->out_attr.channels != frame->header.channels ||
         self->out_attr.bits_per_sample != frame->header.bits_per_sample ||
         self->out_attr.sample_rate != frame->header.sample_rate))
        out_count = 0;
    buf_count = blocksize - out_count;

    channels = frame->header.channels;

    if (out_count > 0) {
        if (write_out_samples(self, (FLAC__int32 **) buffer,
                              channels, 0, out_count) < 0)
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        self->out_attr.channels = frame->header.channels;
        self->out_attr.bits_per_sample = frame->header.bits_per_sample;
        self->out_attr.sample_rate = frame->header.sample_rate;
    }

    if (buf_count > 0) {
        if (self->buf_count > 0) {
            PyErr_SetString(PyExc_RuntimeError,
                            "decoder_write called multiple times");
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        if (buf_count > self->buf_size || !self->buf_samples[channels - 1]) {
            for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
                PyMem_Free(self->buf_samples[i]);
                self->buf_samples[i] = NULL;
            }
            self->buf_size = blocksize;
            for (i = 0; i < channels; i++) {
                self->buf_samples[i] = PyMem_New(FLAC__int32, self->buf_size);
                if (!self->buf_samples[i]) {
                    PyErr_NoMemory();
                    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                }
            }
        }

        for (i = 0; i < channels; i++)
            memcpy(self->buf_samples[i], &buffer[i][out_count],
                   buf_count * sizeof(FLAC__int32));

        self->buf_attr.channels = frame->header.channels;
        self->buf_attr.bits_per_sample = frame->header.bits_per_sample;
        self->buf_attr.sample_rate = frame->header.sample_rate;
        self->buf_start = 0;
        self->buf_count = buf_count;
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
decoder_metadata(const FLAC__StreamDecoder  *decoder,
                 const FLAC__StreamMetadata *metadata,
                 void                       *client_data)
{
}

static void
decoder_error(const FLAC__StreamDecoder      *decoder,
              FLAC__StreamDecoderErrorStatus  status,
              void                           *client_data)
{
}

static DecoderObject *
newDecoderObject(PyObject *fileobj)
{
    DecoderObject *self;
    FLAC__StreamDecoderInitStatus status;
    PyObject *seekable;
    unsigned int i;

    self = PyObject_GC_New(DecoderObject, (PyTypeObject *) Decoder_Type);
    if (self == NULL)
        return NULL;

    self->decoder = FLAC__stream_decoder_new();
    self->eof = 0;
    self->fileobj = fileobj;
    Py_XINCREF(self->fileobj);

    PyObject_GC_Track((PyObject *) self);

    for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
        self->out_byteobjs[i] = NULL;
        self->buf_samples[i] = NULL;
    }
    self->out_count = 0;
    self->out_remaining = 0;
    self->buf_start = 0;
    self->buf_count = 0;
    self->buf_size = 0;
    memset(&self->out_attr, 0, sizeof(self->out_attr));
    memset(&self->buf_attr, 0, sizeof(self->buf_attr));

    seekable = PyObject_CallMethod(self->fileobj, "seekable", "()");
    self->seekable = seekable ? PyObject_IsTrue(seekable) : 0;
    Py_XDECREF(seekable);
    if (PyErr_Occurred()) {
        Py_XDECREF(self);
        return NULL;
    }

    if (self->decoder == NULL) {
        PyErr_NoMemory();
        Py_XDECREF(self);
        return NULL;
    }

    status = FLAC__stream_decoder_init_stream(self->decoder,
                                              &decoder_read,
                                              &decoder_seek,
                                              &decoder_tell,
                                              &decoder_length,
                                              &decoder_eof,
                                              &decoder_write,
                                              &decoder_metadata,
                                              &decoder_error,
                                              self);

    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        PyErr_Format(ErrorObject, "init_stream failed (state = %s)",
                     FLAC__StreamDecoderInitStatusString[status]);
        Py_XDECREF(self);
        return NULL;
    }

    return self;
}

static int
Decoder_traverse(DecoderObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->fileobj);
    return 0;
}

static void
Decoder_clear(DecoderObject *self)
{
    Py_CLEAR(self->fileobj);
}

static void
Decoder_dealloc(DecoderObject *self)
{
    unsigned int i;

    PyObject_GC_UnTrack((PyObject *) self);

    for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
        Py_CLEAR(self->out_byteobjs[i]);
        PyMem_Free(self->buf_samples[i]);
        self->buf_samples[i] = NULL;
    }

    Py_CLEAR(self->fileobj);

    if (self->decoder)
        FLAC__stream_decoder_delete(self->decoder);

    PyObject_Free(self);
}

static PyObject *
Decoder_read(DecoderObject *self, PyObject *args)
{
    Py_ssize_t limit;
    FLAC__bool ok;
    FLAC__StreamDecoderState state;
    PyObject *memview, *arrays[FLAC__MAX_CHANNELS] = {0}, *result = NULL;
    Py_ssize_t out_count, new_size;
    unsigned int i;

    if (!PyArg_ParseTuple(args, "n:read", &limit))
        return NULL;

    self->out_remaining = limit;

    out_count = self->out_remaining;
    if (out_count > self->buf_count)
        out_count = self->buf_count;
    if (out_count > 0) {
        if (write_out_samples(self, self->buf_samples,
                              self->buf_attr.channels,
                              self->buf_start, out_count) < 0)
            goto fail;

        self->out_attr = self->buf_attr;
        self->buf_start += out_count;
        self->buf_count -= out_count;
    }

    while (self->out_remaining > 0 && self->buf_count == 0) {
        ok = FLAC__stream_decoder_process_single(self->decoder);

        state = FLAC__stream_decoder_get_state(self->decoder);
        if (state == FLAC__STREAM_DECODER_ABORTED)
            FLAC__stream_decoder_flush(self->decoder);

        if (PyErr_Occurred())
            goto fail;

        if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
            break;

        if (!ok) {
            PyErr_Format(ErrorObject, "process_single failed (state = %s)",
                         FLAC__StreamDecoderStateString[state]);
            goto fail;
        }
    }

    if (self->out_count == 0) {
        Py_INCREF(Py_None);
        result = Py_None;
    } else {
        if (self->out_remaining > 0) {
            new_size = self->out_count * sizeof(FLAC__int32);
            for (i = 0; i < self->out_attr.channels; i++)
                if (PyByteArray_Resize(self->out_byteobjs[i], new_size) < 0)
                    goto fail;
        }

        for (i = 0; i < self->out_attr.channels; i++) {
            memview = PyMemoryView_FromObject(self->out_byteobjs[i]);
            arrays[i] = PyObject_CallMethod(memview, "cast", "(s)",
                                            INT32_FORMAT);
            Py_XDECREF(memview);
            if (!arrays[i])
                goto fail;
        }

        result = PyTuple_New(self->out_attr.channels);
        for (i = 0; i < self->out_attr.channels; i++) {
            PyTuple_SetItem(result, i, arrays[i]);
            arrays[i] = NULL;   /* PyTuple_SetItem steals reference */
        }
    }

 fail:
    for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
        Py_CLEAR(arrays[i]);
        Py_CLEAR(self->out_byteobjs[i]);
    }

    self->out_count = 0;
    self->out_remaining = 0;

    return result;
}

static PyObject *
Decoder_read_metadata(DecoderObject *self, PyObject *args)
{
    FLAC__bool ok;
    FLAC__StreamDecoderState state;

    if (!PyArg_ParseTuple(args, ":read_metadata"))
        return NULL;

    ok = FLAC__stream_decoder_process_until_end_of_metadata(self->decoder);

    state = FLAC__stream_decoder_get_state(self->decoder);
    if (state == FLAC__STREAM_DECODER_ABORTED)
        FLAC__stream_decoder_flush(self->decoder);

    if (PyErr_Occurred())
        return NULL;

    if (!ok) {
        PyErr_Format(ErrorObject, "read_metadata failed (state = %s)",
                     FLAC__StreamDecoderStateString[state]);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
Decoder_seek_absolute(DecoderObject *self, PyObject *args)
{
    PyObject *arg = NULL;
    FLAC__uint64 sample_number;
    FLAC__bool ok;
    FLAC__StreamDecoderState state;

    if (!PyArg_ParseTuple(args, "O:seek_absolute", &arg))
        return NULL;
    sample_number = Long_AsUint64(arg);
    if (PyErr_Occurred())
        return NULL;

    self->buf_count = 0;

    ok = FLAC__stream_decoder_seek_absolute(self->decoder, sample_number);

    state = FLAC__stream_decoder_get_state(self->decoder);
    if ((state == FLAC__STREAM_DECODER_ABORTED ||
         state == FLAC__STREAM_DECODER_SEEK_ERROR))
        FLAC__stream_decoder_flush(self->decoder);

    if (PyErr_Occurred())
        return NULL;

    if (!ok) {
        PyErr_Format(ErrorObject, "seek_absolute failed (state = %s)",
                     FLAC__StreamDecoderStateString[state]);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef Decoder_methods[] = {
    {"read", (PyCFunction)Decoder_read, METH_VARARGS,
     PyDoc_STR("read(n_samples) -> tuple of arrays, or None")},
    {"read_metadata", (PyCFunction)Decoder_read_metadata, METH_VARARGS,
     PyDoc_STR("read_metadata() -> None")},
    {"seek_absolute", (PyCFunction)Decoder_seek_absolute, METH_VARARGS,
     PyDoc_STR("seek_absolute(sample_number) -> None")},
    {NULL, NULL}
};

static PyMemberDef Decoder_members[] = {
    {"channels", T_UINT,
     offsetof(DecoderObject, out_attr.channels),
     READONLY},
    {"bits_per_sample", T_UINT,
     offsetof(DecoderObject, out_attr.bits_per_sample),
     READONLY},
    {"sample_rate", T_ULONG,
     offsetof(DecoderObject, out_attr.sample_rate),
     READONLY},
    {NULL}
};

static PyType_Slot Decoder_Type_slots[] = {
    {Py_tp_dealloc,  Decoder_dealloc},
    {Py_tp_traverse, Decoder_traverse},
    {Py_tp_clear,    Decoder_clear},
    {Py_tp_methods,  Decoder_methods},
    {Py_tp_members,  Decoder_members},
    {0, 0}
};

static PyType_Spec Decoder_Type_spec = {
    "plibflac._io.Decoder",
    sizeof(DecoderObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    Decoder_Type_slots
};

static PyObject *
plibflac_decoder(PyObject *self, PyObject *args)
{
    PyObject *fileobj = NULL;

    if (!PyArg_ParseTuple(args, "O:decoder", &fileobj))
        return NULL;

    return (PyObject *) newDecoderObject(fileobj);
}

/****************************************************************/
/* Encoder objects */

typedef struct {
    PyObject_HEAD
    PyObject            *fileobj;
    FLAC__StreamEncoder *encoder;
    char                 seekable;
} EncoderObject;

static PyObject *Encoder_Type;

static FLAC__StreamEncoderWriteStatus
encoder_write(const FLAC__StreamEncoder *encoder,
              const FLAC__byte           buffer[],
              size_t                     bytes,
              uint32_t                   samples,
              uint32_t                   current_frame,
              void                      *client_data)
{
    EncoderObject *self = client_data;
    PyObject *memview, *count;
    size_t n;

    while (bytes > 0) {
        memview = PyMemoryView_FromMemory((void *) buffer, bytes, PyBUF_READ);
        count = PyObject_CallMethod(self->fileobj, "write", "(O)", memview);
        n = count ? PyLong_AsSize_t(count) : (size_t) -1;
        Py_XDECREF(memview);
        Py_XDECREF(count);

        if (PyErr_Occurred()) {
            return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
        } else if (n > bytes) {
            PyErr_SetString(PyExc_ValueError, "invalid result from write");
            return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
        } else {
            bytes -= n;
        }
    }
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderSeekStatus
encoder_seek(const FLAC__StreamEncoder *encoder,
             FLAC__uint64               absolute_byte_offset,
             void                      *client_data)
{
    EncoderObject *self = client_data;
    PyObject *dummy;

    if (!self->seekable)
        return FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;

    dummy = PyObject_CallMethod(self->fileobj, "seek", "(K)",
                                (unsigned long long) absolute_byte_offset);
    Py_XDECREF(dummy);

    if (PyErr_Occurred())
        return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    else
        return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus
encoder_tell(const FLAC__StreamEncoder *encoder,
             FLAC__uint64              *absolute_byte_offset,
             void                      *client_data)
{
    EncoderObject *self = client_data;
    PyObject *result;
    FLAC__uint64 pos;

    if (!self->seekable)
        return FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;

    result = PyObject_CallMethod(self->fileobj, "tell", "()");
    pos = result ? Long_AsUint64(result) : (FLAC__uint64) -1;
    Py_XDECREF(result);

    if (PyErr_Occurred()) {
        return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
    } else {
        *absolute_byte_offset = pos;
        return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
    }
}

static EncoderObject *
newEncoderObject(PyObject *fileobj)
{
    EncoderObject *self;
    FLAC__StreamEncoderInitStatus status;
    PyObject *seekable;

    self = PyObject_GC_New(EncoderObject, (PyTypeObject *) Encoder_Type);
    if (self == NULL)
        return NULL;

    self->encoder = FLAC__stream_encoder_new();
    self->fileobj = fileobj;
    Py_XINCREF(self->fileobj);

    PyObject_GC_Track((PyObject *) self);

    seekable = PyObject_CallMethod(self->fileobj, "seekable", "()");
    self->seekable = seekable ? PyObject_IsTrue(seekable) : 0;
    Py_XDECREF(seekable);
    if (PyErr_Occurred()) {
        Py_XDECREF(self);
        return NULL;
    }

    if (self->encoder == NULL) {
        PyErr_NoMemory();
        Py_XDECREF(self);
        return NULL;
    }

    status = FLAC__stream_encoder_init_stream(self->encoder,
                                              &encoder_write,
                                              &encoder_seek,
                                              &encoder_tell,
                                              NULL, self);

    if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        PyErr_Format(ErrorObject, "init_stream failed (state = %s)",
                     FLAC__StreamEncoderInitStatusString[status]);
        Py_XDECREF(self);
        return NULL;
    }

    return self;
}

static int
Encoder_traverse(EncoderObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->fileobj);
    return 0;
}

static void
Encoder_clear(EncoderObject *self)
{
    Py_CLEAR(self->fileobj);
}

static void
Encoder_dealloc(EncoderObject *self)
{
    PyObject_GC_UnTrack((PyObject *) self);

    Py_CLEAR(self->fileobj);

    if (self->encoder)
        FLAC__stream_encoder_delete(self->encoder);

    PyObject_Free(self);
}

static PyType_Slot Encoder_Type_slots[] = {
    {Py_tp_dealloc,  Encoder_dealloc},
    {Py_tp_traverse, Encoder_traverse},
    {Py_tp_clear,    Encoder_clear},
    {0, 0}
};

static PyType_Spec Encoder_Type_spec = {
    "plibflac._io.Encoder",
    sizeof(EncoderObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    Encoder_Type_slots
};

static PyObject *
plibflac_encoder(PyObject *self, PyObject *args)
{
    PyObject *fileobj = NULL;

    if (!PyArg_ParseTuple(args, "O:encoder", &fileobj))
        return NULL;

    return (PyObject *) newEncoderObject(fileobj);
}

/****************************************************************/

static PyMethodDef plibflac_methods[] = {
    {"decoder", plibflac_decoder, METH_VARARGS,
     PyDoc_STR("decoder(fileobj) -> new Decoder object")},
    {"encoder", plibflac_encoder, METH_VARARGS,
     PyDoc_STR("encoder(fileobj) -> new Encoder object")},
    {NULL, NULL}
};

PyDoc_STRVAR(module_doc,
"Low-level functions for reading and writing FLAC streams.");

static int
plibflac_exec(PyObject *m)
{
    if (Decoder_Type == NULL) {
        Decoder_Type = PyType_FromSpec(&Decoder_Type_spec);
        if (Decoder_Type == NULL)
            return -1;
    }

    if (Encoder_Type == NULL) {
        Encoder_Type = PyType_FromSpec(&Encoder_Type_spec);
        if (Encoder_Type == NULL)
            return -1;
    }

    if (ErrorObject == NULL) {
        ErrorObject = PyErr_NewException("plibflac.error", NULL, NULL);
        if (ErrorObject == NULL)
            return -1;
    }
    Py_INCREF(ErrorObject);
    if (PyModule_AddObject(m, "error", ErrorObject) < 0) {
        Py_DECREF(ErrorObject);
        return -1;
    }

    return 0;
}

static struct PyModuleDef_Slot plibflac_slots[] = {
    {Py_mod_exec, plibflac_exec},
    {0, NULL},
};

static struct PyModuleDef plibflacmodule = {
    PyModuleDef_HEAD_INIT,
    "_io",
    module_doc,
    0,
    plibflac_methods,
    plibflac_slots,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit__plibflac(void)
{
    return PyModuleDef_Init(&plibflacmodule);
}
