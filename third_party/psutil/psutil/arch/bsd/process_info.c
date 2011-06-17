/*
 * $Id: process_info.c 772 2010-11-03 13:51:11Z g.rodola $
 *
 * Helper functions related to fetching process information. Used by _psutil_bsd
 * module methods.
 */

#include <Python.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>

#include "process_info.h"


/*
 * Returns a list of all BSD processes on the system.  This routine
 * allocates the list and puts it in *procList and a count of the
 * number of entries in *procCount.  You are responsible for freeing
 * this list (use "free" from System framework).
 * On success, the function returns 0.
 * On error, the function returns a BSD errno value.
 */
int
get_proc_list(struct kinfo_proc **procList, size_t *procCount)
{
    int err;
    struct kinfo_proc * result;
    int done;
    static const int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    // Declaring name as const requires us to cast it when passing it to
    // sysctl because the prototype doesn't include the const modifier.
    size_t              length;

    assert( procList != NULL);
    assert(*procList == NULL);
    assert(procCount != NULL);

    *procCount = 0;

    /*
     * We start by calling sysctl with result == NULL and length == 0.
     * That will succeed, and set length to the appropriate length.
     * We then allocate a buffer of that size and call sysctl again
     * with that buffer.  If that succeeds, we're done.  If that fails
     * with ENOMEM, we have to throw away our buffer and loop.  Note
     * that the loop causes use to call sysctl with NULL again; this
     * is necessary because the ENOMEM failure case sets length to
     * the amount of data returned, not the amount of data that
     * could have been returned.
     */
    result = NULL;
    done = 0;
    do {
        assert(result == NULL);
        // Call sysctl with a NULL buffer.
        length = 0;
        err = sysctl((int *)name, (sizeof(name) / sizeof(*name)) - 1,
                     NULL, &length, NULL, 0);
        if (err == -1)
            err = errno;

        // Allocate an appropriately sized buffer based on the results
        // from the previous call.
        if (err == 0) {
            result = malloc(length);
            if (result == NULL)
                err = ENOMEM;
        }

        // Call sysctl again with the new buffer.  If we get an ENOMEM
        // error, toss away our buffer and start again.
        if (err == 0) {
            err = sysctl((int *) name, (sizeof(name) / sizeof(*name)) - 1,
                          result, &length, NULL, 0);
            if (err == -1)
                err = errno;
            if (err == 0) {
                done = 1;
            }
            else if (err == ENOMEM) {
                assert(result != NULL);
                free(result);
                result = NULL;
                err = 0;
            }
        }
    } while (err == 0 && ! done);

    // Clean up and establish post conditions.
    if (err != 0 && result != NULL) {
        free(result);
        result = NULL;
    }

    *procList = result;
    *procCount = length / sizeof(struct kinfo_proc);

    assert((err == 0) == (*procList != NULL));
    return err;
}


char
*getcmdpath(long pid, size_t *pathsize)
{
    int  mib[4];
    char *path;
    size_t size = 0;

    /*
     * Make a sysctl() call to get the raw argument space of the process.
     */
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PATHNAME;
    mib[3] = pid;

    // call with a null buffer first to determine if we need a buffer
    if (sysctl(mib, 4, NULL, &size, NULL, 0) == -1) {
        return NULL;
    }

    path = malloc(size);
    if (path == NULL)
        return NULL;

    *pathsize = size;
    if (sysctl(mib, 4, path, &size, NULL, 0) == -1) {
        free(path);
        return NULL;       /* Insufficient privileges */
    }

    return path;
}



/*
 * Borrowed from psi Python System Information project
 *
 * Get command arguments and environment variables.
 *
 * Based on code from ps.
 *
 * Returns:
 *      0 for success;
 *      -1 for failure (Exception raised);
 *      1 for insufficient privileges.
 */
char
*getcmdargs(long pid, size_t *argsize)
{
    int mib[4];
    size_t size, argmax;
    char *procargs = NULL;

    /* Get the maximum process arguments size. */
    mib[0] = CTL_KERN;
    mib[1] = KERN_ARGMAX;

    size = sizeof(argmax);
    if (sysctl(mib, 2, &argmax, &size, NULL, 0) == -1)
        return NULL;

    /* Allocate space for the arguments. */
    procargs = (char *)malloc(argmax);
    if (procargs == NULL)
        return NULL;

    /*
     * Make a sysctl() call to get the raw argument space of the process.
     */
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ARGS;
    mib[3] = pid;

    size = argmax;
    if (sysctl(mib, 4, procargs, &size, NULL, 0) == -1) {
        free(procargs);
        return NULL;       /* Insufficient privileges */
    }

    // return string and set the length of arguments
    *argsize = size;
    return procargs;
}


/* returns the command line as a python list object */
PyObject*
get_arg_list(long pid)
{
    char *argstr = NULL;
    int pos = 0;
    size_t argsize = 0;
    PyObject *retlist = Py_BuildValue("[]");
    PyObject *item = NULL;

    if (pid < 0) {
        return retlist;
    }

    // XXX - this leaks memory (grrr)
    argstr = getcmdargs(pid, &argsize);

    if (NULL == argstr) {
        if (ESRCH == errno) {
            PyErr_Format(PyExc_RuntimeError,
                    "getcmdargs() failed - no process found with pid %lu", pid);
            return NULL;
        }

        // ignore other errors for now, since we don't want to bail on
        // get_process_info() if cmdline is the only thing we couldn't get.
        // In that case, we just return an empty list return
        // PyErr_Format(PyExc_RuntimeError, "getcmdargs() failed for pid %lu", pid);
        return retlist;
    }

    // args are returned as a flattened string with \0 separators between
    // arguments add each string to the list then step forward to the next
    // separator
    if (argsize > 0) {
        while(pos < argsize) {
            item = Py_BuildValue("s", &argstr[pos]);
            PyList_Append(retlist, item);
            Py_DECREF(item);
            pos = pos + strlen(&argstr[pos]) + 1;
        }
    }

    free(argstr);
    return retlist;
}

