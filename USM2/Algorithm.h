#ifndef ALGORITHM_H
#define ALGORITHM_H

#include "CImgI.h"
#include "Threads.h"
#include "Helpers.h"
#include <memory>

struct Algorithm
{
	/* Settings are configuration that affect the output. */
	struct Settings
	{
		Settings();
		string GetAsString() const;

		float m_fInputScale;

		float fRadius;
		float fAmountUp;
		float fAmountDown;
		float fThreshold;
		float fGamma;
		float fHigh;
		float fLight;
		float fMidtone;
		float fShadow; 
	};

	/* Options are configuration that don't affect the output. */
	struct Options
	{
		Options();

		/* If zero, uses one thread per processor.  If positive, uses the specified number of
		 * threads.  If negative, uses fewer threads than processors. */
		int nb_threads;

		enum DisplayMode
		{
			DISPLAY_SINGLE,
			DISPLAY_INSIDE,
			DISPLAY_SIDE_BY_SIDE
		};

		DisplayMode m_DisplayMode;
	};

	struct Callbacks
	{
		~Callbacks() { }
		virtual void Finished() { }
	};

	Settings &GetSettings() { return m_Settings; }
	const Settings &GetSettings() const { return m_Settings; }
	Options &GetOptions() { return m_Options; }
	const Options &GetOptions() const { return m_Options; }

	Algorithm();
	~Algorithm();
	void SetCallbacks(auto_ptr<Callbacks> pCallbacks) { m_pCallbacks = pCallbacks; }
	void SetTarget(const CImg &image);
	void SetMask(const CImg &mask);
	void Run();
	bool Running() const;
	bool GetError(string &sError);
	float Progress() const;
	void Abort();
	static float GetRequiredOverlapFactor();

protected:
	void Finish();
	void CreateMainWorkerThread();
	void DestroyMainWorkerThread();
	void WakePrimaryWorkerThread();
	void CreateWorkerThread(int iThreadNo);
	void thread_main_primary();
	static DWORD WINAPI algorithm_primary_thread(void *arg);
	static DWORD WINAPI algorithm_thread(void *arg);
	void Synchronize();
	void Denoise(int iThreadNo);
	void RunDenoise(int iThreadNo);
	void thread_main(int iThreadNo);
	int GetProcessedChannels() const;

	Settings m_Settings;
	Options m_Options;

	/* These buffers are only used in Denoise; they're in here because they're shared by all threads. */
	CImgF m_WorkImage; /* current slice of img */
	CImg m_WorkMask;
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
