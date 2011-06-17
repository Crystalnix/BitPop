/*
 * $Id: process_info.h 779 2010-11-08 20:34:16Z g.rodola $
 *
 * Helper functions related to fetching process information. Used by _psutil_mswindows
 * module methods.
 */

#include <Python.h>
#include <windows.h>

PyObject * NoSuchProcess(void);
PyObject * AccessDenied(void);
HANDLE handle_from_pid_waccess(DWORD pid, DWORD dwDesiredAccess);
HANDLE handle_from_pid(DWORD pid);
PVOID GetPebAddress(HANDLE ProcessHandle);
HANDLE handle_from_pid(DWORD pid);
BOOL is_running(HANDLE hProcess);
int pid_in_proclist(DWORD pid);
int pid_is_running(DWORD pid);
int is_system_proc(DWORD pid);
PyObject* get_arg_list(long pid);
PyObject* get_ppid(long pid);
PyObject* get_name(long pid);
DWORD* get_pids(DWORD *numberOfReturnedPIDs);
