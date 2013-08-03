#ifndef THREADS_WIN32_H
#define THREADS_WIN32_H

#include <windows.h>

class Mutex
{
public:
	Mutex();
	~Mutex();

	bool Lock();
	bool TryLock();
	void Unlock();

private:
	friend class ThreadCond;
	HANDLE m_hMutex;
};

class ThreadCond
{
public:
	ThreadCond(Mutex *pParent);
	~ThreadCond();

	bool Wait();
	void Signal(bool bBroadcast = false);
	void Broadcast() { Signal(true); }

private:
	Mutex *m_pParent;
	int m_iNumWaiting;
	CRITICAL_SECTION m_iNumWaitingLock;
	HANDLE m_WakeupSema;
	HANDLE m_WaitersDone;
};

#endif
