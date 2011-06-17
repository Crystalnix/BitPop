/*
 * $Id: _psutil_osx.h 780 2010-11-10 18:42:47Z jloden $
 *
 * OS X platform-specific module methods for _psutil_osx
 */

#include <Python.h>

// --- per-process functions
static PyObject* get_process_name(PyObject* self, PyObject* args);
static PyObject* get_process_cmdline(PyObject* self, PyObject* args);
static PyObject* get_process_ppid(PyObject* self, PyObject* args);
static PyObject* get_process_uid(PyObject* self, PyObject* args);
static PyObject* get_process_gid(PyObject* self, PyObject* args);
static PyObject* get_cpu_times(PyObject* self, PyObject* args);
static PyObject* get_process_create_time(PyObject* self, PyObject* args);
static PyObject* get_memory_info(PyObject* self, PyObject* args);
static PyObject* get_process_num_threads(PyObject* self, PyObject* args);

// --- system-related functions
static int pid_exists(long pid);
static PyObject* get_pid_list(PyObject* self, PyObject* args);
static PyObject* get_num_cpus(PyObject* self, PyObject* args);
static PyObject* get_total_phymem(PyObject* self, PyObject* args);
static PyObject* get_avail_phymem(PyObject* self, PyObject* args);
static PyObject* get_total_virtmem(PyObject* self, PyObject* args);
static PyObject* get_avail_virtmem(PyObject* self, PyObject* args);
static PyObject* get_system_cpu_times(PyObject* self, PyObject* args);

