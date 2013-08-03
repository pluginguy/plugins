#ifndef CRASH_REPORTING_H
#define CRASH_REPORTING_H

#include <windows.h>

int GenerateDump(EXCEPTION_POINTERS *pExceptionPointers);

#endif
