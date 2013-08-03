/* Implement the high-level interface for the algorithm. */

#define NOMINMAX
#include "Algorithm.h"
#include "Helpers.h"
#include "StringUtil.h"
#include "Unsharp.h"
#include <vector>
#include <algorithm>
using namespace std;
#include <assert.h>
#include <math.h>

#pragma warning (disable : 4244) // 'initializing' : conversion from 'int' to 'const float', possible loss of data
#pragma warning (disable : 4101) // unreferenced local variable

#include <windows.h>


Algorithm::Settings::Settings()
{
	/* The parameters for the algorithm are for an image with color components in the
	 * typical [0,255] range.  If the input scale is different, m_fInputScale is the
	 * factor to bring the data to that range.  (In practice, all this does is scale
	 * gfact.) */
	m_fInputScale = 1.0f;


	fRadius = 5.0f;
	fAmountUp = 0.5f;
	fAmountDown = 0;
	fThreshold = 0;
	fGamma = 1.8f;
	fHigh = 1.0f;
	fLight = 1.0f;
	fMidtone = 1.0f;
	fShadow = 1.0f;
}

string Algorithm::Settings::GetAsString() const
{
	const Algorithm::Settings DefaultSettings;
	string sBuf;
#define TO_STR(name, fmt, digits) \
	if(name != DefaultSettings.name) { \
		if(!sBuf.empty()) sBuf += " "; \
		sBuf += StringUtil::ssprintf(fmt, name) + " " + StringUtil::FormatFloat(name, digits); \
	}

	// XXX
	TO_STR(fRadius, "-dt", 3);
	TO_STR(fAmountUp, "-p", 3);
	TO_STR(fGamma, "-a", 3);
	TO_STR(fAmountDown, "-alpha", 3);
	TO_STR(fThreshold, "-sigma", 3);
	TO_STR(fHigh, "-gauss", 3);
	TO_STR(fLight, "-iter", 3);
	TO_STR(fMidtone, "-fact", 3);
	TO_STR(fShadow, "-dl", 3);
	return sBuf;
}

Algorithm::Options::Options()
{
	nb_threads = 0;
	m_DisplayMode = DISPLAY_SINGLE;
}

Algorithm::Algorithm():
	m_iProgressCounter(0),
	m_bStartRequest(false),
	m_bStopRequest(false),
	m_Signal(&m_ProcessingMutex)
{
	m_iThreadsRunning = 0;
	m_hMainThreadHandle = NULL;

	/* We create the first thread once and leave it running, since OpenGL contexts are
	 * associated with the thread and if we recreate it every time it adds about 100ms
	 * to runtime, which is substantial for preview images. */
	m_bExitingPrimaryThread = false;

	CreateMainWorkerThread();
}

Algorithm::~Algorithm()
{
	Abort();
	Finish();
	DestroyMainWorkerThread();
}

float Algorithm::GetRequiredOverlapFactor()
{
       return 1.0f;
}

void Algorithm::SetTarget(const CImg &image)
{
	m_SourceImage.Hold(image);
}

void Algorithm::SetMask(const CImg &mask)
{
	m_Mask.Hold(mask);
}

/* Get the number of channels to process.  If OpenGL is enabled, always process four channels.
 * If more SSE optimizations are implemented, this should force four channels. */
int Algorithm::GetProcessedChannels() const
{
	return max(4, m_SourceImage.m_iChannels);
}

void Algorithm::Finish()
{
	m_iProgressCounter = 0;
	m_bStopRequest = false;
	m_Dest.free();

	for(size_t i = 0; i < m_ahWorkerThreadHandles.size(); ++i)
	{
		WaitForSingleObject(m_ahWorkerThreadHandles[i], INFINITE);
		CloseHandle(m_ahWorkerThreadHandles[i]);
	}
	m_ahWorkerThreadHandles.clear();
}

struct thread_start_t
{
	Algorithm *pThis;
	int iThreadNo;
};

void Algorithm::CreateMainWorkerThread()
{
	thread_start_t *pStart = new thread_start_t;
	pStart->iThreadNo = 0;
	pStart->pThis = this;

	unsigned long iThreadID = 0;
	m_hMainThreadHandle = CreateThread(0, 0, algorithm_primary_thread, pStart, 0, &iThreadID);
}

void Algorithm::DestroyMainWorkerThread()
{
	m_ProcessingMutex.Lock();
	m_bExitingPrimaryThread = true;
	m_Signal.Broadcast();
	m_ProcessingMutex.Unlock();

	WaitForSingleObject(m_hMainThreadHandle, INFINITE);
	CloseHandle(m_hMainThreadHandle);
	m_hMainThreadHandle = NULL;
	m_bExitingPrimaryThread = false;
}

void Algorithm::WakePrimaryWorkerThread()
{
	m_ProcessingMutex.Lock();
	m_bStartRequest = true;
	m_Signal.Broadcast();
	m_ProcessingMutex.Unlock();
}

void Algorithm::CreateWorkerThread(int iThreadNo)
{
	thread_start_t *pStart = new thread_start_t;
	pStart->iThreadNo = iThreadNo;
	pStart->pThis = this;

	unsigned long iThreadID = 0;
	HANDLE hHandle = CreateThread(0, 0, algorithm_thread, pStart, 0, &iThreadID);
	m_ahWorkerThreadHandles.push_back(hHandle);
}

void Algorithm::thread_main_primary()
{
	while(1)
	{
		m_ProcessingMutex.Lock();
		while(!m_bStartRequest && !m_bExitingPrimaryThread)
			m_Signal.Wait();
		bool bStart = m_bStartRequest;
		m_bStartRequest = false;
		bool bExit = m_bExitingPrimaryThread;
		m_ProcessingMutex.Unlock();

		if(bExit)
			break;

		if(bStart)
			thread_main(0);
	}
}

DWORD WINAPI Algorithm::algorithm_primary_thread(void *arg)
{
	thread_start_t *pThreadStart = (thread_start_t *) arg;
	thread_start_t ThreadStart = *pThreadStart;
	delete pThreadStart;

	SetThreadName(0);

	ThreadStart.pThis->thread_main_primary();
	ExitThread(0);
	return 0;
}

void Algorithm::Run()
{
	if(m_iThreadsRunning > 0)
		throw Exception("Algorithm::Run() : already running");

	Finish();

	if(!m_Mask.Empty() && (m_Mask.m_iWidth != m_SourceImage.m_iWidth || m_Mask.m_iHeight != m_SourceImage.m_iHeight))
		throw Exception("Given mask and image have different dimensions");

	const Settings &s = GetSettings();
	m_bStopRequest = false;
	m_iProgressCounter = 0;
	m_iStage = -1;

	iNumThreads = GetOptions().nb_threads;
	if(iNumThreads <= 0)
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		iNumThreads += si.dwNumberOfProcessors;
		if(iNumThreads < 1)
			iNumThreads = 1;
	}

	printf("Threads: %i\n", iNumThreads);

	m_iThreadsRunning = iNumThreads;
	fStartedAt = gettime();

	WakePrimaryWorkerThread();
	for(int i=1; i < iNumThreads; ++i)
		CreateWorkerThread(i);
}

bool Algorithm::Running() const
{
	m_ProcessingMutex.Lock();
	bool bRet = m_iThreadsRunning > 0;
	m_ProcessingMutex.Unlock();

	if(bRet)
		return true;

	if(fStartedAt)
		printf("Timing: took %f\n", gettime() - fStartedAt);
	fStartedAt = 0;

	/* All threads have exited. */
	return false;
}

bool Algorithm::GetError(string &sError)
{
	sError = m_sError;
	m_sError.clear();
	return !sError.empty();
}

float Algorithm::Progress() const
{
	if(!Running())
		return 0.0f;

	const Settings &s = GetSettings();
	int maxcounter = 0;
	if(s.fGamma != 1)
		maxcounter += m_ProcBlocks.GetTotalRows()*2; // unsharp_region_apply_gamma
	maxcounter += m_ProcBlocks.GetTotalRows(); // unsharp_region_apply_blur_horiz
	maxcounter += m_ProcBlocks.GetTotalCols(); // unsharp_region_apply_blur_vert
	maxcounter += m_ProcBlocks.GetTotalRows(); // unsharp_region_combine
	return min(m_iProgressCounter*99.9f/maxcounter,99.9f) / 100.0f;
}

void Algorithm::Abort()
{
	m_ProcessingMutex.Lock();
	if(m_iThreadsRunning == 0)
	{
		m_ProcessingMutex.Unlock();
		return;
	}

	m_bStopRequest = true;
	m_Signal.Broadcast();

	while(m_iThreadsRunning > 0)
		m_Signal.Wait();
	m_ProcessingMutex.Unlock();

	Finish();
}

DWORD WINAPI Algorithm::algorithm_thread(void *arg)
{
	thread_start_t *pThreadStart = (thread_start_t *) arg;
	thread_start_t ThreadStart = *pThreadStart;
	delete pThreadStart;

	SetThreadName(ThreadStart.iThreadNo);
	ThreadStart.pThis->thread_main(ThreadStart.iThreadNo);
	ExitThread(0);
	return 0;
}

class AbortedException: public Exception
{
public:
	AbortedException() throw(): Exception("Aborted") { }
};

void Algorithm::Synchronize()
{
	bool bStopRequest;
	const Settings &s = GetSettings();
	m_ProcessingMutex.Lock();
	--m_iThreadsRemainingInStage;
	int iStage = m_iStage;
	m_Signal.Broadcast();

	while((m_iThreadsRemainingInStage > 0 && m_iStage == iStage) && !m_bStopRequest)
		m_Signal.Wait();
	bStopRequest = m_bStopRequest;

	if(m_iStage == iStage)
	{
		m_iThreadsRemainingInStage = iNumThreads;
		m_iStage = iStage+1;
		m_Signal.Broadcast();
	}
	m_ProcessingMutex.Unlock();

	if(bStopRequest)
		throw AbortedException();
}

/* Process m_img, outputting into m_Temp. */
void Algorithm::Denoise(int iThreadNo)
{
	const Settings &s = GetSettings();
	const Options &o = GetOptions();
	volatile LONG *pProgress = &m_iProgressCounter;

	/* Handle the heavyweight allocations now that our synchronization is set up, so if
	 * we throw an exception, the threads will be cancelled cleanly. */
	if(iThreadNo == 0)
	{
		m_Slices.Init(m_WorkImage.height);

		/* Optimization: if the mask is all-on, clear it so we don't do checks later. */
		bool bMaskIsUsed = false;
		cimgIM_forXY(m_WorkMask, x, y)
		{
			if(!m_WorkMask(x,y))
			{
				bMaskIsUsed = true;
				break;
			}
		}
		if(!bMaskIsUsed)
			m_WorkMask.Free();

		double tt = gettime();

		float fScale = s.m_fInputScale / 255.0f;
		m_WorkImage.scale(fScale);

		unsharp_region(
			m_WorkImage, m_Dest,
			s.fRadius, s.fAmountUp,
			s.fGamma, s.fAmountDown, s.fThreshold,
			s.fShadow, s.fMidtone, s.fLight, s.fHigh, &m_bStopRequest, &m_iProgressCounter);
		m_WorkImage.swap(m_Dest);
double fstart = gettime();
		m_WorkImage.scale(1.0f / fScale);
double ff = gettime(); printf("xxxx: %f\n", ff - fstart); fstart = ff;

		printf("Timing: prep %f\n", gettime() - tt);
	}

	Synchronize();

	return;

	/* We're done with the original structure tensors m_G for this pass.  Reuse the storage
	 * for the next stage. */
	{
		Synchronize();
		if(iThreadNo == 0)
		{
			m_Slices.Reset();
		}
		Synchronize();

		double fTime = gettime();
		printf("Timing: do_blur_anisotropic %f\n", gettime() - fTime); fTime = gettime();
		Synchronize();

		Synchronize();

	double tt = gettime();
		Synchronize();
		if(iThreadNo == 0)
			m_Slices.Reset();
		Synchronize();
		printf("Timing: main %f\n", gettime() - tt);

		Synchronize();
		if(m_bStopRequest)
			return;

		/* Copy and scale the finished data back. */
		Synchronize();
		if(iThreadNo == 0)
			m_Slices.Reset();
		Synchronize();
	}

	Synchronize();
}

void Algorithm::RunDenoise(int iThreadNo)
{
	const Settings &s = GetSettings();
	const Options &o = GetOptions();

	/* Initialize thread synchronization. */
	m_ProcessingMutex.Lock();
	if(iThreadNo == 0)
	{
		/* Don't allocate memory here; we don't want to throw while locked. */
		m_iStage = 0;
		m_iThreadsRemainingInStage = iNumThreads;
		m_iThreadsFinished = 0;
		m_Signal.Broadcast();
	}
	else
	{
		while(m_iStage == -1)
			m_Signal.Wait();
	}
	m_ProcessingMutex.Unlock();
	Synchronize();

	/*
	 * To reduce peak memory usage, process the image by breaking it into blocks.  We
	 * need to be able to do this in both dimensions, so we can also keep blocks under
	 * 4096x4096 for OpenGL.
	 *
	 * Blocks overlap, to prevent seams near the edges.
	 *
	 * Tricky: the source of each block needs to be the original image data, not the result
	 * of previous blocks where the blocks overlap.  We want to avoid making a whole separate
	 * copy of the image, to save memory.  So, keep a "backup" of the overlap area of each
	 * block.  These are the only areas where the results of one block can affect the others.
	 */

	int iOverlapPixels; // valid on thread 0 only
	if(iThreadNo == 0)
	{
		/* XXX: this is a guess of length; the actual pixel distance we go is much less */
		/* XXX: this is enough for the final data, but more may be needed for the blurring passes */
//		const float n = 2; /* guess */
//		const float sqrt2amplitude = sqrtf(2*s.amplitude);
//		const float fsigma = n * sqrt2amplitude;
//		const float length = s.gauss_prec * fsigma;

		/* The amount of overlap around each block slice to include.  This is the region
		 * past the actual area we're processing where we may blur data from. */
		iOverlapPixels = s.fRadius + 10;

		m_ProcBlocks.SetLimitTo4096(false);
		m_ProcBlocks.LoadFromSourceImage(m_SourceImage, iOverlapPixels);
		m_ProcBlocks.DeleteMaskedBlocks(m_Mask);

		m_Dest.alloc(m_ProcBlocks.GetMaxBlockWidth(), m_ProcBlocks.GetMaxBlockHeight(), GetProcessedChannels());
	}
	Synchronize();

	if(iThreadNo == 0)
	{
		m_ProcBlocks.SaveOverlaps();

		for(size_t iBlock = 0; iBlock < m_ProcBlocks.GetTotalBlocks(); ++iBlock)
		{
			m_ProcBlocks.GetBlock(m_WorkImage, (int) iBlock, GetProcessedChannels());

			if(!m_Mask.Empty())
				m_ProcBlocks.GetBlockMask(m_WorkMask, m_Mask, (int) iBlock);

			/* Run the filter. */
			Synchronize();
			Denoise(iThreadNo);
			Synchronize();

			m_ProcBlocks.StoreBlock(m_WorkImage, (int) iBlock);
		}
	}
	else
	{
		for(size_t iBlock = 0; iBlock < m_ProcBlocks.GetTotalBlocks(); ++iBlock)
		{
			/* Wait for thread 0 to handle setup. */
			Synchronize();
			Denoise(iThreadNo);
			Synchronize();
		}
	}
}

void Algorithm::thread_main(int iThreadNo)
{
	try
	{
		RunDenoise(iThreadNo);

		if(iThreadNo == 0 && m_pCallbacks.get())
			m_pCallbacks->Finished();
	} catch(const AbortedException &e) {
		/* We were aborted, either by a call to Abort() or because of an exception
		 * in another thread. */
	} catch(const std::exception &e) {
		/* If we throw an exception in any thread, abort all other threads, and pass
		 * the exception up to the main thread by throwing an exception the next time
		 * Running() is called. */
		m_ProcessingMutex.Lock(); /* lock in case multiple threads throw an exception simultaneously */
		m_sError = e.what();
		m_bStopRequest = true;
		m_Signal.Broadcast();
		m_ProcessingMutex.Unlock();
	}

	/* Once we decrement m_iThreadsRunning and unlock m_ProcessingMutex, *this may be deallocated
	 * at any time. */
	m_ProcessingMutex.Lock();
	m_Signal.Broadcast();
	InterlockedDecrement(&m_iThreadsRunning);
	m_ProcessingMutex.Unlock();
}
