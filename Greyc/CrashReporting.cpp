#define _WIN32_WINNT 0x0400 // for IsDebuggerPresent
#define STRSAFE_NO_DEPRECATE

#include "CrashReporting.h"
#include <stdio.h>
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include "PhotoshopKeys.h"

#pragma comment(lib, "dbghelp.lib")

static DWORD WINAPI CrashMessageBoxThread(void *arg)
{
	MessageBox(NULL, (char *) arg, plugInName, MB_OK);
	return 0;
}

void GenerateDump(EXCEPTION_POINTERS *pExceptionPointers)
{
	SYSTEMTIME stLocalTime;
	GetLocalTime(&stLocalTime);

	char szPath[MAX_PATH]; 
	GetTempPath(sizeof(szPath), szPath);

	char *szAppName = plugInName;
	StringCchCat(szPath, sizeof(szPath), szAppName);
	CreateDirectory(szPath, NULL);

	char szFileName[MAX_PATH]; 
	StringCchPrintf(szFileName, MAX_PATH, "%04i%02i%02i-%02i%02i%02i.dmp", 
		stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay, 
		stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond);

	char szFullFileName[MAX_PATH]; 
	StringCchPrintf(szFullFileName, MAX_PATH, "%s\\%s", szPath, szFileName);

	HANDLE hDumpFile = CreateFile(szFullFileName, GENERIC_READ|GENERIC_WRITE, 
		FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	MINIDUMP_EXCEPTION_INFORMATION ExpParam;
	ExpParam.ThreadId = GetCurrentThreadId();
	ExpParam.ExceptionPointers = pExceptionPointers;
	ExpParam.ClientPointers = FALSE;

	BOOL bMiniDumpSuccessful = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), 
		hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);

	CloseHandle(hDumpFile);

	char szMessage[1024*32];
	StringCchPrintf(szMessage, sizeof(szMessage),
		plugInName " has crashed.  A crash dump has been created:\n\n"
		"%s\n\n"
		"The application will exit.", szFileName);

	/* If we show a MessageBox in this thread, then it'll run the message pump for
	 * any other dialogs that might still be alive, which might do bad things at this
	 * point.  Create a temporary thread, and show the message box from there. */
	unsigned long iThreadID = 0;
	HANDLE hThreadHandle = CreateThread(0, 0, CrashMessageBoxThread, szMessage, 0, &iThreadID);
	WaitForSingleObject(hThreadHandle, INFINITE);

	/* This is too dangerous to call from an exception handler. */
//	ShellExecute(NULL, "open", szPath, NULL, NULL, SW_SHOWDEFAULT);
}

long __stdcall ExceptionHandler(EXCEPTION_POINTERS *pExc)
{
	if(IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

        __try
        {
		GenerateDump(pExc);
        }
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

static LPTOP_LEVEL_EXCEPTION_FILTER g_pOldExceptionHandler = NULL;

void CrashHandlerInit()
{
	g_pOldExceptionHandler = SetUnhandledExceptionFilter(ExceptionHandler);
}

void CrashHandlerRemove()
{
	SetUnhandledExceptionFilter(g_pOldExceptionHandler);
	g_pOldExceptionHandler = NULL;
}
