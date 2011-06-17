/*
 * $Id: _psutil_osx.c 780 2010-11-10 18:42:47Z jloden $
 *
 * OS X platform-specific module methods for _psutil_osx
 */

#include <Python.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <mach/mach.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/mach_traps.h>
#include <mach/shared_memory_server.h>

#include "_psutil_osx.h"
#include "arch/osx/process_info.h"


/*
 * define the psutil C module methods and initialize the module.
 */
static PyMethodDef
PsutilMethods[] =
{
     // --- per-process functions

     {"get_process_name", get_process_name, METH_VARARGS,
        "Return process name"},
     {"get_process_cmdline", get_process_cmdline, METH_VARARGS,
        "Return process cmdline as a list of cmdline arguments"},
     {"get_process_ppid", get_process_ppid, METH_VARARGS,
        "Return process ppid as an integer"},
     {"get_process_uid", get_process_uid, METH_VARARGS,
        "Return process real user id as an integer"},
     {"get_process_gid", get_process_gid, METH_VARARGS,
        "Return process real group id as an integer"},
     {"get_cpu_times", get_cpu_times, METH_VARARGS,
           "Return tuple of user/kern time for the given PID"},
     {"get_process_create_time", get_process_create_time, METH_VARARGS,
         "Return a float indicating the process create time expressed in "
         "seconds since the epoch"},
     {"get_memory_info", get_memory_info, METH_VARARGS,
         "Return a tuple of RSS/VMS memory information"},
     {"get_process_num_threads", get_process_num_threads, METH_VARARGS,
         "Return number of threads used by process"},

     // --- system-related functions

     {"get_pid_list", get_pid_list, METH_VARARGS,
         "Returns a list of PIDs currently running on the system"},
     {"get_num_cpus", get_num_cpus, METH_VARARGS,
           "Return number of CPUs on the system"},
     {"get_total_phymem", get_total_phymem, METH_VARARGS,
         "Return the total amount of physical memory, in bytes"},
     {"get_avail_phymem", get_avail_phymem, METH_VARARGS,
         "Return the amount of available physical memory, in bytes"},
     {"get_total_virtmem", get_total_virtmem, METH_VARARGS,
         "Return the total amount of virtual memory, in bytes"},
     {"get_avail_virtmem", get_avail_virtmem, METH_VARARGS,
         "Return the amount of available virtual memory, in bytes"},
     {"get_system_cpu_times", get_system_cpu_times, METH_VARARGS,
         "Return system cpu times as a tuple (user, system, nice, idle, irc)"},

     {NULL, NULL, 0, NULL}
};


/*
 * Raises an OSError(errno=ESRCH, strerror="No such process") exception
 * in Python.
 */
static PyObject*
NoSuchProcess(void) {
    errno = ESRCH;
    return PyErr_SetFromErrno(PyExc_OSError);
}

/*
 * Raises an OSError(errno=EPERM, strerror="Operation not permitted") exception
 * in Python.
 */
static PyObject*
AccessDenied(void) {
    errno = EPERM;
    return PyErr_SetFromErrno(PyExc_OSError);
}

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif

#if PY_MAJOR_VERSION >= 3

static int
psutil_osx_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int
psutil_osx_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}


static struct PyModuleDef
moduledef = {
    PyModuleDef_HEAD_INIT,
    "psutil_osx",
    NULL,
    sizeof(struct module_state),
    PsutilMethods,
    NULL,
    psutil_osx_traverse,
    psutil_osx_clear,
    NULL
};

#define INITERROR return NULL

PyObject *
PyInit__psutil_osx(void)

#else
#define INITERROR return

void
init_psutil_osx(void)
#endif
{
#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule("_psutil_osx", PsutilMethods);
#endif
    if (module == NULL) {
        INITERROR;
    }
    struct module_state *st = GETSTATE(module);

    st->error = PyErr_NewException("_psutil_osx.Error", NULL, NULL);
    if (st->error == NULL) {
        Py_DECREF(module);
        INITERROR;
    }
#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}


/*
 * Return a Python list of all the PIDs running on the system.
 */
static PyObject*
get_pid_list(PyObject* self, PyObject* args)
{
    kinfo_proc *proclist = NULL;
    kinfo_proc *orig_address = NULL;
    size_t num_processes;
    size_t idx;
    PyObject *pid;
    PyObject *retlist = PyList_New(0);

    if (get_proc_list(&proclist, &num_processes) != 0) {
        Py_DECREF(retlist);
        PyErr_SetString(PyExc_RuntimeError, "failed to retrieve process list.");
        return NULL;
    }

    if (num_processes > 0) {
        // save the address of proclist so we can free it later
        orig_address = proclist;
        for (idx=0; idx < num_processes; idx++) {
            pid = Py_BuildValue("i", proclist->kp_proc.p_pid);
            PyList_Append(retlist, pid);
            Py_XDECREF(pid);
            proclist++;
        }
    }
    free(orig_address);
    return retlist;
}


/*
 * Return process name from kinfo_proc as a Python string.
 */
static PyObject*
get_process_name(PyObject* self, PyObject* args)
{
    long pid;
    struct kinfo_proc kp;
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }
    if (get_kinfo_proc(pid, &kp) == -1) {
        return NULL;
    }
    return Py_BuildValue("s", kp.kp_proc.p_comm);
}


/*
 * Return process cmdline as a Python list of cmdline arguments.
 */
static PyObject*
get_process_cmdline(PyObject* self, PyObject* args)
{
    long pid;
    PyObject* arglist = NULL;

    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }

    // get the commandline, defined in arch/osx/process_info.c
    arglist = get_arg_list(pid);

    // get_arg_list() returns NULL only if getcmdargs failed with ESRCH
    // (no process with that PID)
    if (NULL == arglist) {
        return PyErr_SetFromErrno(PyExc_OSError);
    }
    return Py_BuildValue("N", arglist);
}


/*
 * Return process parent pid from kinfo_proc as a Python integer.
 */
static PyObject*
get_process_ppid(PyObject* self, PyObject* args)
{
    long pid;
    struct kinfo_proc kp;
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }
    if (get_kinfo_proc(pid, &kp) == -1) {
        return NULL;
    }
    return Py_BuildValue("l", (long)kp.kp_eproc.e_ppid);
}


/*
 * Return process real uid from kinfo_proc as a Python integer.
 */
static PyObject*
get_process_uid(PyObject* self, PyObject* args)
{
    long pid;
    struct kinfo_proc kp;
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }
    if (get_kinfo_proc(pid, &kp) == -1) {
        return NULL;
    }
    return Py_BuildValue("l", (long)kp.kp_eproc.e_pcred.p_ruid);
}


/*
 * Return process real group id from ki_comm as a Python integer.
 */
static PyObject*
get_process_gid(PyObject* self, PyObject* args)
{
    long pid;
    struct kinfo_proc kp;
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }
    if (get_kinfo_proc(pid, &kp) == -1) {
        return NULL;
    }
    return Py_BuildValue("l", (long)kp.kp_eproc.e_pcred.p_rgid);
}


/*
 * Return 1 if PID exists in the current process list, else 0.
 */
static int
pid_exists(long pid) {
    int kill_ret;

    // save some time if it's an invalid PID
    if (pid < 0) {
        return 0;
    }

    // if kill returns success of permission denied we know it's a valid PID
    kill_ret = kill(pid , 0);
    if ( (0 == kill_ret) || (EPERM == errno) ) {
        return 1;
    }

    // otherwise return 0 for PID not found
    return 0;
}


/*
 * Return a Python integer indicating the number of CPUs on the system.
 */
static PyObject*
get_num_cpus(PyObject* self, PyObject* args)
{

    int mib[2];
    int ncpu;
    size_t len;

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    len = sizeof(ncpu);

    if (sysctl(mib, 2, &ncpu, &len, NULL, 0) == -1) {
        PyErr_SetFromErrno(0);
        return NULL;
    }

    return Py_BuildValue("i", ncpu);
}


#define TV2DOUBLE(t)    ((t).tv_sec + (t).tv_usec / 1000000.0)

/*
 * Return a Python tuple (user_time, kernel_time)
 */
static PyObject*
get_cpu_times(PyObject* self, PyObject* args)
{
    long pid;
    int err;
    unsigned int info_count = TASK_BASIC_INFO_COUNT;
    task_port_t task;// = (task_port_t)NULL;
    time_value_t user_time, system_time;
    struct task_basic_info tasks_info;
    struct task_thread_times_info task_times;

    // the argument passed should be a process id
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }

    /* task_for_pid() requires special privileges
     * "This function can be called only if the process is owned by the
     * procmod group or if the caller is root."
     * - http://developer.apple.com/documentation/MacOSX/Conceptual/universal_binary/universal_binary_tips/chapter_5_section_19.html
     */
    err = task_for_pid(mach_task_self(), pid, &task);
    if ( err == KERN_SUCCESS) {
        info_count = TASK_BASIC_INFO_COUNT;
        err = task_info(task, TASK_BASIC_INFO, (task_info_t)&tasks_info, &info_count);
        if (err != KERN_SUCCESS) {
                if (err == 4) { // errcode 4 is "invalid argument" (access denied)
                    return AccessDenied();
                }

                //otherwise throw a runtime error with appropriate error code
                return PyErr_Format(PyExc_RuntimeError, "task_info(TASK_BASIC_INFO) failed for pid %lu - %s (%i)",
                       pid, mach_error_string(err), err);

        }

        info_count = TASK_THREAD_TIMES_INFO_COUNT;
        err = task_info(task, TASK_THREAD_TIMES_INFO, (task_info_t)&task_times, &info_count);
        if (err != KERN_SUCCESS) {
                if (err == 4) { // errcode 4 is "invalid argument" (access denied)
                    return AccessDenied();
                }

                return PyErr_Format(PyExc_RuntimeError, "task_info(TASK_THREAD_TIMES_INFO) failed for pid %lu - %s (%i)",
                        pid, mach_error_string(err), err);
        }
    }

    else { // task_for_pid failed
        if (! pid_exists(pid) ) {
            return NoSuchProcess();
        }

        // pid exists, so return AccessDenied error since task_for_pid() failed
        return AccessDenied();
    }

    float user_t = -1.0;
    float sys_t = -1.0;
    user_time = tasks_info.user_time;
    system_time = tasks_info.system_time;

    time_value_add(&user_time, &task_times.user_time);
    time_value_add(&system_time, &task_times.system_time);

    user_t = (float)user_time.seconds + ((float)user_time.microseconds / 1000000.0);
    sys_t = (float)system_time.seconds + ((float)system_time.microseconds / 1000000.0);
    return Py_BuildValue("(dd)", user_t, sys_t);
}


/*
 * Return a Python float indicating the process create time expressed in
 * seconds since the epoch.
 */
static PyObject*
get_process_create_time(PyObject* self, PyObject* args)
{
    long pid;
    struct kinfo_proc kp;
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }
    if (get_kinfo_proc(pid, &kp) == -1) {
        return NULL;
    }
    return Py_BuildValue("d", TV2DOUBLE(kp.kp_proc.p_starttime));
}


/*
 * Return a tuple of RSS and VMS memory usage.
 */
static PyObject*
get_memory_info(PyObject* self, PyObject* args)
{
    long pid;
    int err;
    unsigned int info_count = TASK_BASIC_INFO_COUNT;
    mach_port_t task;
    struct task_basic_info tasks_info;
    vm_region_basic_info_data_64_t  b_info;
    vm_address_t address = GLOBAL_SHARED_TEXT_SEGMENT;
    vm_size_t size;
    mach_port_t object_name;

    // the argument passed should be a process id
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }

    /* task_for_pid() requires special privileges
     * "This function can be called only if the process is owned by the
     * procmod group or if the caller is root."
     * - http://developer.apple.com/documentation/MacOSX/Conceptual/universal_binary/universal_binary_tips/chapter_5_section_19.html
     */
    err = task_for_pid(mach_task_self(), pid, &task);
    if ( err == KERN_SUCCESS) {
        info_count = TASK_BASIC_INFO_COUNT;
        err = task_info(task, TASK_BASIC_INFO, (task_info_t)&tasks_info, &info_count);
        if (err != KERN_SUCCESS) {
                if (err == 4) { // errcode 4 is "invalid argument" (access denied)
                    return AccessDenied();
                }

                //otherwise throw a runtime error with appropriate error code
                return PyErr_Format(PyExc_RuntimeError, "task_info(TASK_BASIC_INFO) failed for pid %lu - %s (%i)",
                       pid, mach_error_string(err), err);
        }

        /* Issue #73 http://code.google.com/p/psutil/issues/detail?id=73
         * adjust the virtual memory size down to account for
         * shared memory that task_info.virtual_size includes w/every process
         */
        info_count = VM_REGION_BASIC_INFO_COUNT_64;
        err = vm_region_64(task, &address, &size, VM_REGION_BASIC_INFO,
            (vm_region_info_t)&b_info, &info_count, &object_name);
        if (err == KERN_SUCCESS) {
            if (b_info.reserved && size == (SHARED_TEXT_REGION_SIZE) &&
                tasks_info.virtual_size > (SHARED_TEXT_REGION_SIZE + SHARED_DATA_REGION_SIZE)) {
                    tasks_info.virtual_size -= (SHARED_TEXT_REGION_SIZE + SHARED_DATA_REGION_SIZE);
            }
        }
    }

    else {
        if (! pid_exists(pid) ) {
            return NoSuchProcess();
        }

        // pid exists, so return AccessDenied error since task_for_pid() failed
        return AccessDenied();
    }

    return Py_BuildValue("(ll)", tasks_info.resident_size, tasks_info.virtual_size);
}


/*
 * Return number of threads used by process as a Python integer.
 */
static PyObject*
get_process_num_threads(PyObject* self, PyObject* args)
{
    long pid;
    int err;
    unsigned int info_count = TASK_BASIC_INFO_COUNT;
    mach_port_t task;
    struct task_basic_info tasks_info;
    thread_act_port_array_t thread_list;
    mach_msg_type_number_t thread_count;

    // the argument passed should be a process id
    if (! PyArg_ParseTuple(args, "l", &pid)) {
        return NULL;
    }

    /* task_for_pid() requires special privileges
     * "This function can be called only if the process is owned by the
     * procmod group or if the caller is root."
     * - http://developer.apple.com/documentation/MacOSX/Conceptual/universal_binary/universal_binary_tips/chapter_5_section_19.html
     */
    err = task_for_pid(mach_task_self(), pid, &task);
    if ( err == KERN_SUCCESS) {
        info_count = TASK_BASIC_INFO_COUNT;
        err = task_info(task, TASK_BASIC_INFO, (task_info_t)&tasks_info, &info_count);
        if (err != KERN_SUCCESS) {
                if (err == 4) { // errcode 4 is "invalid argument" (access denied)
                    return AccessDenied();
                }

                //otherwise throw a runtime error with appropriate error code
                return PyErr_Format(PyExc_RuntimeError, "task_info(TASK_BASIC_INFO) failed for pid %lu - %s (%i)",
                       pid, mach_error_string(err), err);
        }

        err = task_threads(task, &thread_list, &thread_count);
        if (err == KERN_SUCCESS) {
            return Py_BuildValue("l", (long)thread_count);
        }
    }


    else {
        if (! pid_exists(pid) ) {
            return NoSuchProcess();
        }

        // pid exists, so return AccessDenied error since task_for_pid() failed
        return AccessDenied();
    }
    return NULL;
}


/*
 * Return a Python integer indicating the total amount of physical memory
 * in bytes.
 */
static PyObject*
get_total_phymem(PyObject* self, PyObject* args)
{
    int mib[2];
    uint64_t total_phymem;
    size_t len;

    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    len = sizeof(total_phymem);

    if (sysctl(mib, 2, &total_phymem, &len, NULL, 0) == -1) {
        PyErr_SetFromErrno(0);
        return NULL;
    }

    return Py_BuildValue("L", total_phymem);
}


/*
 * Return a Python long indicating the amount of available physical memory in
 * bytes.
 */
static PyObject*
get_avail_phymem(PyObject* self, PyObject* args)
{
    vm_statistics_data_t vm_stat;
    mach_msg_type_number_t count;
    kern_return_t error;
    unsigned long long mem_free;
    int pagesize = getpagesize();
    mach_port_t mport = mach_host_self();

    count = sizeof(vm_stat) / sizeof(natural_t);
    error = host_statistics(mport, HOST_VM_INFO, (host_info_t)&vm_stat, &count);

    if (error != KERN_SUCCESS) {
        return PyErr_Format(PyExc_RuntimeError,
                    "Error in host_statistics(): %s", mach_error_string(error));
    }

    mem_free = (unsigned long long) vm_stat.free_count * pagesize;
    return Py_BuildValue("L", mem_free);
}


/*
 * Return a Python integer indicating the total amount of virtual memory
 * in bytes.
 */
static PyObject*
get_total_virtmem(PyObject* self, PyObject* args)
{
    int mib[2];
    size_t size;
    struct xsw_usage totals;

    mib[0] = CTL_VM;
    mib[1] = VM_SWAPUSAGE;
    size = sizeof(totals);

    if (sysctl(mib, 2, &totals, &size, NULL, 0) == -1) {
        PyErr_SetFromErrno(0);
        return NULL;
     }

    return Py_BuildValue("L", totals.xsu_total);
}

/*
 * Return a Python integer indicating the avail amount of virtual memory
 * in bytes.
 */
static PyObject*
get_avail_virtmem(PyObject* self, PyObject* args)
{
    int mib[2];
    size_t size;
    struct xsw_usage totals;

    mib[0] = CTL_VM;
    mib[1] = VM_SWAPUSAGE;
    size = sizeof(totals);

    if (sysctl(mib, 2, &totals, &size, NULL, 0) == -1) {
        PyErr_SetFromErrno(0);
        return NULL;
     }

    return Py_BuildValue("L", totals.xsu_avail);
}

/*
 * Return a Python tuple representing user, kernel and idle CPU times
 */
static PyObject*
get_system_cpu_times(PyObject* self, PyObject* args)
{
    mach_msg_type_number_t  count = HOST_CPU_LOAD_INFO_COUNT;
    kern_return_t error;
    host_cpu_load_info_data_t r_load;

    error = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&r_load, &count);
    if (error != KERN_SUCCESS) {
        return PyErr_Format(PyExc_RuntimeError,
                "Error in host_statistics(): %s", mach_error_string(error));
    }

    //user, nice, system, idle, iowait, irqm, softirq
    return Py_BuildValue("(dddd)",
                    (double)r_load.cpu_ticks[CPU_STATE_USER] / CLK_TCK,
                    (double)r_load.cpu_ticks[CPU_STATE_NICE] / CLK_TCK,
                    (double)r_load.cpu_ticks[CPU_STATE_SYSTEM] / CLK_TCK,
                    (double)r_load.cpu_ticks[CPU_STATE_IDLE] / CLK_TCK
            );
}

