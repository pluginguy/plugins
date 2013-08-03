#define _WIN32_WINNT 0x0400
#include "Helpers.h"
#include <assert.h>

#pragma comment(lib, "winmm.lib") // for timeGetTime

#define MS_VC_EXCEPTION 0x406d1388
typedef struct tagTHREADNAME_INFO
{
        DWORD dwType;        // must be 0x1000
        LPCSTR szName;       // pointer to name (in same addr space)
        DWORD dwThreadID;    // thread ID (-1 caller thread)
        DWORD dwFlags;       // reserved for future use, most be zero
} THREADNAME_INFO;

void SetThreadName(const char *szName)
{
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = szName;
	info.dwThreadID = GetCurrentThreadId();
	info.dwFlags = 0;

	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR *)&info);
	} __except (EXCEPTION_CONTINUE_EXECUTION) {
	}
}

HINSTANCE g_hInstance = NULL;

/* Return the directory of the running module, with a trailing backslash. */
string GetModuleDirectory()
{
	char sModulePath[MAX_PATH];

	GetModuleFileName(g_hInstance, sModulePath, sizeof(sModulePath));
	char *pSep = strrchr(sModulePath, '\\');
	assert(pSep != NULL);
	++pSep;
	*pSep = 0;
	return sModulePath;
}

bool ThisProcessHasFocus()
{
	HWND hForegroundWindow = GetForegroundWindow();
	if(hForegroundWindow == NULL)
		return false;

	DWORD iCurrentProcess = GetCurrentProcessId();
	DWORD iFocusedProcess;
	GetWindowThreadProcessId(hForegroundWindow, &iFocusedProcess);

	return iCurrentProcess == iFocusedProcess;
}

void SetWindowStyle(HWND hWnd, int iMask)
{
	int iStyle = GetWindowLong(hWnd, GWL_STYLE);
	iStyle |= iMask;
	SetWindowLong(hWnd, GWL_STYLE, iStyle);
}

void ClearWindowStyle(HWND hWnd, int iMask)
{
	int iStyle = GetWindowLong(hWnd, GWL_STYLE);
	iStyle &= ~iMask;
	SetWindowLong(hWnd, GWL_STYLE, iStyle);
}

void SetClipboardFromString(HWND hWnd, string s)
{
	if (!OpenClipboard(hWnd))
		return;

	EmptyClipboard();

        HGLOBAL hBuf = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
        if(hBuf == NULL)
        {
		CloseClipboard();
		return;
        }

        char *pBuf = (char *) GlobalLock(hBuf);
        memcpy(pBuf, s.c_str(), s.size() + 1);
        GlobalUnlock(hBuf);

        SetClipboardData(CF_TEXT, hBuf);

	CloseClipboard();
}

bool ThisProcessHasAConsole()
{
	HANDLE hConsole = CreateFile("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hConsole == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(hConsole);
	return true;
}

double gettime()
{
	static bool bInitted = false;
	if(!bInitted)
	{
		timeBeginPeriod(1);
		bInitted = true;
	}
	return timeGetTime() / 1000.0;
}

Instance::Instance()
{
	char szFullAppPath[MAX_PATH];
	GetModuleFileName(NULL, szFullAppPath, MAX_PATH);
	m_hAppInstance = LoadLibrary(szFullAppPath);
}

Instance::~Instance()
{
	if(m_hAppInstance)
		FreeLibrary(m_hAppInstance);
}

ReadNotifier::ReadNotifier(HANDLE hFileHandle, ReadNotifier::Callback *pCB):
	m_pCallback(pCB)
{
	m_hFileHandle = hFileHandle;
	m_Action = ACTION_NONE;
	m_hWakeUpEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_hWakeUpFinishedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	memset(&m_OverlappedRead, 0, sizeof(m_OverlappedRead));
	m_bOverlappedReadActive = false;
	m_bDataIsWaiting = false;
	m_pReadRequestBuf = NULL;
	m_OverlappedRead.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	unsigned long iThreadID = 0;
	m_hThreadHandle = CreateThread(0, 0, RunThreadStart, this, 0, &iThreadID);
	assert(m_hThreadHandle != INVALID_HANDLE_VALUE);
}

ReadNotifier::~ReadNotifier()
{
	/* Shut down the thread. */
	RunAction(ACTION_SHUTDOWN);

	WaitForSingleObject(m_hThreadHandle, INFINITE);
	CloseHandle(m_hThreadHandle);

	CloseHandle(m_OverlappedRead.hEvent);
	CloseHandle(m_hWakeUpEvent);
	CloseHandle(m_hWakeUpFinishedEvent);
}

DWORD ReadNotifier::RunThreadStart(void *p)
{
	((ReadNotifier *) p)->RunThread();
	ExitThread(0);
}

void ReadNotifier::StartPassiveRead()
{
	if(m_bOverlappedReadActive)
		return;

	/* Start a new overlapped read.  We request 0 bytes, which means the event will
	 * be signalled when data is written to the pipe, but nothing will be read.  Point
	 * it at a valid but dummy pointer, since it expects to have a pointer even though
	 * we're not reading any data. */
	static char unused;
	if(!ReadFile(m_hFileHandle, &unused, 0, NULL, &m_OverlappedRead))
	{
		if(GetLastError() == ERROR_IO_PENDING)
		{
			m_bOverlappedReadActive = true;
			return;
		}

		printf("ThreadBeginOverlapped error %i %s\n", GetLastError(), StringUtil::GetWindowsError(GetLastError()).c_str());
		return;
	}
	else
	{
		/* There's already data waiting, and our ReadFile finished immediately. */
		m_bDataIsWaiting = true;
		ResetEvent(m_OverlappedRead.hEvent);
	}
}

void ReadNotifier::FinishPassiveRead()
{
	/* Finish the overlapped request. */
	DWORD iDataRead;
	if(!GetOverlappedResult(m_hFileHandle, &m_OverlappedRead, &iDataRead, FALSE))
	{
		/* On error, allow a notification to be sent, so we'll trigger a
		 * read on the handle. */
		printf("GetOverlappedResult error %i %s", GetLastError(), StringUtil::GetWindowsError(GetLastError()).c_str());
	}

	/* The caller must make another call to Start, or make a read request, before we'll call the callback again. */
	m_bOverlappedReadActive = false;
	ResetEvent(m_OverlappedRead.hEvent);
}

void ReadNotifier::RunThread()
{
	SetThreadName("ReadNotifier");

	bool bShutdown = false;
	while(!bShutdown)
	{
		if(m_bDataIsWaiting)
		{
			m_bDataIsWaiting = false;

			/* Call the notification callback.  This must not call Start(); it'll deadlock. */
			m_pCallback->ReadNotification(this);
			continue;
		}

		HANDLE aHandles[2] = { m_hWakeUpEvent, m_OverlappedRead.hEvent };
		DWORD iRet = WaitForMultipleObjects(2, aHandles, FALSE, INFINITE);

		if(iRet == WAIT_OBJECT_0 + 1)
		{
			/* m_OverlappedRead.hEvent has been signalled. */
			// printf("Passive read incoming\n");
			FinishPassiveRead();
			m_bDataIsWaiting = true;
			continue;
		}
		
		if(iRet == WAIT_FAILED)
		{
			/* We shouldn't hit errors waiting for events. */
			printf("WaitForMultipleObjects error: %i %s", GetLastError(), StringUtil::GetWindowsError(GetLastError()).c_str());
			abort();
		}
		assert(iRet == WAIT_OBJECT_0 + 0);

		if(m_Action == ACTION_START)
		{
			// printf("Start new passive read\n");
			StartPassiveRead();
		}

		if(m_Action == ACTION_READ)
		{
			ThreadHandleRead();
		}

		if(m_Action == ACTION_SHUTDOWN)
			bShutdown = true;

		m_Action = ACTION_NONE;

		SetEvent(m_hWakeUpFinishedEvent);
	}

	if(m_bOverlappedReadActive)
	{
		CancelIo(m_hFileHandle);

		/* Wait for the cancel to complete. */
		DWORD iRet = WaitForSingleObject(m_OverlappedRead.hEvent, INFINITE);

		/* We shouldn't hit errors waiting for events. */
		assert(iRet == WAIT_OBJECT_0);

		FinishPassiveRead();
	}
}

void ReadNotifier::ThreadHandleRead()
{
	/* If we were already running a 0-byte read to watch for data, wait for it
	 * to end.  Note that the timeout here is wrong; we use the timeout period
	 * twice, so we can wait twice as long as requested. */
	if(m_bOverlappedReadActive)
	{
		/* We want to read an actual block of data, but we already have a passive read (0 bytes)
		 * running.  We need to finish that before we can start the actual read.  Don't use
		 * CancelIO; it's slow, and there's no need: we're waiting for data anyway, so just wait
		 * for the read to finish. */
		DWORD iReadWait = WaitForSingleObject(m_OverlappedRead.hEvent, m_iReadRequestTimeout);
		if(iReadWait == WAIT_TIMEOUT)
		{
			/* We didn't get any data. */
			m_iReadRequestSize = 0;
			m_iReadRequestResult = WAIT_TIMEOUT;
			return;
		}

		FinishPassiveRead();
	}

	DWORD iBytesRead;
	if(ReadFile(m_hFileHandle, m_pReadRequestBuf, m_iReadRequestSize, &iBytesRead, &m_OverlappedRead))
	{
		/* The read returned immediately. */
		m_iReadRequestSize = iBytesRead;
		m_iReadRequestResult = 0;
		ResetEvent(m_OverlappedRead.hEvent);
		return;
	}

	if(GetLastError() != ERROR_IO_PENDING)
	{
		m_iReadRequestSize = 0;
		m_iReadRequestResult = GetLastError();
		return;
	}

	/* The read is running async.  Wait for it to finish. */
	DWORD iReadWait = WaitForSingleObject(m_OverlappedRead.hEvent, m_iReadRequestTimeout);
	if(iReadWait == WAIT_TIMEOUT)
	{
		/* The wait timed out.  Cancel it. */
		CancelIo(m_hFileHandle);
	}

	if(!GetOverlappedResult(m_hFileHandle, &m_OverlappedRead, &iBytesRead, TRUE))
	{
		if(GetLastError() == ERROR_OPERATION_ABORTED)
		{
			/* We timed out above. */
			SetLastError(WAIT_TIMEOUT);
		}

		m_iReadRequestSize = 0;
		m_iReadRequestResult = GetLastError();
	}
	else
	{
		m_iReadRequestSize = iBytesRead;
		m_iReadRequestResult = 0;
	}
	ResetEvent(m_OverlappedRead.hEvent);
}

void ReadNotifier::RunAction(ReadNotifier::Action a)
{
	assert(m_Action == ACTION_NONE);

	m_Action = a;
	SetEvent(m_hWakeUpEvent);

	/* Wait for the thread to finish. */
	WaitForSingleObject(m_hWakeUpFinishedEvent, INFINITE);
	assert(m_Action == ACTION_NONE);
}

void ReadNotifier::Start()
{
	RunAction(ACTION_START);
}

int ReadNotifier::Read(void *pBuf, int iBytes, int iTimeoutMs)
{
	if(iBytes == 0)
	{
		/* Don't run the callback for 0 bytes; ReadFile will block when it shouldn't. */
		SetLastError(ERROR_SUCCESS);
		return 0;
	}

	assert(m_pReadRequestBuf == NULL);

	m_pReadRequestBuf = pBuf;
	m_iReadRequestSize = iBytes;
	m_iReadRequestTimeout = iTimeoutMs;

	RunAction(ACTION_READ);

	m_pReadRequestBuf = NULL;

	SetLastError(m_iReadRequestResult);
	if(m_iReadRequestResult)
		return -1;
	else
		return m_iReadRequestSize;
}





void Slices::Init(int iDimY)
{
	m_iDimY = iDimY;
	Reset();
}

bool Slices::Get(int &iStartY)
{
	iStartY = InterlockedExchangeAdd(&m_iNextY, 1);
	return iStartY < m_iDimY;
}

void Slices::Reset()
{
	InterlockedExchange(&m_iNextY, 0);
}

struct MessageBoxData
{
	HWND hWnd;
	LPCSTR lpText;
	LPCSTR lpCaption;
	UINT uType;
};

static DWORD WINAPI ErrorMessageBoxThread(void *arg)
{
	MessageBoxData *pData = (MessageBoxData *) arg;
	int iResult = MessageBox(pData->hWnd, pData->lpText, pData->lpCaption, pData->uType);
	return iResult;
}

/* Run MessageBox in another thread, so the message pump for this thread isn't run. */
DWORD MessageBoxInThread(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
	MessageBoxData mbd;
	mbd.hWnd = hWnd;
	mbd.lpText = lpText;
	mbd.lpCaption = lpCaption;
	mbd.uType = uType;

	unsigned long iThreadID = 0;
	HANDLE hThreadHandle = CreateThread(0, 0, ErrorMessageBoxThread, &mbd, 0, &iThreadID);
	WaitForSingleObject(hThreadHandle, INFINITE);

	DWORD iResult = 0;
	GetExitCodeThread(hThreadHandle, &iResult);
	CloseHandle(hThreadHandle);
	return iResult;
}

int IOWithTimeout(HANDLE hFile, void *pBuf, int iSize, HANDLE hEvent, int iTimeoutMs, bool bRead)
{
	OVERLAPPED ov;
	memset(&ov, 0, sizeof(ov));
	ov.hEvent = hEvent;

	if(WaitForSingleObject(ov.hEvent, 0) == WAIT_OBJECT_0)
	{
		fprintf(stderr, "Event should not be signalled\n");
		abort();
	}

	DWORD iGot = -1;
	bool bOperationCompletedImmediately;
	
	if(bRead)
		bOperationCompletedImmediately = ReadFile(hFile, pBuf, iSize, &iGot, &ov) != 0;
	else
		bOperationCompletedImmediately = WriteFile(hFile, pBuf, iSize, &iGot, &ov) != 0;

	if(bOperationCompletedImmediately)
	{
		/* The documentation says that if ReadFile returns true here, then it completed as if
		 * it's a synchronous request.  This is incorrect: the event is also set, and we need
		 * to manually reset it. */
		ResetEvent(hEvent);
		return iGot;
	}

	/* We expect ReadFile to return ERROR_IO_PENDING.  Anything else is a real error. */
	if(GetLastError() != ERROR_IO_PENDING)
		return -1;

	if(WaitForSingleObject(ov.hEvent, iTimeoutMs) == WAIT_TIMEOUT)
	{
		/* We've timed out, so request a cancel.  The operation might still complete normally
		 * and we still need to GetOverlappedResult either way, so just wait for the completion
		 * (which shouldn't time out now), then continue below. */
		CancelIo(hFile);
	}

	if(!GetOverlappedResult(hFile, &ov, &iGot, TRUE))
	{
		if(GetLastError() == ERROR_OPERATION_ABORTED)
			SetLastError(WAIT_TIMEOUT);

		ResetEvent(hEvent);
		return -1;
	}
	ResetEvent(hEvent);
	return iGot;
}

int ReadWithTimeout(HANDLE hFile, void *pBuf, int iSize, HANDLE hEvent, int iTimeoutMs)
{
	return IOWithTimeout(hFile, pBuf, iSize, hEvent, iTimeoutMs, true);
}

int WriteWithTimeout(HANDLE hFile, const void *pBuf, int iSize, HANDLE hEvent, int iTimeoutMs)
{
	return IOWithTimeout(hFile, (void *) pBuf, iSize, hEvent, iTimeoutMs, false);
}

static DWORD WINAPI DebugBreakThread(void *p)
{
	int iMilliseconds = (int) p;
	Sleep(iMilliseconds);
	if(IsDebuggerPresent())
		DebugBreak();
	return 0;
}

void QueueDebugBreak(int iMilliseconds)
{
	unsigned long iThreadID = 0;
	HANDLE hHandle = CreateThread(0, 0, DebugBreakThread, (void *) iMilliseconds, 0, &iThreadID);
	assert(hHandle != INVALID_HANDLE_VALUE);
	CloseHandle(hHandle);
	return;
}

#if !defined(_WIN64)
int GetCPUID()
{
	static bool bInitialized = false;
	static unsigned iFeature = 0;
	if(bInitialized)
		return iFeature;

	__try
	{
		_asm
		{
			push ebx
			push ecx
			push edx

			mov eax, 1
			cpuid
			mov iFeature, edx

			pop ebx
			pop ecx
			pop edx
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}

	bInitialized = true;
	return iFeature;
}
#endif

void ScaleArea(float f, int &iX, int &iY, int &iWidth, int &iHeight)
{
	if(f == 0)
		return;
	int iScaledWidth = int(iWidth * f);
	int iScaledHeight = int(iHeight * f);
	int iAddedWidth = iScaledWidth - iWidth;
	int iAddedHeight = iScaledHeight - iHeight;

	iX -= iAddedWidth/2;
	iY -= iAddedHeight/2;
	iWidth += iAddedWidth;
	iHeight += iAddedHeight;
}

/* Convert the first channel of the output to monochrome output, ignoring the
 * rest of the input. */
void Blit8bY_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	bool bWithAlpha,
	int iInStride, int iOutStride)
{
	if(iInChannels)
		bWithAlpha=false;
	for(int y = 0; y < iHeight; ++y)
	{
		const uint8_t *pIn = pInBuf + y * iInStride;
		uint8_t *pOut = pOutBuf + y * iOutStride;
		for(int x = 0; x < iWidth; ++x)
		{
			pOut[0] = pIn[0];
			pOut[1] = pIn[0];
			pOut[2] = pIn[0];
			if(bWithAlpha)
				pOut[3] = pIn[1];
			else
				pOut[3] = 255;

			pIn += iInChannels;
			pOut += 4;
		}
	}
}

void Blit8bRGB_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	int iInStride, int iOutStride)
{
	for(int y = 0; y < iHeight; ++y)
	{
		const uint8_t *pIn = pInBuf + y * iInStride;
		uint8_t *pOut = pOutBuf + y * iOutStride;
		for(int x = 0; x < iWidth; ++x)
		{
			pOut[0] = pIn[2];
			pOut[1] = pIn[1];
			pOut[2] = pIn[0];
			pOut[3] = 255;

			pIn += iInChannels;
			pOut += 4;
		}
	}
}

void Blit8bRGBA_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	int iInStride, int iOutStride)
{
	for(int y = 0; y < iHeight; ++y)
	{
		const uint8_t *pIn = pInBuf + y * iInStride;
		uint8_t *pOut = pOutBuf + y * iOutStride;
		for(int x = 0; x < iWidth; ++x)
		{
			pOut[0] = pIn[2];
			pOut[1] = pIn[1];
			pOut[2] = pIn[0];
			pOut[3] = pIn[3];

			pIn += iInChannels;
			pOut += 4;
		}
	}
}

void Blit16b_8b(const uint16_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iChannels,
	int iInStride, int iOutStride)
{
	iInStride /= 2;
	for(int y = 0; y < iHeight; ++y)
	{
		const uint16_t *pIn = pInBuf + y * iInStride;
		uint8_t *pOut = pOutBuf + y * iOutStride;
		for(int x = 0; x < iWidth; ++x)
		{
			for(int i = 0; i < iChannels; ++i)
			{
				uint16_t iVal = *(pIn++);
				*(pOut++) = uint8_t(iVal * 10 / 1285);
			}
		}
	}
}
