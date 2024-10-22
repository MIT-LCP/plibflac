#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#include <Python.h>
#include <structmember.h>

#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>

#ifdef _WIN32
# include <io.h>
# undef lseek
# undef off_t
# define lseek _lseeki64
# define off_t __int64
#endif

/* PyBUF_READ and PyBUF_WRITE were not formally added to the limited
   API until 3.11, but PyMemoryView_FromMemory is stable since 3.3
   (https://github.com/python/cpython/issues/98680) */
#ifndef PyBUF_READ
# define PyBUF_READ 0x100
#endif
#ifndef PyBUF_WRITE
# define PyBUF_WRITE 0x200
#endif

/* Added in 3.12 */
#ifndef Py_mod_multiple_interpreters
# define Py_mod_multiple_interpreters 3
#endif
#ifndef Py_MOD_PER_INTERPRETER_GIL_SUPPORTED
# define Py_MOD_PER_INTERPRETER_GIL_SUPPORTED ((void *)2)
#endif

/* Macros added in 3.13 */
#ifndef Py_BEGIN_CRITICAL_SECTION
# define Py_BEGIN_CRITICAL_SECTION(obj) {
# define Py_END_CRITICAL_SECTION() }
#endif

/****************************************************************/

#define OFF_MAX ((((off_t) 1 << (sizeof(off_t) * CHAR_BIT - 2)) - 1) * 2 + 1)

#if INT_MAX == 0x7fffffff
# define INT32_FORMAT "i"
#elif LONG_MAX == 0x7fffffff
# define INT32_FORMAT "l"
#else
# error "type of int32 is unknown!"
#endif

static FLAC__bool
Long_AsBool(PyObject *n)
{
    unsigned long v = PyLong_AsUnsignedLong(n);
    if (v > 1 && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_OverflowError,
                        "Python int too large to convert to bool");
        return 1;
    }
    return v;
}

static uint32_t
Long_AsUint32(PyObject *n)
{
    unsigned long v = PyLong_AsUnsignedLong(n);
    if (v > (uint32_t) -1 && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_OverflowError,
                        "Python int too large to convert to uint32");
        return (uint32_t) -1;
    }
    return v;
}

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

static uintmax_t
check_return_uint(PyObject *value, const char *method_name,
                  const char *caller, uintmax_t max_value)
{
    uintmax_t n;

    if (!value)
        return 0;

    if (!PyLong_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "%s() returned %R, not an integer (in %s)",
                     method_name, value, caller);
        return 0;
    }

    if ((unsigned long long) -1 > (size_t) -1)
        n = PyLong_AsUnsignedLongLong(value);
    else
        n = PyLong_AsSize_t(value);

    if (PyErr_Occurred() || n > max_value) {
        PyErr_Format(PyExc_ValueError,
                     "%s() returned %R, which is out of range (in %s)",
                     method_name, value, caller);
        return 0;
    }
    return n;
}

static PyObject *
MemoryView_FromMem(void *ptr, size_t size)
{
#if !defined(Py_LIMITED_API) && defined(PYPY_VERSION)
    /* Workaround for buggy PyMemoryView_FromMemory in PyPy (<= 7.3.16) */
    Py_buffer b;
    if (PyBuffer_FillInfo(&b, NULL, ptr, size, 0, PyBUF_WRITABLE) < 0)
        return NULL;
    else
        return PyMemoryView_FromBuffer(&b);
#else
    return PyMemoryView_FromMemory(ptr, size, PyBUF_WRITE);
#endif
}

static unsigned long
get_python_version(void)
{
    PyObject *version_info, *major = NULL, *minor = NULL;
    long major_n, minor_n;

    version_info = PySys_GetObject("version_info");
    if (version_info != NULL) {
        major = PySequence_GetItem(version_info, 0);
        minor = PySequence_GetItem(version_info, 1);
    }
    major_n = major ? PyLong_AsLong(major) : -1;
    minor_n = minor ? PyLong_AsLong(minor) : -1;
    Py_CLEAR(major);
    Py_CLEAR(minor);
    if (major_n < 0 || minor_n < 0)
        return 0;
    return ((major_n << 24) + (minor_n << 16));
}

/****************************************************************/

typedef struct {
    PyObject *Decoder_Type;
    PyObject *Encoder_Type;
    PyObject *Error_Type;
} plibflac_module_state;

static PyObject *
get_error_type(PyObject *module)
{
    plibflac_module_state *st = PyModule_GetState(module);
    if (!st)
        return NULL;
    return st->Error_Type;
}

/****************************************************************/

/* Release GIL before calling libFLAC functions */
#define BEGIN_PROCESSING()                              \
    do {                                                \
        assert(self->thread_state == NULL);             \
        self->thread_state = PyEval_SaveThread();       \
    } while (0)
/* Acquire GIL after calling libFLAC functions */
#define END_PROCESSING()                                \
    do {                                                \
        PyEval_RestoreThread(self->thread_state);       \
        self->thread_state = NULL;                      \
    } while (0)

/* Acquire GIL before calling Python functions within callback */
#define BEGIN_CALLBACK()                                \
    do {                                                \
        PyEval_RestoreThread(self->thread_state);       \
        self->thread_state = NULL;                      \
    } while (0)
/* Release GIL after calling Python functions within callback */
#define END_CALLBACK()                                  \
    do {                                                \
        assert(self->thread_state == NULL);             \
        self->thread_state = PyEval_SaveThread();       \
    } while (0)

/* Simple check to prevent calling decoder/encoder methods from
   multiple threads, or calling methods recursively within I/O
   callbacks. */

#define BEGIN_NO_RECURSION(method_name)                                 \
    do {                                                                \
        Py_BEGIN_CRITICAL_SECTION(self);                                \
        if (recursion_check(&self->busy_method, method_name) == 0) {    \
            assert(self->thread_state == NULL);                         \

#define END_NO_RECURSION                                                \
            assert(self->thread_state == NULL);                         \
            self->busy_method = NULL;                                   \
        }                                                               \
        Py_END_CRITICAL_SECTION();                                      \
    } while (0)

static int
recursion_check(const char **busy_method, const char *this_method)
{
    if (*busy_method) {
        if (this_method[0] == '.')
            PyErr_Format(PyExc_TypeError,
                         "cannot set '%s' within %s()",
                         &this_method[1], *busy_method);
        else
            PyErr_Format(PyExc_TypeError,
                         "%s() called recursively within %s()",
                         this_method, *busy_method);
        return -1;
    } else {
        *busy_method = this_method;
        return 0;
    }
}

/****************************************************************/

#define PROPERTY_DEF_RO(Obj, prop)                            \
    {#prop, (getter)Obj##_##prop##_getter, NULL, NULL, NULL}

#define PROPERTY_DEF_RW(Obj, prop)                            \
    {#prop, (getter)Obj##_##prop##_getter,                    \
     (setter)Obj##_##prop##_setter, NULL, NULL}

#define PROPERTY_FUNCS(Obj, obj, prop, type, to_pyobj, from_pyobj)      \
    static PyObject *                                                   \
    Obj##_##prop##_getter(Obj##Object *self, void *closure)             \
    {                                                                   \
        type value;                                                     \
        Py_BEGIN_CRITICAL_SECTION(self);                                \
        value = FLAC__stream_##obj##_get_##prop(self->obj);             \
        Py_END_CRITICAL_SECTION();                                      \
        return to_pyobj(value);                                         \
    }                                                                   \
    static int                                                          \
    Obj##_##prop##_setter(Obj##Object *self, PyObject *value,           \
                          void *closure)                                \
    {                                                                   \
        type n;                                                         \
        FLAC__bool ok = 0;                                              \
        if (!value) {                                                   \
            PyErr_Format(PyExc_AttributeError,                          \
                         "cannot delete attribute '%s'", #prop);        \
            return -1;                                                  \
        }                                                               \
        if (!PyLong_Check(value)) {                                     \
            PyErr_Format(PyExc_TypeError,                               \
                         "invalid type for attribute '%s'", #prop);     \
            return -1;                                                  \
        }                                                               \
        n = from_pyobj(value);                                          \
        if (PyErr_Occurred())                                           \
            return -1;                                                  \
        BEGIN_NO_RECURSION("." #prop);                                  \
        ok = FLAC__stream_##obj##_set_##prop(self->obj, n);             \
        END_NO_RECURSION;                                               \
        if (!ok) {                                                      \
            PyErr_Format(PyExc_ValueError,                              \
                         "cannot set '%s' after open()", #prop);        \
            return -1;                                                  \
        }                                                               \
        return 0;                                                       \
    }
#define PROPERTY_BOOL(Obj, obj, prop)                                   \
    PROPERTY_FUNCS(Obj, obj, prop, FLAC__bool,                          \
                   PyBool_FromLong, Long_AsBool)
#define PROPERTY_UINT32(Obj, obj, prop)                                 \
    PROPERTY_FUNCS(Obj, obj, prop, uint32_t,                            \
                   PyLong_FromUnsignedLong, Long_AsUint32)
#define PROPERTY_UINT64(Obj, obj, prop)                                 \
    PROPERTY_FUNCS(Obj, obj, prop, FLAC__uint64,                        \
                   PyLong_FromUnsignedLongLong, Long_AsUint64)

/****************************************************************/
/* Decoder objects */

typedef struct {
    PyObject_HEAD

    PyThreadState       *thread_state;
    const char          *busy_method;
    PyObject            *module;

    PyObject            *fileobj;
    FLAC__StreamDecoder *decoder;
    int                  fd;
    char                 seekable;
    char                 eof;

    PyObject            *out_byteobjs[FLAC__MAX_CHANNELS];
    FLAC__int32         *out_samples[FLAC__MAX_CHANNELS];
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

static FLAC__StreamDecoderReadStatus
decoder_read(const FLAC__StreamDecoder *decoder,
             FLAC__byte                 buffer[],
             size_t                    *bytes,
             void                      *client_data)
{
    DecoderObject *self = client_data;
    size_t max = *bytes, n = 0;
    PyObject *memview = NULL, *count = NULL;
    FLAC__StreamDecoderReadStatus status;

    BEGIN_CALLBACK();

    PyErr_CheckSignals();
    if (PyErr_Occurred()) {
        status = FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        goto done;
    }

    memview = MemoryView_FromMem(buffer, max);
    if (memview != NULL)
        count = PyObject_CallMethod(self->fileobj, "readinto", "(O)", memview);
    if (count != Py_None)
        n = check_return_uint(count, "readinto", "decoder_read", max);
    Py_XDECREF(memview);
    Py_XDECREF(count);

    if (PyErr_Occurred()) {
        status = FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    } else if (count == Py_None) {
        /* None means stream is non-blocking and no data available */
        status = FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    } else if (n == 0) {
        /* Zero means end of file */
        *bytes = 0;
        self->eof = 1;
        status = FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    } else {
        *bytes = n;
        status = FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

 done:
    END_CALLBACK();
    return status;
}

static FLAC__StreamDecoderReadStatus
decoder_read_fd(const FLAC__StreamDecoder *decoder,
                FLAC__byte                 buffer[],
                size_t                    *bytes,
                void                      *client_data)
{
    DecoderObject *self = client_data;
    Py_ssize_t n;
    int e;

    do {
        BEGIN_CALLBACK();
        PyErr_CheckSignals();
        e = !!PyErr_Occurred();
        END_CALLBACK();

        if (e)
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;

        /* There is, I believe, a bug here: if a signal arrives after
           the call to PyErr_CheckSignals and before the call to read,
           then it will not interrupt the read - thus, the process
           will wait (potentially forever) for the read to complete,
           before calling the Python signal handler.  This is a
           fundamental bug that also affects 'os.read' and all other
           blocking system calls in CPython. */

        n = read(self->fd, buffer, *bytes);
    } while (n < 0 && errno == EINTR);

    if (n == 0) {
        *bytes = 0;
        self->eof = 1;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    } else if (n < 0) {
        e = errno;
        if (e == EAGAIN || e == EWOULDBLOCK) {
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        } else {
            BEGIN_CALLBACK();
            errno = e;
            PyErr_SetFromErrno(PyExc_OSError);
            END_CALLBACK();
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
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
    FLAC__StreamDecoderSeekStatus status;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;

    BEGIN_CALLBACK();

    self->eof = 0;
    dummy = PyObject_CallMethod(self->fileobj, "seek", "(K)",
                                (unsigned long long) absolute_byte_offset);
    check_return_uint(dummy, "seek", "decoder_seek", (FLAC__uint64) -1);
    Py_XDECREF(dummy);

    if (PyErr_Occurred())
        status = FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    else
        status = FLAC__STREAM_DECODER_SEEK_STATUS_OK;

    END_CALLBACK();
    return status;
}

static FLAC__StreamDecoderSeekStatus
decoder_seek_fd(const FLAC__StreamDecoder *decoder,
                FLAC__uint64               absolute_byte_offset,
                void                      *client_data)
{
    DecoderObject *self = client_data;
    int e;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;

    if (absolute_byte_offset > (FLAC__uint64) OFF_MAX) {
        errno = EOVERFLOW;
    } else {
        if (lseek(self->fd, absolute_byte_offset, SEEK_SET) >= 0)
            return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }
    e = errno;
    BEGIN_CALLBACK();
    errno = e;
    PyErr_SetFromErrno(PyExc_OSError);
    END_CALLBACK();
    return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
}

static FLAC__StreamDecoderTellStatus
decoder_tell(const FLAC__StreamDecoder *decoder,
             FLAC__uint64              *absolute_byte_offset,
             void                      *client_data)
{
    DecoderObject *self = client_data;
    PyObject *result;
    FLAC__uint64 pos;
    FLAC__StreamDecoderTellStatus status;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;

    BEGIN_CALLBACK();

    result = PyObject_CallMethod(self->fileobj, "tell", "()");
    pos = check_return_uint(result, "tell", "decoder_tell",
                            (FLAC__uint64) -1);
    Py_XDECREF(result);

    if (PyErr_Occurred()) {
        status = FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
    } else {
        *absolute_byte_offset = pos;
        status = FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }

    END_CALLBACK();
    return status;
}

static FLAC__StreamDecoderTellStatus
decoder_tell_fd(const FLAC__StreamDecoder *decoder,
                FLAC__uint64              *absolute_byte_offset,
                void                      *client_data)
{
    DecoderObject *self = client_data;
    off_t pos;
    int e;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;

    pos = lseek(self->fd, (off_t) 0, SEEK_CUR);
    if (pos >= 0) {
        *absolute_byte_offset = (FLAC__uint64) pos;
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }
    e = errno;
    BEGIN_CALLBACK();
    errno = e;
    PyErr_SetFromErrno(PyExc_OSError);
    END_CALLBACK();
    return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
}

static FLAC__StreamDecoderLengthStatus
decoder_length(const FLAC__StreamDecoder *decoder,
               FLAC__uint64              *stream_length,
               void                      *client_data)
{
    DecoderObject *self = client_data;
    PyObject *oldpos = NULL, *newpos = NULL, *dummy = NULL;
    FLAC__uint64 pos;
    FLAC__StreamDecoderLengthStatus status;

    if (!self->seekable)
        return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;

    BEGIN_CALLBACK();

    oldpos = PyObject_CallMethod(self->fileobj, "tell", "()");
    check_return_uint(oldpos, "tell", "decoder_length", (FLAC__uint64) -1);

    if (oldpos != NULL)
        newpos = PyObject_CallMethod(self->fileobj, "seek", "(ii)", 0, 2);
    check_return_uint(newpos, "seek", "decoder_length", (FLAC__uint64) -1);

    if (newpos != NULL)
        dummy = PyObject_CallMethod(self->fileobj, "seek", "(O)", oldpos);
    check_return_uint(dummy, "seek", "decoder_length", (FLAC__uint64) -1);

    pos = newpos ? Long_AsUint64(newpos) : (FLAC__uint64) -1;
    Py_XDECREF(oldpos);
    Py_XDECREF(newpos);
    Py_XDECREF(dummy);

    if (PyErr_Occurred()) {
        status = FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
    } else {
        *stream_length = pos;
        status = FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }

    END_CALLBACK();
    return status;
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

    if (self->out_count == 0) {
        BEGIN_CALLBACK();

        for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
            Py_CLEAR(self->out_byteobjs[i]);
            self->out_samples[i] = NULL;
        }

        size = self->out_remaining * sizeof(FLAC__int32);
        for (i = 0; i < channels; i++) {
            self->out_byteobjs[i] = PyByteArray_FromStringAndSize(NULL, size);
            if (self->out_byteobjs[i] == NULL)
                break;
            self->out_samples[i] =
                (FLAC__int32 *) PyByteArray_AsString(self->out_byteobjs[i]);
        }

        END_CALLBACK();
    }

    for (i = 0; i < channels; i++) {
        if (self->out_samples[i] == NULL)
            return -1;
        memcpy(&self->out_samples[i][self->out_count],
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
            BEGIN_CALLBACK();
            PyErr_SetString(PyExc_RuntimeError,
                            "decoder_write called multiple times");
            END_CALLBACK();
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        if (buf_count > self->buf_size || !self->buf_samples[channels - 1]) {
            BEGIN_CALLBACK();
            for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
                PyMem_Free(self->buf_samples[i]);
                self->buf_samples[i] = NULL;
            }
            self->buf_size = blocksize;
            for (i = 0; i < channels; i++) {
                self->buf_samples[i] = PyMem_New(FLAC__int32, self->buf_size);
                if (!self->buf_samples[i]) {
                    PyErr_NoMemory();
                    break;
                }
            }
            END_CALLBACK();
        }

        for (i = 0; i < channels; i++) {
            if (!self->buf_samples[i])
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            memcpy(self->buf_samples[i], &buffer[i][out_count],
                   buf_count * sizeof(FLAC__int32));
        }

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
    DecoderObject *self = client_data;

    /* I'm not sure it's possible for metadata callback to be invoked
       after decoding begins, but be safe */
    if (self->out_count > 0 || self->buf_count > 0)
        return;

    BEGIN_CALLBACK();

    if (metadata && metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        self->out_attr.channels = metadata->data.stream_info.channels;
        self->out_attr.sample_rate = metadata->data.stream_info.sample_rate;
        self->out_attr.bits_per_sample =
            metadata->data.stream_info.bits_per_sample;
    }

    END_CALLBACK();
}

static void
decoder_error(const FLAC__StreamDecoder      *decoder,
              FLAC__StreamDecoderErrorStatus  status,
              void                           *client_data)
{
}

static void
decoder_clear_internal(DecoderObject *self)
{
    unsigned int i;

    for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
        Py_CLEAR(self->out_byteobjs[i]);
        self->out_samples[i] = NULL;
        PyMem_Free(self->buf_samples[i]);
        self->buf_samples[i] = NULL;
    }

    self->out_count = 0;
    self->out_remaining = 0;
    self->buf_start = 0;
    self->buf_count = 0;
    self->buf_size = 0;
    memset(&self->out_attr, 0, sizeof(self->out_attr));
    memset(&self->buf_attr, 0, sizeof(self->buf_attr));
}

static DecoderObject *
newDecoderObject(PyObject *module, PyObject *fileobj)
{
    plibflac_module_state *st;
    DecoderObject *self;
    unsigned int i;

    st = PyModule_GetState(module);
    if (st == NULL)
        return NULL;

    self = PyObject_GC_New(DecoderObject, (PyTypeObject *) st->Decoder_Type);
    if (self == NULL)
        return NULL;

    self->thread_state = NULL;
    self->busy_method = NULL;
    self->decoder = FLAC__stream_decoder_new();
    self->eof = 0;
    self->module = module;
    Py_XINCREF(self->module);
    self->fileobj = fileobj;
    Py_XINCREF(self->fileobj);

    PyObject_GC_Track((PyObject *) self);

    for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
        self->out_byteobjs[i] = NULL;
        self->out_samples[i] = NULL;
        self->buf_samples[i] = NULL;
    }

    if (self->decoder == NULL) {
        PyErr_NoMemory();
        Py_XDECREF(self);
        return NULL;
    }

    decoder_clear_internal(self);

    return self;
}

static int
Decoder_traverse(DecoderObject *self, visitproc visit, void *arg)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    Py_VISIT(self->module);
    Py_VISIT(self->fileobj);
    Py_END_CRITICAL_SECTION();
    return 0;
}

static int
Decoder_clear(DecoderObject *self)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    Py_CLEAR(self->module);
    Py_CLEAR(self->fileobj);
    Py_END_CRITICAL_SECTION();
    return 0;
}

static void
Decoder_dealloc(DecoderObject *self)
{
    PyObject_GC_UnTrack((PyObject *) self);

    decoder_clear_internal(self);

    Py_CLEAR(self->module);
    Py_CLEAR(self->fileobj);

    if (self->decoder)
        FLAC__stream_decoder_delete(self->decoder);

    PyObject_GC_Del(self);
}

static PyObject *
Decoder_new(PyTypeObject *subtype, PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_TypeError, "cannot create 'Decoder' instances");
    return NULL;
}

static PyObject *
Decoder_open(DecoderObject *self, PyObject *args)
{
    FLAC__StreamDecoderInitStatus status;
    PyObject *seekable;
    PyObject *result = NULL;

    BEGIN_NO_RECURSION("open");
    if (!PyArg_ParseTuple(args, "i:open", &self->fd))
        goto done;

    seekable = PyObject_CallMethod(self->fileobj, "seekable", "()");
    self->seekable = seekable ? PyObject_IsTrue(seekable) : 0;
    Py_XDECREF(seekable);
    if (PyErr_Occurred())
        goto done;

    BEGIN_PROCESSING();
    if (self->fd >= 0)
        status = FLAC__stream_decoder_init_stream(self->decoder,
                                                  &decoder_read_fd,
                                                  &decoder_seek_fd,
                                                  &decoder_tell_fd,
                                                  &decoder_length,
                                                  &decoder_eof,
                                                  &decoder_write,
                                                  &decoder_metadata,
                                                  &decoder_error,
                                                  self);
    else
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
    END_PROCESSING();

    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        PyErr_Format(get_error_type(self->module),
                     "init_stream failed (state = %s)",
                     FLAC__StreamDecoderInitStatusString[status]);
        goto done;
    }

    decoder_clear_internal(self);

    Py_INCREF((result = Py_None));

 done:                                          \
    END_NO_RECURSION;
    return result;
}

static PyObject *
Decoder_close(DecoderObject *self, PyObject *args)
{
    FLAC__bool ok;
    PyObject *result = NULL;

    BEGIN_NO_RECURSION("close");
    if (!PyArg_ParseTuple(args, ":close"))
        goto done;

    decoder_clear_internal(self);

    BEGIN_PROCESSING();
    ok = FLAC__stream_decoder_finish(self->decoder);
    END_PROCESSING();

    if (!ok) {
        PyErr_Format(get_error_type(self->module),
                     "finish failed (MD5 hash incorrect)");
        goto done;
    }

    Py_INCREF((result = Py_None));

 done:
    END_NO_RECURSION;
    return result;
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

    BEGIN_NO_RECURSION("read");
    if (!PyArg_ParseTuple(args, "n:read", &limit))
        goto done;

    self->out_remaining = limit;

    out_count = self->out_remaining;
    if (out_count > self->buf_count)
        out_count = self->buf_count;
    if (out_count > 0) {
        BEGIN_PROCESSING();
        ok = (write_out_samples(self, self->buf_samples,
                                self->buf_attr.channels,
                                self->buf_start, out_count) >= 0);
        END_PROCESSING();
        if (!ok)
            goto fail;

        self->out_attr = self->buf_attr;
        self->buf_start += out_count;
        self->buf_count -= out_count;
    }

    while (self->out_remaining > 0 && self->buf_count == 0) {
        BEGIN_PROCESSING();

        ok = FLAC__stream_decoder_process_single(self->decoder);

        state = FLAC__stream_decoder_get_state(self->decoder);
        if (state == FLAC__STREAM_DECODER_ABORTED)
            FLAC__stream_decoder_flush(self->decoder);

        END_PROCESSING();

        if (PyErr_Occurred())
            goto fail;

        if ((state == FLAC__STREAM_DECODER_END_OF_STREAM ||
             state == FLAC__STREAM_DECODER_ABORTED))
            break;

        if (!ok) {
            PyErr_Format(get_error_type(self->module),
                         "process_single failed (state = %s)",
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
        self->out_samples[i] = NULL;
    }

    self->out_count = 0;
    self->out_remaining = 0;

 done:
    END_NO_RECURSION;
    return result;
}

static PyObject *
Decoder_read_metadata(DecoderObject *self, PyObject *args)
{
    FLAC__bool ok;
    FLAC__StreamDecoderState state;
    PyObject *result = NULL;

    BEGIN_NO_RECURSION("read_metadata");
    if (!PyArg_ParseTuple(args, ":read_metadata"))
        goto done;

    BEGIN_PROCESSING();

    ok = FLAC__stream_decoder_process_until_end_of_metadata(self->decoder);

    state = FLAC__stream_decoder_get_state(self->decoder);
    if (state == FLAC__STREAM_DECODER_ABORTED)
        FLAC__stream_decoder_flush(self->decoder);

    END_PROCESSING();

    if (PyErr_Occurred())
        goto done;

    if (!ok) {
        PyErr_Format(get_error_type(self->module),
                     "read_metadata failed (state = %s)",
                     FLAC__StreamDecoderStateString[state]);
        goto done;
    }

    Py_INCREF((result = Py_None));

 done:
    END_NO_RECURSION;
    return result;
}

static PyObject *
Decoder_seek(DecoderObject *self, PyObject *args)
{
    PyObject *arg = NULL, *result = NULL;
    FLAC__uint64 sample_number;
    FLAC__bool ok;
    FLAC__StreamDecoderState state;

    BEGIN_NO_RECURSION("seek");
    if (!PyArg_ParseTuple(args, "O:seek", &arg))
        goto done;
    sample_number = Long_AsUint64(arg);
    if (PyErr_Occurred())
        goto done;

    self->buf_count = 0;

    BEGIN_PROCESSING();

    ok = FLAC__stream_decoder_seek_absolute(self->decoder, sample_number);

    state = FLAC__stream_decoder_get_state(self->decoder);
    if ((state == FLAC__STREAM_DECODER_ABORTED ||
         state == FLAC__STREAM_DECODER_SEEK_ERROR))
        FLAC__stream_decoder_flush(self->decoder);

    END_PROCESSING();

    if (PyErr_Occurred())
        goto done;

    if (!ok) {
        PyErr_Format(get_error_type(self->module),
                     "seek_absolute failed (state = %s)",
                     FLAC__StreamDecoderStateString[state]);
        goto done;
    }

    Py_INCREF((result = Py_None));

 done:
    END_NO_RECURSION;
    return result;
}

static PyObject*
Decoder_total_samples_getter(DecoderObject* self, void *closure)
{
    FLAC__uint64 n;
    Py_BEGIN_CRITICAL_SECTION(self);
    n = FLAC__stream_decoder_get_total_samples(self->decoder);
    Py_END_CRITICAL_SECTION();
    return PyLong_FromUnsignedLongLong(n);
}

static PyMethodDef Decoder_methods[] = {
    {"close", (PyCFunction)Decoder_close, METH_VARARGS,
     PyDoc_STR("close() -> None")},
    {"open", (PyCFunction)Decoder_open, METH_VARARGS,
     PyDoc_STR("open(fd) -> None")},
    {"read", (PyCFunction)Decoder_read, METH_VARARGS,
     PyDoc_STR("read(n_samples) -> tuple of arrays, or None")},
    {"read_metadata", (PyCFunction)Decoder_read_metadata, METH_VARARGS,
     PyDoc_STR("read_metadata() -> None")},
    {"seek", (PyCFunction)Decoder_seek, METH_VARARGS,
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

PROPERTY_FUNCS(Decoder, decoder, md5_checking, FLAC__bool,
               PyBool_FromLong, Long_AsBool)

static PyGetSetDef Decoder_properties[] = {
    PROPERTY_DEF_RO(Decoder, total_samples),
    PROPERTY_DEF_RW(Decoder, md5_checking),
    {NULL}
};

static PyType_Slot Decoder_Type_slots[] = {
    {Py_tp_dealloc,  Decoder_dealloc},
    {Py_tp_traverse, Decoder_traverse},
    {Py_tp_clear,    Decoder_clear},
    {Py_tp_new,      Decoder_new},
    {Py_tp_methods,  Decoder_methods},
    {Py_tp_members,  Decoder_members},
    {Py_tp_getset,   Decoder_properties},
    {0, 0}
};

static PyType_Spec Decoder_Type_spec = {
    "_plibflac.Decoder",
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

    return (PyObject *) newDecoderObject(self, fileobj);
}

/****************************************************************/
/* Encoder objects */

typedef struct {
    PyObject_HEAD

    PyThreadState       *thread_state;
    const char          *busy_method;
    PyObject            *module;

    PyObject            *fileobj;
    FLAC__StreamEncoder *encoder;
    char                 seekable;

    int32_t              compression_level;
    PyObject            *apodization;
} EncoderObject;

static FLAC__StreamEncoderWriteStatus
encoder_write(const FLAC__StreamEncoder *encoder,
              const FLAC__byte           buffer[],
              size_t                     bytes,
              uint32_t                   samples,
              uint32_t                   current_frame,
              void                      *client_data)
{
    EncoderObject *self = client_data;
    PyObject *bytesobj, *count;
    size_t n;
    FLAC__StreamEncoderWriteStatus status;

    BEGIN_CALLBACK();

    while (bytes > 0) {
        PyErr_CheckSignals();
        if (PyErr_Occurred())
            break;

        bytesobj = PyBytes_FromStringAndSize((void *) buffer, bytes);
        count = PyObject_CallMethod(self->fileobj, "write", "(O)", bytesobj);
        n = check_return_uint(count, "write", "encoder_write", bytes);
        Py_XDECREF(bytesobj);
        Py_XDECREF(count);

        if (PyErr_Occurred())
            break;
        else
            bytes -= n;
    }

    if (bytes == 0)
        status = FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    else
        status = FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;

    END_CALLBACK();
    return status;
}

static FLAC__StreamEncoderSeekStatus
encoder_seek(const FLAC__StreamEncoder *encoder,
             FLAC__uint64               absolute_byte_offset,
             void                      *client_data)
{
    EncoderObject *self = client_data;
    PyObject *dummy;
    FLAC__StreamEncoderSeekStatus status;

    if (!self->seekable)
        return FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;

    BEGIN_CALLBACK();

    dummy = PyObject_CallMethod(self->fileobj, "seek", "(K)",
                                (unsigned long long) absolute_byte_offset);
    check_return_uint(dummy, "seek", "encoder_seek", (FLAC__uint64) -1);
    Py_XDECREF(dummy);

    if (PyErr_Occurred())
        status = FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    else
        status = FLAC__STREAM_ENCODER_SEEK_STATUS_OK;

    END_CALLBACK();
    return status;
}

static FLAC__StreamEncoderTellStatus
encoder_tell(const FLAC__StreamEncoder *encoder,
             FLAC__uint64              *absolute_byte_offset,
             void                      *client_data)
{
    EncoderObject *self = client_data;
    PyObject *result;
    FLAC__uint64 pos;
    FLAC__StreamEncoderSeekStatus status;

    if (!self->seekable)
        return FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;

    BEGIN_CALLBACK();

    result = PyObject_CallMethod(self->fileobj, "tell", "()");
    pos = check_return_uint(result, "tell", "encoder_tell",
                            (FLAC__uint64) -1);
    Py_XDECREF(result);

    if (PyErr_Occurred()) {
        status = FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
    } else {
        *absolute_byte_offset = pos;
        status = FLAC__STREAM_ENCODER_TELL_STATUS_OK;
    }

    END_CALLBACK();
    return status;
}

static EncoderObject *
newEncoderObject(PyObject *module, PyObject *fileobj)
{
    plibflac_module_state *st;
    EncoderObject *self;

    st = PyModule_GetState(module);
    if (st == NULL)
        return NULL;

    self = PyObject_GC_New(EncoderObject, (PyTypeObject *) st->Encoder_Type);
    if (self == NULL)
        return NULL;

    self->thread_state = NULL;
    self->busy_method = NULL;
    self->encoder = FLAC__stream_encoder_new();
    self->module = module;
    Py_XINCREF(self->module);
    self->fileobj = fileobj;
    Py_XINCREF(self->fileobj);
    self->apodization = NULL;
    self->compression_level = 0;

    PyObject_GC_Track((PyObject *) self);

    if (self->encoder == NULL) {
        PyErr_NoMemory();
        Py_XDECREF(self);
        return NULL;
    }

    return self;
}

static int
Encoder_traverse(EncoderObject *self, visitproc visit, void *arg)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    Py_VISIT(self->module);
    Py_VISIT(self->fileobj);
    Py_VISIT(self->apodization);
    Py_END_CRITICAL_SECTION();
    return 0;
}

static int
Encoder_clear(EncoderObject *self)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    Py_CLEAR(self->module);
    Py_CLEAR(self->fileobj);
    Py_CLEAR(self->apodization);
    Py_END_CRITICAL_SECTION();
    return 0;
}

static void
Encoder_dealloc(EncoderObject *self)
{
    PyObject_GC_UnTrack((PyObject *) self);

    Py_CLEAR(self->module);
    Py_CLEAR(self->fileobj);
    Py_CLEAR(self->apodization);

    if (self->encoder)
        FLAC__stream_encoder_delete(self->encoder);

    PyObject_GC_Del(self);
}

static PyObject *
Encoder_new(PyTypeObject *subtype, PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_TypeError, "cannot create 'Encoder' instances");
    return NULL;
}

static PyObject *
Encoder_open(EncoderObject *self, PyObject *args)
{
    FLAC__StreamEncoderInitStatus status;
    PyObject *seekable, *result = NULL;

    BEGIN_NO_RECURSION("open");
    if (!PyArg_ParseTuple(args, ":open"))
        goto done;

    seekable = PyObject_CallMethod(self->fileobj, "seekable", "()");
    self->seekable = seekable ? PyObject_IsTrue(seekable) : 0;
    Py_XDECREF(seekable);
    if (PyErr_Occurred())
        goto done;

    BEGIN_PROCESSING();
    status = FLAC__stream_encoder_init_stream(self->encoder,
                                              &encoder_write,
                                              &encoder_seek,
                                              &encoder_tell,
                                              NULL, self);
    END_PROCESSING();

    if (PyErr_Occurred())
        goto done;

    if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        PyErr_Format(get_error_type(self->module),
                     "init_stream failed (state = %s)",
                     FLAC__StreamEncoderInitStatusString[status]);
        goto done;
    }

    Py_INCREF((result = Py_None));

 done:
    END_NO_RECURSION;
    return result;
}

static PyObject *
Encoder_close(EncoderObject *self, PyObject *args)
{
    PyObject *result = NULL;
    FLAC__StreamEncoderState state;
    FLAC__bool ok;

    BEGIN_NO_RECURSION("close");
    if (!PyArg_ParseTuple(args, ":close"))
        goto done;


    BEGIN_PROCESSING();
    ok = FLAC__stream_encoder_finish(self->encoder);
    END_PROCESSING();

    if (PyErr_Occurred())
        goto done;

    if (!ok) {
        state = FLAC__stream_encoder_get_state(self->encoder);
        PyErr_Format(get_error_type(self->module),
                     "finish failed (state = %s)",
                     FLAC__StreamEncoderStateString[state]);
        goto done;
    }

    Py_INCREF((result = Py_None));

 done:
    END_NO_RECURSION;
    return result;
}

static PyObject *
Encoder_write(EncoderObject *self, PyObject *args)
{
    PyObject *seq, *arrays[FLAC__MAX_CHANNELS] = {NULL},
        *memview, *memview2, *result = NULL;
    FLAC__int32 *data[FLAC__MAX_CHANNELS] = {NULL};
    size_t channels, i;
    Py_ssize_t nsamples = 0, nsamples_i;
    FLAC__StreamEncoderState state;
    FLAC__bool ok;

    BEGIN_NO_RECURSION("write");
    if (!PyArg_ParseTuple(args, "O:write", &seq))
        goto done;

    channels = PySequence_Length(seq);
    if (PyErr_Occurred())
        goto done;

    if (channels != FLAC__stream_encoder_get_channels(self->encoder)) {
        PyErr_SetString(PyExc_ValueError, "length of sequence "
                        "must match number of channels");
        goto done;
    }

    for (i = 0; i < channels; i++) {
        arrays[i] = PySequence_GetItem(seq, i);
        if (!arrays[i])
            goto done;
        nsamples_i = PySequence_Length(arrays[i]);
        if (PyErr_Occurred())
            goto done;

        if (i == 0) {
            nsamples = nsamples_i;
        } else if (nsamples_i != nsamples) {
            PyErr_Format(PyExc_ValueError, "length of channel %u (%zu) "
                         "must match length of channel 0 (%zu)",
                         i, nsamples_i, nsamples);
            goto done;
        }
    }

    for (i = 0; i < channels; i++) {
        data[i] = PyMem_New(FLAC__int32, nsamples);
        if (!data[i]) {
            PyErr_NoMemory();
            goto done;
        }

        memview = MemoryView_FromMem(data[i], nsamples * sizeof(FLAC__int32));
        memview2 = PyObject_CallMethod(memview, "cast", "(s)", INT32_FORMAT);
        Py_XDECREF(memview);
        if (PySequence_SetSlice(memview2, 0, nsamples, arrays[i]) < 0) {
            Py_XDECREF(memview2);
            goto done;
        }
        Py_XDECREF(memview2);
        Py_CLEAR(arrays[i]);
    }

    BEGIN_PROCESSING();
    ok = FLAC__stream_encoder_process(self->encoder,
                                      (const FLAC__int32 **) data,
                                      nsamples);
    END_PROCESSING();

    if (PyErr_Occurred())
        goto done;

    if (!ok) {
        state = FLAC__stream_encoder_get_state(self->encoder);
        PyErr_Format(get_error_type(self->module),
                     "process failed (state = %s)",
                     FLAC__StreamEncoderStateString[state]);
        goto done;
    }

    Py_INCREF((result = Py_None));

 done:
    END_NO_RECURSION;

    for (i = 0; i < FLAC__MAX_CHANNELS; i++) {
        PyMem_Free(data[i]);
        Py_CLEAR(arrays[i]);
    }

    return result;
}

static PyMethodDef Encoder_methods[] = {
    {"close", (PyCFunction)Encoder_close, METH_VARARGS,
     PyDoc_STR("close() -> None")},
    {"open", (PyCFunction)Encoder_open, METH_VARARGS,
     PyDoc_STR("open() -> None")},
    {"write", (PyCFunction)Encoder_write, METH_VARARGS,
     PyDoc_STR("write(sample_arrays) -> None")},
    {NULL}
};

PROPERTY_UINT32(Encoder, encoder, channels)
PROPERTY_UINT32(Encoder, encoder, bits_per_sample)
PROPERTY_UINT32(Encoder, encoder, sample_rate)
PROPERTY_UINT64(Encoder, encoder, total_samples_estimate)
PROPERTY_BOOL(Encoder, encoder, streamable_subset)
PROPERTY_BOOL(Encoder, encoder, verify)
PROPERTY_UINT32(Encoder, encoder, blocksize)
PROPERTY_BOOL(Encoder, encoder, do_mid_side_stereo)
PROPERTY_BOOL(Encoder, encoder, loose_mid_side_stereo)
PROPERTY_UINT32(Encoder, encoder, max_lpc_order)
PROPERTY_UINT32(Encoder, encoder, qlp_coeff_precision)
PROPERTY_BOOL(Encoder, encoder, do_qlp_coeff_prec_search)
PROPERTY_BOOL(Encoder, encoder, do_exhaustive_model_search)
PROPERTY_UINT32(Encoder, encoder, min_residual_partition_order)
PROPERTY_UINT32(Encoder, encoder, max_residual_partition_order)

static PyObject *
Encoder_compression_level_getter(EncoderObject *self, void *closure)
{
    unsigned long value;
    Py_BEGIN_CRITICAL_SECTION(self);
    value = self->compression_level;
    Py_END_CRITICAL_SECTION();
    return PyLong_FromUnsignedLong(value);
}

static int
Encoder_compression_level_setter(EncoderObject *self, PyObject *value,
                                 void *closure)
{
    uint32_t n;
    FLAC__bool ok = 0;
    if (!value) {
        PyErr_Format(PyExc_AttributeError,
                     "cannot delete attribute 'compression_level'");
        return -1;
    }
    if (!PyLong_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "invalid type for attribute 'compression_level'");
        return -1;
    }
    n = Long_AsUint32(value);
    if (PyErr_Occurred())
        return -1;
    BEGIN_NO_RECURSION(".compression_level");
    ok = FLAC__stream_encoder_set_compression_level(self->encoder, n);
    END_NO_RECURSION;
    if (!ok) {
        PyErr_Format(PyExc_ValueError,
                     "cannot set 'compression_level' after open()");
        return -1;
    }
    self->compression_level = n;
    Py_CLEAR(self->apodization);
    return 0;
}

static PyObject *
Encoder_apodization_getter(EncoderObject *self, void *closure)
{
    PyObject *value;
    Py_BEGIN_CRITICAL_SECTION(self);
    value = self->apodization ? self->apodization : Py_None;
    Py_INCREF(value);
    Py_END_CRITICAL_SECTION();
    return value;
}

static int
Encoder_apodization_setter(EncoderObject *self, PyObject *value,
                           void *closure)
{
    PyObject *bytes;
    char *s;
    Py_ssize_t len;

    if (!value) {
        PyErr_Format(PyExc_AttributeError,
                     "cannot delete attribute 'apodization'");
        return -1;
    }
    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "invalid type for attribute 'apodization'");
        return -1;
    }

    BEGIN_NO_RECURSION(".apodization");
    bytes = PyUnicode_AsUTF8String(value);
    if (bytes && PyBytes_AsStringAndSize(bytes, &s, &len) == 0) {
        if (len != (Py_ssize_t) strlen(s)) {
            PyErr_SetString(PyExc_ValueError, "embedded null character");
        }
        else if (!FLAC__stream_encoder_set_apodization(self->encoder, s)) {
            PyErr_Format(PyExc_ValueError,
                         "cannot set 'apodization' after open()");
        }
    }
    Py_XDECREF(bytes);
    END_NO_RECURSION;

    if (PyErr_Occurred())
        return -1;

    Py_INCREF(value);
    Py_CLEAR(self->apodization);
    self->apodization = value;
    return 0;
}

static PyGetSetDef Encoder_properties[] = {
    PROPERTY_DEF_RW(Encoder, channels),
    PROPERTY_DEF_RW(Encoder, bits_per_sample),
    PROPERTY_DEF_RW(Encoder, sample_rate),
    PROPERTY_DEF_RW(Encoder, total_samples_estimate),
    PROPERTY_DEF_RW(Encoder, compression_level),
    PROPERTY_DEF_RW(Encoder, streamable_subset),
    PROPERTY_DEF_RW(Encoder, verify),
    PROPERTY_DEF_RW(Encoder, blocksize),
    PROPERTY_DEF_RW(Encoder, do_mid_side_stereo),
    PROPERTY_DEF_RW(Encoder, loose_mid_side_stereo),
    PROPERTY_DEF_RW(Encoder, apodization),
    PROPERTY_DEF_RW(Encoder, max_lpc_order),
    PROPERTY_DEF_RW(Encoder, qlp_coeff_precision),
    PROPERTY_DEF_RW(Encoder, do_qlp_coeff_prec_search),
    PROPERTY_DEF_RW(Encoder, do_exhaustive_model_search),
    PROPERTY_DEF_RW(Encoder, min_residual_partition_order),
    PROPERTY_DEF_RW(Encoder, max_residual_partition_order),
    {NULL}
};

static PyType_Slot Encoder_Type_slots[] = {
    {Py_tp_dealloc,  Encoder_dealloc},
    {Py_tp_traverse, Encoder_traverse},
    {Py_tp_clear,    Encoder_clear},
    {Py_tp_new,      Encoder_new},
    {Py_tp_methods,  Encoder_methods},
    {Py_tp_getset,   Encoder_properties},
    {0, 0}
};

static PyType_Spec Encoder_Type_spec = {
    "_plibflac.Encoder",
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

    return (PyObject *) newEncoderObject(self, fileobj);
}

/****************************************************************/

static PyObject *
plibflac_flac_vendor(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":flac_vendor"))
        return NULL;
    return PyUnicode_FromString(FLAC__VENDOR_STRING);
}

static PyObject *
plibflac_flac_version(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":flac_version"))
        return NULL;
    return PyUnicode_FromString(FLAC__VERSION_STRING);
}

static PyMethodDef plibflac_methods[] = {
    {"decoder", plibflac_decoder, METH_VARARGS,
     PyDoc_STR("decoder(fileobj) -> new Decoder object")},
    {"encoder", plibflac_encoder, METH_VARARGS,
     PyDoc_STR("encoder(fileobj) -> new Encoder object")},
    {"flac_vendor", plibflac_flac_vendor, METH_VARARGS,
     PyDoc_STR("flac_vendor() -> str")},
    {"flac_version", plibflac_flac_version, METH_VARARGS,
     PyDoc_STR("flac_version() -> str")},
    {NULL, NULL}
};

PyDoc_STRVAR(module_doc,
"Low-level functions for reading and writing FLAC streams.");

static int
plibflac_exec(PyObject *m)
{
    plibflac_module_state *st = PyModule_GetState(m);

#ifdef PLIBFLAC_VERSION
    if (PyModule_AddStringConstant(m, "__version__", PLIBFLAC_VERSION) < 0)
        return -1;
#endif

    if (st->Decoder_Type == NULL) {
        st->Decoder_Type = PyType_FromSpec(&Decoder_Type_spec);
        if (st->Decoder_Type == NULL)
            return -1;
    }

    if (st->Encoder_Type == NULL) {
        st->Encoder_Type = PyType_FromSpec(&Encoder_Type_spec);
        if (st->Encoder_Type == NULL)
            return -1;
    }

    if (st->Error_Type == NULL) {
        st->Error_Type = PyErr_NewException("plibflac.Error", NULL, NULL);
        if (st->Error_Type == NULL)
            return -1;
    }
    Py_INCREF(st->Error_Type);
    if (PyModule_AddObject(m, "Error", st->Error_Type) < 0) {
        Py_DECREF(st->Error_Type);
        return -1;
    }

    return 0;
}

static int
plibflac_traverse(PyObject *m, visitproc visit, void *arg)
{
    plibflac_module_state *st = PyModule_GetState(m);
    if (st) {
        Py_VISIT(st->Decoder_Type);
        Py_VISIT(st->Encoder_Type);
        Py_VISIT(st->Error_Type);
    }
    return 0;
}

static int
plibflac_clear(PyObject *m)
{
    plibflac_module_state *st = PyModule_GetState(m);
    if (st) {
        Py_CLEAR(st->Decoder_Type);
        Py_CLEAR(st->Encoder_Type);
        Py_CLEAR(st->Error_Type);
    }
    return 0;
}

static void
plibflac_free(void *m)
{
    plibflac_module_state *st = PyModule_GetState(m);
    if (st) {
        Py_CLEAR(st->Decoder_Type);
        Py_CLEAR(st->Encoder_Type);
        Py_CLEAR(st->Error_Type);
    }
}

static struct PyModuleDef_Slot plibflac_slots[4];

static struct PyModuleDef plibflacmodule = {
    PyModuleDef_HEAD_INIT,
    "_plibflac",
    module_doc,
    sizeof(plibflac_module_state),
    plibflac_methods,
    plibflac_slots,
    plibflac_traverse,
    plibflac_clear,
    plibflac_free
};

#if __GNUC__ >= 4
__attribute__((visibility("default")))
#endif
PyMODINIT_FUNC
PyInit__plibflac(void)
{
    unsigned long version = get_python_version();
    int s = 0;

    plibflac_slots[s].slot = Py_mod_exec;
    plibflac_slots[s].value = plibflac_exec;
    s++;
    if (version >= 0x030c0000) {
        plibflac_slots[s].slot = Py_mod_multiple_interpreters;
        plibflac_slots[s].value = Py_MOD_PER_INTERPRETER_GIL_SUPPORTED;
        s++;
    }
#ifdef Py_GIL_DISABLED
    plibflac_slots[s].slot = Py_mod_gil;
    plibflac_slots[s].value = Py_MOD_GIL_NOT_USED;
    s++;
#endif
    plibflac_slots[s].slot = 0;
    plibflac_slots[s].value = NULL;

    return PyModuleDef_Init(&plibflacmodule);
}
