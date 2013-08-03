#define _WIN32_WINNT 0x0400
#include "Threads.h"

Mutex::Mutex()
{
	m_hMutex = CreateMutex(NULL, false, NULL);
}

Mutex::~Mutex()
{
	CloseHandle(m_hMutex);
}

bool Mutex::Lock()
{
	return WaitForSingleObject(m_hMutex, INFINITE) == WAIT_OBJECT_0;
}

bool Mutex::TryLock()
{
	return WaitForSingleObject(m_hMutex, 0) == WAIT_OBJECT_0;
}

void Mutex::Unlock()
{
	ReleaseMutex(m_hMutex);
}

ThreadCond::ThreadCond(Mutex *pParent)
{
	m_pParent = pParent;
	m_iNumWaiting = 0;
	m_WakeupSema = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	InitializeCriticalSection(&m_iNumWaitingLock);
	m_WaitersDone = CreateEvent(NULL, FALSE, FALSE, NULL);
}

ThreadCond::~ThreadCond()
{
	CloseHandle(m_WakeupSema);
	DeleteCriticalSection(&m_iNumWaitingLock);
	CloseHandle(m_WaitersDone);
}

/* http://www.cs.wustl.edu/~schmidt/win32-cv-1.html */
bool ThreadCond::Wait()
{
	EnterCriticalSection(&m_iNumWaitingLock);
	++m_iNumWaiting;
	LeaveCriticalSection(&m_iNumWaitingLock);

	/* Unlock the mutex and wait for a signal. */
	bool bSuccess = SignalObjectAndWait(m_pParent->m_hMutex, m_WakeupSema, INFINITE, false) == WAIT_OBJECT_0;

	EnterCriticalSection(&m_iNumWaitingLock);
	if(!bSuccess)
	{
		if(WaitForSingleObject(m_WakeupSema, 0) == WAIT_OBJECT_0)
			bSuccess = true;
	}
	--m_iNumWaiting;
	bool bLastWaiting = m_iNumWaiting == 0;
	LeaveCriticalSection(&m_iNumWaitingLock);

	/* If we're the last waiter to wake up, and we were actually woken by another
	 * thread (not by timeout), wake up the signaller. */
	if(bLastWaiting && bSuccess)
		SignalObjectAndWait(m_WaitersDone, m_pParent->m_hMutex, INFINITE, false);
	else
		WaitForSingleObject(m_pParent->m_hMutex, INFINITE);

	return bSuccess;
}

void ThreadCond::Signal(bool bBroadcast)
{
	EnterCriticalSection(&m_iNumWaitingLock);

	if(m_iNumWaiting == 0)
	{
		LeaveCriticalSection(&m_iNumWaitingLock);
		return;
	}

	if(bBroadcast)
		ReleaseSemaphore(m_WakeupSema, m_iNumWaiting, 0);
	else
		ReleaseSemaphore(m_WakeupSema, 1, 0);

	LeaveCriticalSection(&m_iNumWaitingLock);
	WaitForSingleObject(m_WaitersDone, INFINITE);
}
