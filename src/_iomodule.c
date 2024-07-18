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
} DecoderObject;

static PyTypeObject Decoder_Type;

static DecoderObject *
newDecoderObject(PyObject *fileobj)
{
    DecoderObject *self;
    self = PyObject_New(DecoderObject, &Decoder_Type);
    if (self == NULL)
        return NULL;

    self->decoder = FLAC__stream_decoder_new();
    if (self->decoder == NULL) {
        PyErr_NoMemory();
        PyObject_Free(self);
        return NULL;
    }

    self->fileobj = fileobj;
    Py_XINCREF(self->fileobj);

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
Decoder_read(DecoderObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":read"))
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef Decoder_methods[] = {
    {"read", (PyCFunction)Decoder_read, METH_VARARGS,
        PyDoc_STR("read() -> None")},
    {NULL, NULL}
};

static PyTypeObject Decoder_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "plibflac.Decoder",         /*tp_name*/
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
