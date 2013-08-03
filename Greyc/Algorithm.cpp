/* Implement the high-level interface for the algorithm.  This calls off to
 * GreycC and GreycGPU for the math. */

#define NOMINMAX
#include "Algorithm.h"
#include "Helpers.h"
#include "StringUtil.h"
#include "GreycC.h"
#include <vector>
#include <algorithm>
using namespace std;
#include <assert.h>
#include <math.h>

#pragma warning (disable : 4244) // 'initializing' : conversion from 'int' to 'const float', possible loss of data
#pragma warning (disable : 4101) // unreferenced local variable

#include <windows.h>


Algorithm::Algorithm():
	m_iProgressCounter(0),
	m_bStartRequest(false),
	m_bStopRequest(false),
	m_Signal(&m_ProcessingMutex)
{
	m_iThreadsRunning = 0;
	m_hMainThreadHandle = NULL;
	m_bFinished = false;

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
	if(m_Options.m_bGPU)
		return 4;
	else
		return max(4, m_SourceImage.m_iChannels);
}

void Algorithm::Finish()
{
	m_iProgressCounter = 0;
	m_bStopRequest = false;
	m_bFinished = false;
	m_G.free();
	m_G2.free();
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

	m_GreycGPU.Cleanup();
}

DWORD WINAPI Algorithm::algorithm_primary_thread(void *arg)
{
	thread_start_t *pThreadStart = (thread_start_t *) arg;
	thread_start_t ThreadStart = *pThreadStart;
	delete pThreadStart;

	SetThreadName(StringUtil::ssprintf("Processing thread %i", 0).c_str());

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

	const AlgorithmSettings &s = GetSettings();
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

bool Algorithm::AnyThreadsAreRunning() const
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
	if(m_bFinished)
		return 1.0f;

	const AlgorithmSettings &s = GetSettings();
	const float da = s.da;
	float maxcounter;
	if(GetOptions().m_bGPU)
	{
		/* In GPU mode, we just count one step per stage per block. */
		maxcounter = 2 + (360/da);
		maxcounter *= m_ProcBlocks.GetTotalBlocks();
	}
	else
	{
		const float factor = 2 * (360/da) + 1;
		maxcounter = m_ProcBlocks.GetTotalRows()*factor;
	}
	maxcounter *= s.iterations;
	if(maxcounter == 0)
		return 1.0f;
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

	/* All threads have finished, so thread 0 set m_bFinished on its way out. */
	assert(m_bFinished);

	Finish();
}

DWORD WINAPI Algorithm::algorithm_thread(void *arg)
{
	thread_start_t *pThreadStart = (thread_start_t *) arg;
	thread_start_t ThreadStart = *pThreadStart;
	delete pThreadStart;

	SetThreadName(StringUtil::ssprintf("Processing thread %i", ThreadStart.iThreadNo).c_str());
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
	const AlgorithmSettings &s = GetSettings();
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
void Algorithm::Denoise(int iThreadNo, int iIteraton)
{
	const AlgorithmSettings &s = GetSettings();
	const AlgorithmOptions &o = GetOptions();
	volatile LONG *pProgress = &m_iProgressCounter;

	/* Handle the heavyweight allocations now that our synchronization is set up, so if
	 * we throw an exception, the threads will be cancelled cleanly. */

	if(iThreadNo == 0)
	{
		m_Slices.Init(m_WorkImage.height);

		if(!o.m_bGPU)
			m_Dest.fill(0);
		m_G.fill(0);

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

		/* Generate the structure tensors, m_G.  This is relatively quick and not threaded.
		 * Only do m_fPreBlur on the first iteration. */
		double tt = gettime();
		do_blur_anisotropic_prep(m_WorkImage, m_G, &m_bStopRequest, o.m_bGPU? NULL:&m_iProgressCounter,
			iIteraton == 0? s.m_fPreBlur:0, s.alpha, s.sigma, s.gfact * s.m_fInputScale, s.partial_stage_output);
		printf("Timing: prep %f\n", gettime() - tt);
		if(o.m_bGPU)
			progress;
	}

	/* Wait for the processed structure tensors to be ready. */
	Synchronize();

	/* If we're displaying intermediate output, stop now. */
	if(s.partial_stage_output != 0)
		return;

	/* We're done with the original structure tensors m_G for this pass.  Reuse the storage
	 * for the next stage. */
	if(o.m_bGPU)
	{
		if(iThreadNo == 0)
		{
			double tt = gettime();
			m_GreycGPU.ProcessAnisotropic(m_WorkImage, m_G, &m_bStopRequest, &m_iProgressCounter,
					s.alt_amplitude, s.amplitude, s.da, s.dl, s.gauss_prec, s.sharpness, s.anisotropy, s.interpolation, s.fast_approx, m_SourceImage.m_iBytesPerChannel > 1);
			printf("Timing: GPU total %f\n", gettime() - tt);
		}
	}
	else
	{
		/* From m_G, process the structure tensors m_G2.  m_G is read-only; each thread writes only
		 * to its portion of m_G2, and does not read m_G2. */
		Synchronize();
		if(iThreadNo == 0)
		{
			m_G2.alloc(m_WorkImage.width, m_WorkImage.height, 4);
			m_G2.fill(0);
			m_Slices.Reset();
		}
		Synchronize();

		double fTime = gettime();
		do_blur_anisotropic(m_G, m_G2, &m_bStopRequest, &m_iProgressCounter, &m_Slices, s.sharpness, s.anisotropy);

		printf("Timing: do_blur_anisotropic %f\n", gettime() - fTime); fTime = gettime();
		Synchronize();

		if(iThreadNo == 0)
			m_G.alloc(m_WorkImage.width, m_WorkImage.height, 4);

		Synchronize();

	double tt = gettime();
		int N = 0;
		for(float theta=(360%(int)s.da)/2.0f; theta<360; theta += s.da)
		{
			++N;
			Synchronize();
			if(iThreadNo == 0)
				m_Slices.Reset();
			Synchronize();
			do_blur_anisotropic_init_for_angle(m_G2, m_G, &m_bStopRequest, &m_iProgressCounter,
				&m_Slices, theta, s.dl);

			/* Run the blur. */
			Synchronize();
			if(iThreadNo == 0)
				m_Slices.Reset();
			Synchronize();
			do_blur_anisotropic_with_vectors_angle(m_WorkImage, m_G, m_WorkMask, m_Dest, &m_bStopRequest, &m_iProgressCounter,
					&m_Slices,
					s.alt_amplitude, s.amplitude, s.dl, s.gauss_prec, s.interpolation, s.fast_approx);
		}
		printf("Timing: main %f\n", gettime() - tt);

		Synchronize();
		if(m_bStopRequest)
			return;

		/* Copy and scale the finished data back. */
		Synchronize();
		if(iThreadNo == 0)
			m_Slices.Reset();
		Synchronize();
		do_blur_anisotropic_finalize(m_Dest, m_WorkImage, N, m_WorkMask, &m_Slices, &m_bStopRequest);
	}

	Synchronize();
}

void Algorithm::RunDenoise(int iThreadNo)
{
	const AlgorithmSettings &s = GetSettings();
	const AlgorithmOptions &o = GetOptions();

	// XXX
	if (s.dl<0 || s.da<0 || s.gauss_prec<0)
		throw Exception("dl>0, da>0, gauss_prec>0");

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

	int iMaxBlockWidth, iMaxBlockHeight, iOverlapPixels; // valid on thread 0 only
	if(iThreadNo == 0)
	{
		/* XXX: this is a guess of length; the actual pixel distance we go is much less */
		/* XXX: this is enough for the final data, but more may be needed for the blurring passes */
		const float n = 2; /* guess */
		const float sqrt2amplitude = sqrtf(2*s.amplitude);
		const float fsigma = n * sqrt2amplitude;
		const float length = s.gauss_prec * fsigma;

		/* The amount of overlap around each block slice to include.  This is the region
		 * past the actual area we're processing where we may blur data from. */
		iOverlapPixels = min((int) length, 100); /* tolerate large aplitude values */

		m_ProcBlocks.SetLimitTo4096(GetOptions().m_bGPU);
		m_ProcBlocks.LoadFromSourceImage(m_SourceImage, iOverlapPixels);
		m_ProcBlocks.DeleteMaskedBlocks(m_Mask);

		if(!o.m_bGPU)
			m_Dest.alloc(m_ProcBlocks.GetMaxBlockWidth(), m_ProcBlocks.GetMaxBlockHeight(), GetProcessedChannels());
	}
	Synchronize();

	if(iThreadNo == 0)
	{
		for(int i = 0; i < s.iterations; ++i)
		{
			m_ProcBlocks.SaveOverlaps();

			for(size_t iBlock = 0; iBlock < m_ProcBlocks.GetTotalBlocks(); ++iBlock)
			{
				m_ProcBlocks.GetBlock(m_WorkImage, iBlock, GetProcessedChannels());

				if(!m_Mask.Empty())
					m_ProcBlocks.GetBlockMask(m_WorkMask, m_Mask, iBlock);

				/* Run the filter. */
				Synchronize();
				Denoise(iThreadNo, i);
				Synchronize();

				m_ProcBlocks.StoreBlock(m_WorkImage, iBlock);
			}
		}
	}
	else
	{
		for(int i = 0; i < s.iterations; ++i)
		{
			for(size_t iBlock = 0; iBlock < m_ProcBlocks.GetTotalBlocks(); ++iBlock)
			{
				/* Wait for thread 0 to handle setup. */
				Synchronize();
				Denoise(iThreadNo, i);
				Synchronize();
			}
		}
	}
}

void Algorithm::thread_main(int iThreadNo)
{
	try
	{
		RunDenoise(iThreadNo);
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

	/* Signal completion, whether we exited with success or due to a signal. */
	if(iThreadNo == 0)
	{
		m_bFinished = true;
		if(m_pCallbacks.get())
			m_pCallbacks->Finished();
	}

	/* Once we decrement m_iThreadsRunning and unlock m_ProcessingMutex, *this may be deallocated
	 * at any time. */
	m_ProcessingMutex.Lock();
	m_Signal.Broadcast();
	InterlockedDecrement(&m_iThreadsRunning);
	m_ProcessingMutex.Unlock();
}
