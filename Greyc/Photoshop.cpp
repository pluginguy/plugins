/* The high-level interface to the Photoshop filter API and the main entry point. */

#include "Photoshop.h"
#include "PhotoshopHelpers.h"
#include "UI.h"
#include "Settings.h"
#include "Memory.h"
#include "Helpers.h"
#include "CrashReporting.h"
#include "PreviewRenderer.h"
#include <time.h>
#include <memory>

FilterRecord *gFilterRecord = NULL;
SPBasicSuite *sSPBasic = NULL;

typedef unsigned __int64 uint64_t;


static void Run()
{
	CImg Source, SourceMask;
	InitSourceImage(gFilterRecord, Source, SourceMask);

	Parameters params;
	params.Init();

	ReadRegistryParameters(params);
	ReadScriptParameters(params);

	/* Run the UI if we've been told to. */
	bool bApplyToImage = true;
	if(!gFilterRecord->descriptorParameters || gFilterRecord->descriptorParameters->playInfo == plugInDialogDisplay)
	{
		PreviewRenderer pr;
		pr.Source.Hold(Source);
		pr.SourceMask.Hold(SourceMask);
		pr.FilterSettings = params.FilterSettings;
		pr.FilterOptions = params.FilterOptions;
printf("%.3f: UI\n", gettime());
		UIResult ret = DoUI(&pr);
		pr.m_pAlgorithm->Abort();
		pr.Image.PreviewImage.Free();

		if(ret == UI_CANCEL)
		{
			/* The user cancelled. */
			throw PhotoshopErrorException(userCanceledErr);
		}
		if(ret == UI_SAVE_ONLY)
			bApplyToImage = false;

		params.FilterSettings = pr.FilterSettings;
		params.FilterOptions = pr.FilterOptions;

		WriteRegistryParameters(params);
	}

	WriteScriptParameters(params);

	if(!bApplyToImage)
		return;

	/* Run the main filter. */
	auto_ptr<AlgorithmRemote> pAlgo(new AlgorithmRemote);
	pAlgo->SetTarget(Source);
	pAlgo->SetMask(SourceMask);

	pAlgo->GetSettings() = params.FilterSettings;
	pAlgo->GetOptions() = params.FilterOptions;
	if(Source.m_iBytesPerChannel == 2)
		pAlgo->GetSettings().m_fInputScale = 255.0f / 32768.0f;
	else
		pAlgo->GetSettings().m_fInputScale = 1.0f;

	if(!ShowProgressDialog(pAlgo.get()))
		throw PhotoshopErrorException(userCanceledErr);

	string sError;
	if(pAlgo->GetError(sError))
		throw Exception(sError);
}

static int16 RunPlugin(const int16 iSelector)
{
	string sError;
	try
	{
		if(gFilterRecord->bigDocumentData != NULL)
			gFilterRecord->bigDocumentData->PluginUsing32BitCoordinates = true;

		switch(iSelector)
		{
		case filterSelectorParameters:	break;
		case filterSelectorPrepare:	break;
		case filterSelectorStart:
		{
			Run();
			printf("UI shutdown\n");
			break;
		}
		case filterSelectorContinue:
		{
			VRect zeroRect = { 0, 0, 0, 0 };

			SetInRect(gFilterRecord, zeroRect);
			SetOutRect(gFilterRecord, zeroRect);
			SetMaskRect(gFilterRecord, zeroRect);
			break;
		}
		case filterSelectorFinish:
			break;
		}
	} catch(const std::bad_alloc &) {
		return memFullErr;
	} catch(const PhotoshopErrorException &e) {
		return e.m_iErr;
	} catch(const exception &e) {
		/* Don't let any exceptions propagate back into Photoshop. */
		sError = e.what();
	} catch(...) {
		sError = "Unknown exception";
	}

	if(sError.empty())
		return noErr;

	PlatformData *pPlatform = (PlatformData *) gFilterRecord->platformData;
	HWND hWnd = (HWND) pPlatform->hwnd;
	MessageBox(hWnd, sError.c_str(), plugInName, MB_OK);

	/* Is it OK to return userCanceledErr for a non-cancellation error?  The only
	 * difference seems to be that this doesn't display a "couldn't be completed because
	 * of a program error" dialog, which is what we want since we already displayed
	 * the error. */
	return userCanceledErr;
}

DLLExport MACPASCAL void PluginMain(const int16 iSelector, void *pFilterRecord, int32 *pData, int16 *pResult)
{
	CrashHandlerInit();

	if(ThisProcessHasFocus() && (GetKeyState(VK_LCONTROL) & 0x8000) && (GetKeyState(VK_LSHIFT) & 0x8000))
	{
		AllocConsole();
		freopen("CONOUT$", "wb", stdout);
		freopen("CONOUT$", "wb", stderr);
	}

	gFilterRecord = (FilterRecord *) pFilterRecord;

	if(iSelector == filterSelectorAbout)
		sSPBasic = ((AboutRecord*)gFilterRecord)->sSPBasic;
	else
		sSPBasic = gFilterRecord->sSPBasic;

	/* Don't allocate any non-POD temporaries in this function; we deallocate our heap in MemoryFree. */
	if(iSelector == filterSelectorAbout)
		DoAbout();
	else
	{
		/* Initialize memory handling.  Don't do this in filterSelectorAbout; Photoshop crashes. */
		MemoryInit();
		*pResult = RunPlugin(iSelector);
		MemoryFree();
	}

	/* Restore the old exception handler before returning to Photoshop. */
	CrashHandlerRemove();
}

extern "C" BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	g_hInstance = (HINSTANCE) hModule;
	return true;
}

void Parameters::Init()
{
	FilterSettings = AlgorithmSettings();
	FilterOptions = AlgorithmOptions();
}
