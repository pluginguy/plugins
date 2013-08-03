#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <windows.h>
#include <io.h>

#include "Algorithm.h"
#include "Threads.h"
#include "CrashReporting.h"
#include "AlgorithmRemoteProtocol.h"

/*
 * This process receives data to be processed on stdin, and returns the results on stdout.
 */
class Protocol
{
public:
	Protocol(ReadNotifier *pIn, int iOutFD);
	~Protocol();

	void ReadCommandData(void *pData, int iSize);
	int ReadCommand();
	void WriteRawData(const void *pData, int iSize);
	void WriteCommandData(const void *pData, int iSize);
	void WriteMessage(int iCommand);
	void ReadImageData(CImg &img);
	void WriteCommandResponseOK(int iCommand);
	void WriteCommandResponseError(string sError);

private:
	ReadNotifier *m_pIn;
	int m_iOutFD;
};

Protocol::Protocol(ReadNotifier *pIn, int iOutFD)
{
	m_pIn = pIn;
	m_iOutFD = iOutFD;
}

Protocol::~Protocol()
{
}

void Protocol::ReadCommandData(void *pData, int iSize)
{
	char *pDataBuf = (char *) pData;
	while(iSize > 0)
	{
		int iGot = m_pIn->Read(pDataBuf, iSize, INFINITE);
		if(iGot == 0)
		{
			fprintf(stderr, "Error reading from control pipe: %i %s\n", GetLastError(), StringUtil::GetWindowsError(GetLastError()).c_str());
			exit(1);
		}
		pDataBuf += iSize;
		iSize -= iGot;
	}
}

int Protocol::ReadCommand()
{
	int iCommand;
	ReadCommandData(&iCommand, sizeof(iCommand));
	return iCommand;
}

void Protocol::WriteRawData(const void *pData, int iSize)
{
	while(iSize > 0)
	{
		int iGot = write(m_iOutFD, pData, iSize);
		if(iGot == -1)
		{
			fprintf(stderr, "Error writing to control pipe: %s\n", strerror(errno));
			exit(1);
		}
		pData = ((char *) pData) + iSize;
		iSize -= iGot;
	}
}

void Protocol::WriteCommandData(const void *pData, int iSize)
{
	WriteRawData(&iSize, sizeof(iSize));
	WriteRawData(pData, iSize);
}

void Protocol::WriteMessage(int iCommand)
{
	WriteCommandData(&iCommand, sizeof(iCommand));
}

void Protocol::WriteCommandResponseOK(int iCommand)
{
	WriteMessage(RESP_OK);
	WriteMessage(iCommand);
}

/* Write an error message to the front-end.  It'll display the message and kill us. */
void Protocol::WriteCommandResponseError(string sError)
{
	WriteMessage(RESP_ERROR);
	int iSize = sError.size();
	WriteCommandData(&iSize, sizeof(iSize));
	WriteCommandData(sError.data(), iSize);
}

void Protocol::ReadImageData(CImg &img)
{
	img.Free();

	ReadCommandData(&img.m_iWidth, sizeof(img.m_iWidth));
	ReadCommandData(&img.m_iHeight, sizeof(img.m_iHeight));
	ReadCommandData(&img.m_iStrideBytes, sizeof(img.m_iStrideBytes));
	ReadCommandData(&img.m_iBytesPerChannel, sizeof(img.m_iBytesPerChannel));
	ReadCommandData(&img.m_iChannels, sizeof(img.m_iChannels));

	img.m_pData = NULL;
	img.Alloc(img.m_iWidth, img.m_iHeight, img.m_iBytesPerChannel, img.m_iChannels, img.m_iStrideBytes);

	ReadCommandData(img.m_pData, img.m_iStrideBytes * img.m_iHeight);
}

struct AlgorithmCallbacks: public Algorithm::Callbacks
{
	AlgorithmCallbacks(HANDLE hEvent): m_hEvent(hEvent)
	{
	}

	void Finished()
	{
		/* The running filter has stopped, so hint the client to update. */
		SetEvent(m_hEvent);
	}

private:
	HANDLE m_hEvent;
};


struct NotifierCallbacks: public ReadNotifier::Callback
{
public:
	NotifierCallbacks(HANDLE hEvent): m_hEvent(hEvent) { }
	void ReadNotification(ReadNotifier *pNotifier)
	{
		/* We've received a message from the client; wake up. */
		SetEvent(m_hEvent);
	}

private:
	HANDLE m_hEvent;
};

void ServerMainLoop(int iInFD, int iOutFD)
{
	printf("Worker: starting\n");
	// Sleep(5000);

	CImg SourceImage, Mask;

	HANDLE hAlgorithmFinishedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	HANDLE hDataFromServerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	Algorithm alg;
	alg.SetCallbacks(auto_ptr<AlgorithmCallbacks>(new AlgorithmCallbacks(hAlgorithmFinishedEvent)));
	HANDLE hInputHandle = (HANDLE) _get_osfhandle(iInFD);

	ReadNotifier notifier(hInputHandle, new NotifierCallbacks(hDataFromServerEvent));
	Protocol pr(&notifier, iOutFD);

	bool bShutdown = false;
	while(!bShutdown)
	{
		notifier.Start();
		{
			HANDLE aHandles[] = { hAlgorithmFinishedEvent, hDataFromServerEvent };
			DWORD iResult = WaitForMultipleObjects(2, aHandles, FALSE, INFINITE);

			if(iResult == WAIT_FAILED)
			{
				printf("WaitForMultipleObjects failed: %i %s\n", GetLastError(), StringUtil::GetWindowsError(GetLastError()).c_str());
				exit(1);
			}

			if(iResult == WAIT_OBJECT_0 + 0)
			{
				/* hAlgorithmFinishedEvent was signalled. */
				printf("Worker: sending RESP_STATE_CHANGED\n");
				pr.WriteMessage(RESP_STATE_CHANGED);
				continue;
			}

			if(iResult != WAIT_OBJECT_0 + 1)
				continue;

			/* Keep going and process the packet synchronously. */
		}

		int iCommand = pr.ReadCommand();
		if(iCommand != CMD_GET_STATE)
			printf("Worker: got command %i\n", iCommand);

		/* Check for errors. */
		{
			string sError;
			if(alg.GetError(sError))
			{
				printf("Worker: responded with error \"%s\"\n", sError.c_str());
				pr.WriteCommandResponseError(sError);

				/* Errors are fatal.  Don't continue and try to process the command.  Don't
				 * bother reading the rest of the packet; just shut down, since the front-end
				 * will shut us down if we don't anyway. */
				alg.Abort();
				bShutdown = true;
				continue;
			}
		}

		if(iCommand == CMD_START)
		{
			printf("Worker: responding to CMD_START\n");
			alg.Abort();

			pr.ReadCommandData(&alg.GetSettings(), sizeof(alg.GetSettings()));
			pr.ReadCommandData(&alg.GetOptions(), sizeof(alg.GetOptions()));
			pr.ReadImageData(SourceImage);
			pr.ReadImageData(Mask);

			alg.SetTarget(SourceImage);
			alg.SetMask(Mask);

			printf("Worker: got image data: %ix%i\n", SourceImage.m_iWidth, SourceImage.m_iHeight);
			pr.WriteCommandResponseOK(iCommand);

			printf("Worker: Starting processing\n");
			alg.Run();
		}

		if(iCommand == CMD_GET_STATE)
		{
			// printf("Worker: responding to CMD_GET_STATE\n");
			float fProgress = alg.Progress();
			pr.WriteCommandResponseOK(iCommand);
			bool bFinished = alg.GetFinished();
			pr.WriteCommandData(&bFinished, sizeof(bFinished));
			pr.WriteCommandData(&fProgress, sizeof(fProgress));
		}

		if(iCommand == CMD_GET_RESULT)
		{
			printf("Worker: responding to CMD_GET_RESULT\n");
			/* Don't request the result until we've said we're done. */
			if(!alg.GetFinished())
			{
				pr.WriteCommandResponseError("Received CMD_GET_RESULT while still working");
			}
			else
			{
				pr.WriteCommandResponseOK(iCommand);
				pr.WriteCommandData(SourceImage.m_pData, SourceImage.m_iStrideBytes * SourceImage.m_iHeight);
			}
		}

		if(iCommand == CMD_RESET)
		{
			fprintf(stderr, "%.3f: Worker: responding to CMD_RESET\n", gettime());
			alg.Abort();
			fprintf(stderr, "%.3f: Worker: CMD_RESET finished\n", gettime());
			pr.WriteCommandResponseOK(iCommand);
		}

		if(iCommand == CMD_SHUTDOWN)
		{
			printf("Worker: responding to CMD_SHUTDOWN\n");
			alg.Abort();
			bShutdown = true;

			pr.WriteCommandResponseOK(iCommand);
		}
	}

	printf("Worker: shutting down\n");
}

void RunServer()
{
	/* Our control pipes are on stdin and stdout.  Move them to other FDs, so we can
	 * use stdout normally. */
	int iInFD = dup(fileno(stdin));
	int iOutFD = dup(fileno(stdout));

	setmode(iInFD, O_BINARY);
	setmode(iOutFD, O_BINARY);

	freopen("NUL","rb", stdin);
	freopen("NUL","wb", stdout);
	freopen("NUL","wb", stderr);

	freopen("CONIN$","rb", stdin);
	freopen("CONOUT$","wb", stdout);
	freopen("CONOUT$","wb", stderr);

	ServerMainLoop(iInFD, iOutFD);
	close(iInFD);
	close(iOutFD);
}

int main(int argc, char *argv[])
{
	if(argc != 2 || strcmp(argv[1], "--server"))
	{
		printf("This program is invoked automatically and should not be run directly.\n");
		exit(1);
	}

	CrashHandlerInit();
	RunServer();
}
