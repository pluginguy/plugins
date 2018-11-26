#ifndef HELPERS_H
#define HELPERS_H

#include "StringUtil.h"

#include <exception>
#include <string>
#include <memory>
#include <windows.h>
#include <intrin.h>
using namespace std;

extern HINSTANCE g_hInstance;

#pragma intrinsic (_InterlockedIncrement)
#define InterlockedIncrement _InterlockedIncrement

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

template<typename T>
inline T clamp(T i, T low, T high)
{
	return max(low, min(i, high));
}

class Exception: public exception
{
public:
        Exception(string sMessage) throw() { m_sMessage = sMessage; }
        virtual ~Exception() throw() { }
        virtual Exception *Copy() const { return new Exception(*this); }
        const char *what() const throw() { return m_sMessage.c_str(); }
        string GetMessage() const { return m_sMessage; }

private:
        string m_sMessage;
};

class Instance
{
public:
	Instance();
	~Instance();

	HINSTANCE Get() const { return m_hAppInstance; }

private:
	HINSTANCE m_hAppInstance;
};

/*
 * Implement read timeouts and an asynchronous data-waiting callback.
 *
 * This is tricky: we can't wait on pipes directly, and the only way to wait until data is
 * available on a pipe like unix select() is to start a 0-byte overlapped read.  However, once
 * we do that we can't stop it; CancelIo is slow, often blocking for 10-15ms before returning.
 * We handle this by doing all reads in the thread that waits for the overlapped read, so we
 * never have to cancel during normal usage.
 */
class ReadNotifier
{
public:
	class Callback
	{
	public:
		/* The handle is readable.  Once called, this will not be called again until
		 * Start() is called again.  Start() and Read() may not be called from the
		 * callback. */
		virtual void ReadNotification(ReadNotifier *pNotifier) = 0;
	};

	/* pCB will be freed.  hHandle will not be closed. */
	ReadNotifier(HANDLE hHandle, Callback *pCB);
	~ReadNotifier();

	/* Begin waiting for data on hHandle.  If already waiting, do nothing. */
	void Start();

	/* Read data from the handle.  Data received in this way will not trigger callbacks. */
	int Read(void *pBuf, int iBytes, int iTimeoutMs);

private:
	static DWORD WINAPI RunThreadStart(void *p);
	void RunThread();
	void StartPassiveRead();
	void FinishPassiveRead();
	void ThreadHandleRead();

	OVERLAPPED m_OverlappedRead;
	bool m_bOverlappedReadActive;	/* true if we have a 0-byte overlapped read active */
	bool m_bDataIsWaiting;		/* true if the user callback needs to be called */
	HANDLE m_hFileHandle;		/* handle of object we're listening on */
	HANDLE m_hThreadHandle;		/* handle of our worker thread */
	HANDLE m_hWakeUpEvent;		/* wake up the worker thread now */
	HANDLE m_hWakeUpFinishedEvent;	/* wake up the caller after finishing an action */

	enum Action
	{
		ACTION_SHUTDOWN,
		ACTION_START,
		ACTION_READ,
		ACTION_NONE
	};
	Action m_Action;
	void RunAction(Action a);

	auto_ptr<Callback> m_pCallback;

	/* State for ACTION_READ: */
	void *m_pReadRequestBuf;
	int m_iReadRequestSize;
	int m_iReadRequestTimeout;
	int m_iReadRequestResult;
};


/*
 * To divide work across threads, we split the image into rows.  Threads call GetSlice to be
 * assigned a row to process.
 *
 * We don't simply split the image into one large slice per thread, because if one thread
 * is working more slowly than the rest (eg. due to CPU contention with some other process,
 * or due to some parts of the image being more expensive to handle), then it'll bottleneck
 * the whole process.  For example, if threads 1, 2 and 3 take 10 seconds to complete their
 * work, and thread 4 takes 20 seconds, then the rest of the threads will be idle for 10
 * seconds, instead of splitting the work more evenly.
 *
 * Returns true if iStartY is set.  If there is no more work to do, returns false.
 */

class Slices
{
public:
	void Init(int iDimY);
	bool Get(int &iStartY);
	void Reset();

private:
	int m_iDimY;
	volatile LONG m_iNextY;
};

void SetThreadName(const char *szName);
string GetModuleDirectory();
bool ThisProcessHasFocus();
void SetWindowStyle(HWND hWnd, int iMask);
void ClearWindowStyle(HWND hWnd, int iMask);
void SetClipboardFromString(HWND hWnd, string s);
bool ThisProcessHasAConsole();
double gettime();
DWORD MessageBoxInThread(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);

int IOWithTimeout(HANDLE hFile, void *pBuf, int iSize, HANDLE hEvent, int iTimeoutMs, bool bRead);
int ReadWithTimeout(HANDLE hFile, void *pBuf, int iSize, HANDLE hEvent, int iTimeoutMs);
int WriteWithTimeout(HANDLE hFile, const void *pBuf, int iSize, HANDLE hEvent, int iTimeoutMs);

void QueueDebugBreak(int iMilliseconds);

#if !defined(_WIN64)
#define CPUID_MMX	0x00800000
#define CPUID_SSE	0x02000000
#define CPUID_SSE2	0x04000000
int GetCPUID();
#endif

#if defined(_WIN64)
inline long int lrintf(float f)
{
        return f >= 0.0f ? (int)floorf(f + 0.5f) : (int)ceilf(f - 0.5f);
}
#else
inline long int lrintf(float f)
{
	int retval;
	_asm fld f;
	_asm fistp retval;
	return retval;
}
#endif

template<typename T>
T align(T val, unsigned n)
{
	unsigned i = (unsigned) val;
	i += n - 1;
	i -= i % n;
	return (T) i;
}

static inline float scale(float x, float l1, float h1, float l2, float h2)
{
	return (x - l1) * (h2 - l2) / (h1 - l1) + l2;
}

#define progress { if(pProgress) InterlockedIncrement(pProgress); }
#define check_cancel { if (*pStopRequest) { printf("cancelled\n"); return; } }
#define progress_and_check_cancel { check_cancel; progress; }

#define M_PI 3.1415926535897932384

void ScaleArea(float f, int &iX, int &iY, int &iWidth, int &iHeight);

void Blit8bY_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	bool bWithAlpha,
	int iInStride, int iOutStride);
void Blit8bRGB_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	int iInStride, int iOutStride);
void Blit8bRGBA_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	int iInStride, int iOutStride);
void Blit16b_8b(const uint16_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iChannels,
	int iInStride, int iOutStride);

#endif
