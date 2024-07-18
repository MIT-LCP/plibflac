#include <Python.h>
#include <FLAC/stream_decoder.h>

/* Common exception type */

static PyObject *ErrorObject;

/****************************************************************/
/* Decoder objects */

typedef struct {
    PyObject_HEAD
    PyObject            *fileobj;
    FLAC__StreamDecoder *decoder;
    char                 eof;
} DecoderObject;

static PyTypeObject Decoder_Type;

static FLAC__StreamDecoderReadStatus
decoder_read(const FLAC__StreamDecoder *decoder,
             FLAC__byte                 buffer[],
             size_t                    *bytes,
             void                      *client_data)
{
    DecoderObject *self = client_data;
    size_t remaining = *bytes, n;
    PyObject *memview, *count;

    *bytes = 0;
    while (remaining > 0) {
        memview = PyMemoryView_FromMemory(buffer, remaining, PyBUF_WRITE);
        count = PyObject_CallMethod(self->fileobj, "readinto", "(O)", memview);
        n = count ? PyLong_AsSize_t(count) : (size_t) -1;
        Py_XDECREF(memview);
        Py_XDECREF(count);

        if (PyErr_Occurred()) {
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        } else if (n == 0) {
            self->eof = 1;
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        } else if (n <= remaining) {
            buffer += n;
            *bytes += n;
            remaining -= n;
        } else {
            PyErr_SetString(PyExc_ValueError, "invalid result from readinto");
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
    }
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderSeekStatus
decoder_seek(const FLAC__StreamDecoder *decoder,
             FLAC__uint64               absolute_byte_offset,
             void                      *client_data)
{
    return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
}

static FLAC__StreamDecoderTellStatus
decoder_tell(const FLAC__StreamDecoder *decoder,
             FLAC__uint64              *absolute_byte_offset,
             void                      *client_data)
{
    return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
}

static FLAC__StreamDecoderLengthStatus
decoder_length(const FLAC__StreamDecoder *decoder,
               FLAC__uint64              *stream_length,
               void                      *client_data)
{
    return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
}

static FLAC__bool
decoder_eof(const FLAC__StreamDecoder *decoder,
            void                      *client_data)
{
    DecoderObject *self = client_data;
    return self->eof;
}

static FLAC__StreamDecoderWriteStatus
decoder_write(const FLAC__StreamDecoder *decoder,
              const FLAC__Frame         *frame,
              const FLAC__int32 * const  buffer[],
              void                      *client_data)
{
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

    self = PyObject_New(DecoderObject, &Decoder_Type);
    if (self == NULL)
        return NULL;

    self->decoder = FLAC__stream_decoder_new();
    self->eof = 0;
    self->fileobj = fileobj;
    Py_XINCREF(self->fileobj);

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

static void
Decoder_dealloc(DecoderObject *self)
{
    Py_XDECREF(self->fileobj);
    if (self->decoder)
        FLAC__stream_decoder_delete(self->decoder);
    PyObject_Free(self);
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
    if (state == FLAC__STREAM_DECODER_ABORTED) {
        FLAC__stream_decoder_flush(self->decoder);
        return NULL;
    }

    if (!ok) {
        PyErr_Format(ErrorObject, "read_metadata failed (state = %s)",
                     FLAC__StreamDecoderStateString[state]);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef Decoder_methods[] = {
    {"read_metadata", (PyCFunction)Decoder_read_metadata, METH_VARARGS,
     PyDoc_STR("read_metadata() -> None")},
    {NULL, NULL}
};

static PyTypeObject Decoder_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "plibflac._io.Decoder",     /*tp_name*/
    sizeof(DecoderObject),      /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)Decoder_dealloc,/*tp_dealloc*/
    0,                          /*tp_vectorcall_offset*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_as_async*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    0,                          /*tp_richcompare*/
    0,                          /*tp_weaklistoffset*/
    0,                          /*tp_iter*/
    0,                          /*tp_iternext*/
    Decoder_methods,            /*tp_methods*/
    0,                          /*tp_members*/
    0,                          /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    0,                          /*tp_dictoffset*/
    0,                          /*tp_init*/
    0,                          /*tp_alloc*/
    0,                          /*tp_new*/
    0,                          /*tp_free*/
    0,                          /*tp_is_gc*/
};

static PyObject *
io_open_decoder(PyObject *self, PyObject *args)
{
    PyObject *fileobj = NULL;

    if (!PyArg_ParseTuple(args, "O:open_decoder", &fileobj))
        return NULL;

    return (PyObject *) newDecoderObject(fileobj);
}

/****************************************************************/

static PyMethodDef io_methods[] = {
    {"open_decoder", io_open_decoder, METH_VARARGS,
     PyDoc_STR("open_decoder(fileobj) -> new Decoder object")},
    {NULL, NULL}
};

PyDoc_STRVAR(module_doc,
"Low-level functions for reading and writing FLAC streams.");

static int
io_exec(PyObject *m)
{
    if (PyType_Ready(&Decoder_Type) < 0)
        return -1;

    if (ErrorObject == NULL) {
        ErrorObject = PyErr_NewException("plibflac.error", NULL, NULL);
        if (ErrorObject == NULL) {
            return -1;
        }
    }
    int rc = PyModule_AddType(m, (PyTypeObject *)ErrorObject);
    Py_DECREF(ErrorObject);
    if (rc < 0) {
        return -1;
    }

    return 0;
}

static struct PyModuleDef_Slot io_slots[] = {
    {Py_mod_exec, io_exec},
    {0, NULL},
};

static struct PyModuleDef iomodule = {
    PyModuleDef_HEAD_INIT,
    "_io",
    module_doc,
    0,
    io_methods,
    io_slots,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit__io(void)
{
    return PyModuleDef_Init(&iomodule);
}
