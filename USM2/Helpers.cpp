#include "Helpers.h"

#pragma comment(lib, "winmm.lib") // for timeGetTime

#define MS_VC_EXCEPTION 0x406d1388
typedef struct tagTHREADNAME_INFO
{
        DWORD dwType;        // must be 0x1000
        LPCSTR szName;       // pointer to name (in same addr space)
        DWORD dwThreadID;    // thread ID (-1 caller thread)
        DWORD dwFlags;       // reserved for future use, most be zero
} THREADNAME_INFO;

void SetThreadName(int iThreadNo)
{
	char szName[1024];
	sprintf(szName, "thread %i", iThreadNo);

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
