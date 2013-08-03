#define _WIN32_WINNT 0x0400
#include "Threads.h"
#include "Photoshop.h"
#include <exception>
#include <windows.h>
#include <PIBufferSuite.h>
#include "PhotoshopHelpers.h"

// #define DEBUG

#if 0
static BufferProcs *pBufferSuite = NULL;
static bool g_bInitialized = false;
static int g_iTotal = 0;
static int g_iPeak = 0;
static bool bPrintedPeak = false;

struct MemoryRecord
{
	BufferID id;

#if defined(DEBUG)
	int iSize;
#endif
};

#if defined(DEBUG)
void AdjustMemory(MemoryRecord *pRec, bool bAlloc)
{
	EnterCriticalSection(&g_PSCritSec);
	int iFree = pBufferSuite->spaceProc();
	LeaveCriticalSection(&g_PSCritSec);

	if(bAlloc)
		g_iTotal += pRec->iSize;
	else
		g_iTotal -= pRec->iSize;
	if(g_iTotal > g_iPeak)
	{
		g_iPeak = g_iTotal;
		bPrintedPeak = true;
	}
	if(bAlloc && pRec->iSize >= 512)
	{
		if(!bPrintedPeak)
			printf("++ %p %i: %i (peak %i, free %i)\n", pRec, pRec->iSize, g_iTotal, g_iPeak, iFree);
		else
			printf("++ %p %i: %i (peak %i, free %i) ***\n", pRec, pRec->iSize, g_iTotal, g_iPeak, iFree);
		bPrintedPeak = false;
	}
	else if(!bAlloc && pRec->iSize <= -512)
		printf("-- %p %i: %i, free %i\n", pRec, pRec->iSize, g_iTotal, iFree);
}
#endif

void MemoryInit()
{
	if(g_bInitialized)
		return;
	g_bInitialized = true;
	pBufferSuite = NULL;

	SPBasicSuite *pBasicSuite = gFilterRecord->sSPBasic;
	if(pBasicSuite != NULL)
	{
		SPErr err = pBasicSuite->AcquireSuite(kPSBufferSuite, kCurrentBufferProcsVersion, (const void **) &pBufferSuite);
		if(err)
			pBufferSuite = NULL;
	}

	if(pBufferSuite == NULL)
		printf("kPSBufferSuite not available; falling back on malloc\n");
}

void MemoryFree()
{
	if(!g_bInitialized)
		return;
	g_bInitialized = false;

#if defined(DEBUG)
	printf("Memory in use at exit: %i\n", g_iTotal);
#endif

	if(gFilterRecord->sSPBasic != NULL)
	{
		gFilterRecord->sSPBasic->ReleaseSuite(kPSBufferSuite, kCurrentBufferProcsVersion);
		pBufferSuite = NULL;
	}
}

void *operator new(size_t i)
{
	/* We need to track the ID; add space in the buffer for it. */
	i += sizeof(MemoryRecord);

	BufferID id;
	void *p = NULL;
	if(pBufferSuite != NULL)
	{
		EnterCriticalSection(&g_PSCritSec);
		SPErr iErr = pBufferSuite->allocateProc(i, &id);
		LeaveCriticalSection(&g_PSCritSec);

		if(!iErr)
		{
			EnterCriticalSection(&g_PSCritSec);
			p = pBufferSuite->lockProc(id, 0);
			LeaveCriticalSection(&g_PSCritSec);
		}
	}
	else
	{
		p = malloc(i);
	}

	if(p == NULL)
	{
#if defined(DEBUG)
		printf("Allocation failure of %i bytes, current allocation %i, peak %i\n", i, g_iTotal, g_iPeak);
#endif
		throw(std::bad_alloc());
	}

	MemoryRecord *pRec = (MemoryRecord *) p;
	pRec->id = id;

#if defined(DEBUG)
	pRec->iSize = i;
	AdjustMemory(pRec, true);
#endif

	return pRec + 1;
}

void *operator new[](size_t i)
{
	i += sizeof(MemoryRecord);

	BufferID id;
	void *p = NULL;

	if(pBufferSuite != NULL)
	{
		EnterCriticalSection(&g_PSCritSec);
		SPErr iErr = pBufferSuite->allocateProc(i, &id);
		LeaveCriticalSection(&g_PSCritSec);

		if(!iErr)
		{
			EnterCriticalSection(&g_PSCritSec);
			p = pBufferSuite->lockProc(id, 0);
			LeaveCriticalSection(&g_PSCritSec);
		}
	}
	else
	{
		p = malloc(i);
	}

	if(p == NULL)
	{
#if defined(DEBUG)
		printf("Allocation failure of %i bytes, current allocation %i, peak %i\n", i, g_iTotal, g_iPeak);
#endif
		throw bad_alloc();
	}

	MemoryRecord *pRec = (MemoryRecord *) p;
	pRec->id = id;
#if defined(DEBUG)
	pRec->iSize = i;
	AdjustMemory(pRec, true);
#endif

	return pRec + 1;
}

void operator delete(void *p) throw()
{
	if(p == NULL)
		return;

	MemoryRecord *pRec = (MemoryRecord *) p;
	--pRec;

#if defined(DEBUG)
	AdjustMemory(pRec, false);
#endif

	EnterCriticalSection(&g_PSCritSec);
	if(pBufferSuite != NULL)
	{
		BufferID id = pRec->id;
		pBufferSuite->unlockProc(id);
		pBufferSuite->freeProc(id);
	}
	else
	{
		free(pRec);
	}
	LeaveCriticalSection(&g_PSCritSec);
}

void operator delete[] (void *p) throw()
{
	if(p == NULL)
		return;

	MemoryRecord *pRec = (MemoryRecord *) p;
	--pRec;

#if defined(DEBUG)
	AdjustMemory(pRec, false);
#endif

	EnterCriticalSection(&g_PSCritSec);
	if(pBufferSuite != NULL)
	{
		BufferID id = pRec->id;
		pBufferSuite->unlockProc(id);
		pBufferSuite->freeProc(id);
	}
	else
	{
		free(pRec);
	}
	LeaveCriticalSection(&g_PSCritSec);
}
#else
void MemoryInit()
{
}
void MemoryFree()
{
}
#endif
