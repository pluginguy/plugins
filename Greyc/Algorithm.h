#ifndef ALGORITHM_H
#define ALGORITHM_H

#include "CImgI.h"
#include "GreycGPU.h"
#include "Threads.h"
#include "Helpers.h"
#include "AlgorithmShared.h"

struct Algorithm
{
	struct Callbacks
	{
		~Callbacks() { }
		virtual void Finished() { }
	};

	AlgorithmSettings &GetSettings() { return m_Settings; }
	const AlgorithmSettings &GetSettings() const { return m_Settings; }
	AlgorithmOptions &GetOptions() { return m_Options; }
	const AlgorithmOptions &GetOptions() const { return m_Options; }

	Algorithm();
	~Algorithm();
	void SetCallbacks(auto_ptr<Callbacks> pCallbacks) { m_pCallbacks = pCallbacks; }
	void SetTarget(const CImg &image);
	void SetMask(const CImg &mask);
	void Run();

	/* True if we've finished running, either successfully or with an error.  Once set, this remains
	 * true until Run() or Abort() are called. */
	bool GetFinished() const { return m_bFinished; }
	bool GetError(string &sError);
	float Progress() const;
	void Abort();

protected:
	bool AnyThreadsAreRunning() const;
	void Finish();
	void CreateMainWorkerThread();
	void DestroyMainWorkerThread();
	void WakePrimaryWorkerThread();
	void CreateWorkerThread(int iThreadNo);
	void thread_main_primary();
	static DWORD WINAPI algorithm_primary_thread(void *arg);
	static DWORD WINAPI algorithm_thread(void *arg);
	void Synchronize();
	void Denoise(int iThreadNo, int iIteraton);
	void RunDenoise(int iThreadNo);
	void thread_main(int iThreadNo);
	int GetProcessedChannels() const;

	AlgorithmSettings m_Settings;
	AlgorithmOptions m_Options;

	GreycGPU m_GreycGPU;

	/* These buffers are only used in Denoise; they're in here because they're shared by all threads. */
	CImgF m_WorkImage; /* current slice of img */
	CImg m_WorkMask;
	CImgF m_G;
	CImgF m_G2;
	CImgF m_Dest;

	Slices m_Slices;
	mutable Mutex m_ProcessingMutex;
	ThreadCond m_Signal;
	bool m_bExitingPrimaryThread;
	int m_iStage;
	int m_iThreadsRemainingInStage;
	int m_iThreadsFinished;

	/* The source/destination buffer: */
	CImg m_SourceImage;
	CImg m_Mask;

	auto_ptr<Callbacks> m_pCallbacks;

	LONG m_iProgressCounter;
	bool m_bFinished;
	bool m_bStartRequest;
	bool m_bStopRequest;
	volatile LONG m_iThreadsRunning;

	int iNumThreads;
	HANDLE m_hMainThreadHandle;
	vector<HANDLE> m_ahWorkerThreadHandles;

	string m_sError;

	Blocks m_ProcBlocks;

	mutable float fStartedAt; // debug/timing
};

#endif
