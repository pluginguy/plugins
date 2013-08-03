#ifndef ALGORITHM_REMOTE_H
#define ALGORITHM_REMOTE_H

#include <memory>
using namespace std;

#include "AlgorithmShared.h"
#include "CImgI.h"

class NotifierCallback;
struct AlgorithmRemote
{
	friend class NotifierCallback;
	struct Callbacks
	{
		~Callbacks() { }
		virtual void StateChanged() { }
	};

public:
	AlgorithmSettings &GetSettings() { return m_Settings; }
	const AlgorithmSettings &GetSettings() const { return m_Settings; }
	AlgorithmOptions &GetOptions() { return m_Options; }
	const AlgorithmOptions &GetOptions() const { return m_Options; }

	AlgorithmRemote();
	~AlgorithmRemote();
	void SetCallbacks(auto_ptr<Callbacks> pCallbacks) { m_pCallbacks = pCallbacks; }
	void SetTarget(const CImg &image);
	void SetMask(const CImg &mask);
	void Run();

	bool GetFinished() const;
	bool GetError(string &sError);

	/* Update progress, errors and completion from the worker process. */
	void UpdateState();

	float Progress() const { return m_fProgress; }
	void Finalize();
	void Abort();
	static float GetRequiredOverlapFactor();

protected:
	bool Init();
	void Shutdown();
	bool WaitForResponse(int iCommandSent);
	bool RawProcessIO(void *pBuf, int iSize, bool bRead) const;
	bool ReadFromProcessRaw(void *pBuf, int iSize) const;
	bool ReadBufferFromProcess(void *pBuf, int iSize) const;
	bool ReadInt32FromProcess(int *pBuf) const;
	bool WriteToProcessRaw(const void *pBuf, int iSize) const;
        bool WriteBufferToProcess(const void *pBuf, int iSize) const;
        bool WriteInt32ToProcess(int value) const;
	void WriteImageToProcess(const CImg &img) const;

	auto_ptr<ReadNotifier> m_pNotifier;

	AlgorithmSettings m_Settings;
	AlgorithmOptions m_Options;
	CImg m_SourceImage;
	CImg m_Mask;

	auto_ptr<Callbacks> m_pCallbacks;
	bool m_bRunning, m_bFinished;
	mutable string m_sError;
	float m_fProgress;

	HANDLE m_hWorkerProcessHandle;
	HANDLE m_hIOEvent;
	HANDLE m_ToProcess, m_FromProcess;
};

#endif
