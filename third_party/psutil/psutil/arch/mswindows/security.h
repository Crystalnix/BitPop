/*
 * $Id: security.h 435 2009-09-19 09:32:29Z jloden $
 *
 * Security related functions for Windows platform (Set privileges such as
 * SeDebug), as well as security helper functions.
 */

#include <windows.h>


BOOL SetPrivilege(HANDLE hToken, LPCTSTR Privilege, BOOL bEnablePrivilege);
int SetSeDebug();
int UnsetSeDebug();
HANDLE token_from_handle(HANDLE hProcess);
int HasSystemPrivilege(HANDLE hProcess);

