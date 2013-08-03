#include "AlgorithmRemote.h"
#include "AlgorithmRemoteProtocol.h"
#include <assert.h>

/*
 * This provides (mostly) the same interface as Algorithm, moving the work to a helper
 * process.
 *
 * This allows us to escape the ugliest trap of Photoshop plugins: Photoshop's allocator
 * isn't threadsafe, which makes implementing threaded filters painfully difficult.  We
 * can't just not use its allocation functions, since it's likely to be using most of
 * the address space of the process.  Moving processing into a separate process gets us
 * out of Photoshop's process, so we can allocate memory normally without running up against
 * address space limits.
 *
 * This also helps isolate the OpenGL processing, so any code or driver issues that may
 * cause instability will be isolated to that process; as long as this layer is careful,
 * crashes within that process won't be able to kill Photoshop entirely.
 *
 * This has one cost: Photoshop may be using a lot of memory, and we won't be able to use
 * memory that it has allocated.
 */

class NotifierCallback: public ReadNotifier::Callback
{
public:
	NotifierCallback(AlgorithmRemote *pRemote): m_pRemote(pRemote) { }
	void ReadNotification(ReadNotifier *pNotifier)
	{
		/* The ReadNotifier is telling us that there's asynchronous data waiting to be read.
		 * Signal the user to call UpdateState.  Once we do this, we'll leave notifications
		 * disabled; they'll be reenabled when UpdateState is called. */
		printf("ReadNotification received\n");
		if(m_pRemote->m_pCallbacks.get())
			m_pRemote->m_pCallbacks->StateChanged();
	}

private:
	AlgorithmRemote *m_pRemote;
};

AlgorithmRemote::AlgorithmRemote()
{
	m_ToProcess = NULL;
	m_FromProcess = NULL;
	m_hWorkerProcessHandle = NULL;
	m_bFinished = false;
	m_fProgress = 0;
	m_hIOEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

AlgorithmRemote::~AlgorithmRemote()
{
	Shutdown();
	CloseHandle(m_hIOEvent);
}

/* Create the stdin and stdout pipes for the child.  We need to use named pipes for this, because
 * for some reason anonymous pipes in Windows don't support overlapped I/O, or any other signallable
 * way to read data. */
bool CreatePipes(HANDLE &hStdin, HANDLE &hFromStdin, HANDLE &hStdout, HANDLE &hFromStdout)
{
	hStdin = hFromStdin = hStdout = hFromStdout = INVALID_HANDLE_VALUE;

	string StdinPipeName = StringUtil::ssprintf("\\\\.\\pipe\\stdin-%i", GetCurrentProcessId());
	string StdoutPipeName = StringUtil::ssprintf("\\\\.\\pipe\\stdout-%i", GetCurrentProcessId());

	hFromStdin =
		CreateNamedPipe(StdinPipeName.c_str(), PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE, 1, 1024*64, 1024*64, 0, NULL);
	if(hFromStdin == INVALID_HANDLE_VALUE)
		goto error;

	hStdin = CreateFile(StdinPipeName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if(hStdin == INVALID_HANDLE_VALUE)
		goto error;

	hFromStdout = CreateNamedPipe(StdoutPipeName.c_str(), PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE, 1, 1024*64, 1024*64, 0, NULL);
	if(hFromStdout == INVALID_HANDLE_VALUE)
		goto error;

	hStdout = CreateFile(StdoutPipeName.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if(hStdin == INVALID_HANDLE_VALUE)
		goto error;

	return true;

error:
	if(hStdin != INVALID_HANDLE_VALUE) CloseHandle(hStdin);
	if(hFromStdin != INVALID_HANDLE_VALUE) CloseHandle(hFromStdin);
	if(hStdout != INVALID_HANDLE_VALUE) CloseHandle(hStdout);
	if(hFromStdout != INVALID_HANDLE_VALUE) CloseHandle(hFromStdout);
	hStdin = hFromStdin = hStdout = hFromStdout = INVALID_HANDLE_VALUE;
	return false;
}

/* Start the worker process.  On error, return false and set m_sError; the caller
 * must call Shutdown. */
bool AlgorithmRemote::Init()
{
	/* If we have an error waiting, it needs to be handled before we can start working again. */
	if(!m_sError.empty())
		return false;

	if(m_hWorkerProcessHandle != NULL)
	{
		/* We're already running.  Just check that the process is still alive. */
		if(WaitForSingleObject(m_hWorkerProcessHandle, 0) != WAIT_OBJECT_0)
			return true;

		/* The process terminated on us while it wasn't being used.  We weren't doing anything,
		 * so no operation was interrupted; just clean up and restart it.  Go into error state,
		 * so Shutdown knows not to try to shut down cleanly. */
		printf("Worker process exit unexpectedly\n");
		m_sError = "Worker process exit unexpectedly";
		Shutdown();
		m_sError = "";
	}

	HANDLE hStdin, hStdout;
	if(!CreatePipes(hStdin, m_ToProcess, hStdout, m_FromProcess))
	{
		m_sError = StringUtil::ssprintf("CreatePipes failed: %s", StringUtil::GetWindowsError(GetLastError()).c_str(), GetLastError());
		return false;
	}

	SetHandleInformation(hStdin, HANDLE_FLAG_INHERIT, 1);
	SetHandleInformation(hStdout, HANDLE_FLAG_INHERIT, 1);
	SetHandleInformation(m_ToProcess, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(m_FromProcess, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFO si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = hStdin;
	si.hStdOutput = hStdout;
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));

	string sDir = GetModuleDirectory();
	string sWorkerPath = sDir + "Greyc-helper.bin";

	/* Annoying: We want the worker process to use our console if it's open, but not to open
	 * one on its own.  We have to work around odd CREATE_NO_WINDOW behavior: if we have a
	 * console open and we give a handle to it to the worker process, writes to it will succeed
	 * but go nowhere. */
	int iCreationFlags = 0;
	if(!ThisProcessHasAConsole())
		iCreationFlags |= CREATE_NO_WINDOW;
	BOOL bRet = CreateProcess(
		(char *) sWorkerPath.c_str(),
		"Greyc-helper.bin --server",
		NULL,	// process security attributes
		NULL,	// thread security attributes
		TRUE,	// inherit handles
		iCreationFlags,	// creation flags
		NULL,	// environment
		NULL,	// cwd
		&si,	// lpStartupInfo,
		&pi	// lpProcessInformation
	);

	CloseHandle(hStdin);
	CloseHandle(hStdout);

	/* We don't need to close StdInWrite and StdOutRead on error; they're stored in m_ToProcess
	 * and m_FromProcess and will be cleaned up normally. */
	if(!bRet)
	{
		m_sError = StringUtil::ssprintf("CreateProcess failed: %s", StringUtil::GetWindowsError(GetLastError()).c_str(), GetLastError());
		return false;
	}

	m_hWorkerProcessHandle = pi.hProcess;
	CloseHandle(pi.hThread);

	/* Start a notifier which will call m_pCallbacks->StateChanged when m_FromProcess is readable.
	 * We often pause this by calling m_pNotifier->Cancel when we're waiting for responses to queries,
	 * but if we get data when we're not waiting for a response, it's a RESP_STATE_CHANGED message. */
	NotifierCallback *pCB = new NotifierCallback(this);
	m_pNotifier.reset(new ReadNotifier(m_FromProcess, pCB));

	return true;
}

/* Shut down the worker process and return to the initial state. */
void AlgorithmRemote::Shutdown()
{
	Abort();

	if(m_hWorkerProcessHandle != NULL && m_sError.empty())
	{
		/* If we're not in an error state, try to shut down the worker cleanly.  If we've
		 * hit an error, skip to killing it. */
		int iCommand = CMD_SHUTDOWN;
		fprintf(stderr, "Writing CMD_SHUTDOWN\n");
		WriteInt32ToProcess(iCommand);
		WaitForResponse(CMD_SHUTDOWN);

		if(m_sError.empty() && WaitForSingleObject(m_hWorkerProcessHandle, 5000) == WAIT_OBJECT_0)
		{
			CloseHandle(m_hWorkerProcessHandle);
			m_hWorkerProcessHandle = NULL;
		}
	}

	if(m_hWorkerProcessHandle != NULL)
	{
		/* We've hit an error, or the process wouldn't shut down quickly.  Skip trying to talk
		 * to the process and just terminate it if it hasn't exited already. */
		if(WaitForSingleObject(m_hWorkerProcessHandle, 0) != WAIT_OBJECT_0)
		{
			if(!TerminateProcess(m_hWorkerProcessHandle, 0))
				printf("TerminateProcess error: %s\n", StringUtil::GetWindowsError(GetLastError()).c_str());
		}
		WaitForSingleObject(m_hWorkerProcessHandle, INFINITE);
		CloseHandle(m_hWorkerProcessHandle);
		m_hWorkerProcessHandle = NULL;
	}

	if(m_ToProcess)
	{
		CloseHandle(m_ToProcess);
		m_ToProcess = NULL;
	}

	if(m_FromProcess)
	{
		CloseHandle(m_FromProcess);
		m_FromProcess = NULL;
	}
}

// XXX: this is static and shouldn't be
float AlgorithmRemote::GetRequiredOverlapFactor()
{
	return 1.25f;
}

void AlgorithmRemote::SetTarget(const CImg &image)
{
	m_SourceImage.Hold(image);
}

void AlgorithmRemote::SetMask(const CImg &mask)
{
	m_Mask.Hold(mask);
}

void AlgorithmRemote::Run()
{
	if(!Init())
		return;

	/* Write the start packet, which includes everything the worker process needs to begin. */
	int iCommand = CMD_START;
	fprintf(stderr, "Writing CMD_START\n");
	WriteInt32ToProcess(iCommand);
	WriteBufferToProcess(&m_Settings, sizeof(m_Settings));
	WriteBufferToProcess(&m_Options, sizeof(m_Options));
	WriteImageToProcess(m_SourceImage);
	WriteImageToProcess(m_Mask);

	/* Wait for the response, indicating that processing has started. */
	printf("%.3f: Run() waiting for response\n", gettime());
	WaitForResponse(CMD_START);

	printf("%.3f: Run() calling UpdateState()\n", gettime());
	this->UpdateState();
	printf("%.3f: Run() done calling UpdateState()\n", gettime());
}

bool AlgorithmRemote::GetFinished() const
{
	return m_bFinished || !m_sError.empty();
}

/*
 * Error handling uses the lazy checking model: the first error sets m_sError, and any
 * further operations are no-ops until the error state is cleared.  We only need to
 * explicitly check m_sError when we're dependent on a result, not after every individual
 * operation.
 *
 * When we clear the error state, we also clean up and terminate the process.
 */
bool AlgorithmRemote::GetError(string &sError)
{
	sError = m_sError;
	if(m_sError.empty())
		return false;

	/* Be sure to leave m_sError set during the call to Shutdown, so it knows we're in
	 * an error state. */
	Shutdown();

	m_sError.clear();
	return true;
}

/* This is called after work has finished, and the finished callback has been invoked.
 * Request the result data from the process and reset. */
void AlgorithmRemote::Finalize()
{
	/* Wait for the response, indicating that processing has started. */
	int iCommand = CMD_GET_RESULT;
	// fprintf(stderr, "Writing CMD_GET_RESULT\n");
	WriteInt32ToProcess(iCommand);
	if(!WaitForResponse(CMD_GET_RESULT))
		return;

	assert(m_SourceImage.m_pData != NULL);
	ReadBufferFromProcess(m_SourceImage.m_pData, m_SourceImage.m_iStrideBytes * m_SourceImage.m_iHeight);

	Abort();
}

void AlgorithmRemote::UpdateState()
{
	if(m_hWorkerProcessHandle == NULL)
		return;

	// printf("Writing CMD_GET_STATE\n");

	int iCommand = CMD_GET_STATE;
	WriteInt32ToProcess(iCommand);

	WaitForResponse(CMD_GET_STATE);
	// fprintf(stderr, "Writing CMD_GET_STATE\n");

	ReadBufferFromProcess(&m_bFinished, sizeof(m_bFinished));
	ReadBufferFromProcess(&m_fProgress, sizeof(m_fProgress));

	if(!m_sError.empty())
	{
		m_fProgress = 0;
		m_bFinished = false;
		return;
	}

	// printf("GET_STATE: %f, %i\n", m_fProgress, m_bFinished);

	/* If the notification was stopped due to receiving a message, restart it now
	 * that we've handled incoming messages. */
	// printf("AlgorithmRemote::UpdateState reenabling notifications\n");
	m_pNotifier->Start();
}

void AlgorithmRemote::Abort()
{
	if(m_hWorkerProcessHandle == NULL)
		return;

	/* Stop sending update notifications. */
	fprintf(stderr, "%.3f: m_pNotifier->Cancel\n", gettime());

	int iCommand = CMD_RESET;
	fprintf(stderr, "%.3f: Writing CMD_RESET\n", gettime());
	WriteInt32ToProcess(iCommand);
	WaitForResponse(CMD_RESET);
	fprintf(stderr, "%.3f: CMD_RESET finished\n", gettime());
}

#include "PhotoshopKeys.h" // for plugInName

/* Read or write data to or from the process. */
bool AlgorithmRemote::RawProcessIO(void *pBuf, int iSize, bool bRead) const
{
	if(!m_sError.empty())
		return false;

	/* We may be trying to read 0 bytes, eg. to receive empty image data.  Don't call ReadFile for
	 * 0 bytes; rather than returning immediately, it'll block until data is available. */
	if(iSize == 0)
		return true;

	/* Read and write with a timeout, so if our worker process gets stuck for some reason,
	 * we can abort. */
	while(1)
	{
		int iGot = IOWithTimeout(bRead? m_FromProcess:m_ToProcess, pBuf, iSize, m_hIOEvent, 1000, bRead);
		if(iGot == -1)
		{
			/* Prompt to retry.  We need to do this in another thread, or else running our message box
			 * will send messages to any other windows currently running in this one. */
			DWORD iError = GetLastError();
			if(iError == WAIT_TIMEOUT)
			{
				/* On Retry, keep waiting a while longer.  XXX: if this actually happens it'll interrupt
				 * batch jobs needlessly */
				int iResult = MessageBoxInThread(NULL, "There was a problem processing the image.  Keep trying?", plugInName, MB_YESNO);
				if(iResult == IDYES)
					continue;
			}

			m_sError = StringUtil::ssprintf("RawProcessIO error: %s", StringUtil::GetWindowsError(iError).c_str());
			return false;
		}

		if(iGot != iSize)
		{
			m_sError = "RawProcessIO error: small response";
			return false;
		}
		return true;
	}
}

/* Read data from the process. */
bool AlgorithmRemote::ReadFromProcessRaw(void *pBuf, int iSize) const
{
	return m_pNotifier->Read(pBuf, iSize, 5000) != -1;
}

/* Write data to the process. */
bool AlgorithmRemote::WriteToProcessRaw(const void *pBuf, int iSize) const
{
	return RawProcessIO((void *) pBuf, iSize, false);
}

/* Read a packet of data from the process. */
bool AlgorithmRemote::ReadBufferFromProcess(void *pBuf, int iSize) const
{
	int iActualSize;
	if(!ReadFromProcessRaw(&iActualSize, sizeof(iActualSize)))
		return false;
	if(iSize != iActualSize)
	{
		m_sError = StringUtil::ssprintf("Protocol error: expected %i bytes, got %i", iSize, iActualSize);
		return false;
	}
	return ReadFromProcessRaw(pBuf, iSize);
}


bool AlgorithmRemote::ReadInt32FromProcess(int *pBuf) const
{
        __int32 value;
        if(!ReadBufferFromProcess(&value, 4))
                return false;

        *pBuf = value;
        return true;
}

bool AlgorithmRemote::WriteBufferToProcess(const void *pBuf, int iSize) const
{
	return WriteToProcessRaw(pBuf, iSize);
}

bool AlgorithmRemote::WriteInt32ToProcess(int value) const
{
        __int32 i32 = (__int32) value;
	return WriteToProcessRaw(&i32, 4);
}

bool AlgorithmRemote::WaitForResponse(int iCommandSent)
{
	while(1)
	{
		int iResponseCode;
		if(!ReadInt32FromProcess(&iResponseCode))
			return false;

		/* RESP_FINISHED is async and tells us immediately when processing is finished.  However,
		 * the response to any pending command must still be replied to. */
		if(iResponseCode == RESP_STATE_CHANGED)
		{
			fprintf(stderr, "Got RESP_STATE_CHANGED\n");
			continue;
		}

		if(iResponseCode == RESP_ERROR)
		{
			int iErrorSize;
			if(!ReadInt32FromProcess(&iErrorSize))
				return false;

			char *pBuf = (char *) _alloca(iErrorSize+1);
			if(pBuf == NULL)
			{
				m_sError = "Allocating memory for worker error message failed";
				return false;
			}

			if(!ReadBufferFromProcess(pBuf, iErrorSize))
				return false;

			pBuf[iErrorSize] = 0;

			printf("RESP_ERROR: %s\n", pBuf);
			m_sError = StringUtil::ssprintf("Error from process: %s", pBuf);

			/* All errors are fatal to the worker process.  Shut down the worker process if it
			 * hasn't shut down on its own. */
			Shutdown();
			return false;
		}

		if(iResponseCode == RESP_OK)
		{
			int iRespondingToCommand;
			ReadInt32FromProcess(&iRespondingToCommand);
			if(iRespondingToCommand != iCommandSent)
			{
				fprintf(stderr, "Got unexpected response command %i to %i\n", iRespondingToCommand, iCommandSent );
				abort();
			}
			return true;
		}

		m_sError = StringUtil::ssprintf("Got unexpected response byte %x", iResponseCode);
		return false;
	}
}

void AlgorithmRemote::WriteImageToProcess(const CImg &img) const
{
        WriteInt32ToProcess(img.m_iWidth);
        WriteInt32ToProcess(img.m_iHeight);
        WriteInt32ToProcess(img.m_iStrideBytes);
        WriteInt32ToProcess(img.m_iBytesPerChannel);
        WriteInt32ToProcess(img.m_iChannels);
	WriteBufferToProcess(img.m_pData, img.m_iStrideBytes * img.m_iHeight);
}

