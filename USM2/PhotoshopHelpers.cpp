#pragma warning (disable : 4800) // 'int' : forcing value to bool 'true' or 'false' (performance warning)

#include "PhotoshopHelpers.h"
#include "CImgI.h"
#include <map>

VPoint GetImageSize(FilterRecord *pFilterRecord)
{
	VPoint ret = { 0, 0 };

	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		ret.h = pFilterRecord->bigDocumentData->imageSize32.h;
		ret.v = pFilterRecord->bigDocumentData->imageSize32.v;
	}
	else
	{
		ret.h = pFilterRecord->imageSize.h;
		ret.v = pFilterRecord->imageSize.v;
	}

	return ret;
}

VRect GetFilterRect(FilterRecord *pFilterRecord)
{
	VRect ret = { 0, 0, 0, 0};
	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		ret.right = pFilterRecord->bigDocumentData->filterRect32.right;
		ret.top = pFilterRecord->bigDocumentData->filterRect32.top;
		ret.left = pFilterRecord->bigDocumentData->filterRect32.left;
		ret.bottom = pFilterRecord->bigDocumentData->filterRect32.bottom;
	}
	else
	{
		ret.right = pFilterRecord->filterRect.right;
		ret.top = pFilterRecord->filterRect.top;
		ret.left = pFilterRecord->filterRect.left;
		ret.bottom = pFilterRecord->filterRect.bottom;
	}
	
	return ret;
}

void SetInRect(FilterRecord *pFilterRecord, VRect inRect)
{
	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		pFilterRecord->bigDocumentData->inRect32.right = inRect.right;
		pFilterRecord->bigDocumentData->inRect32.top = inRect.top;
		pFilterRecord->bigDocumentData->inRect32.left = inRect.left;
		pFilterRecord->bigDocumentData->inRect32.bottom = inRect.bottom;
	}
	else
	{
		pFilterRecord->inRect.right = (int16)inRect.right;
		pFilterRecord->inRect.top = (int16)inRect.top;
		pFilterRecord->inRect.left = (int16)inRect.left;
		pFilterRecord->inRect.bottom = (int16)inRect.bottom;
	}
}

void SetOutRect(FilterRecord *pFilterRecord, VRect inRect)
{
	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		pFilterRecord->bigDocumentData->outRect32.right = inRect.right;
		pFilterRecord->bigDocumentData->outRect32.top = inRect.top;
		pFilterRecord->bigDocumentData->outRect32.left = inRect.left;
		pFilterRecord->bigDocumentData->outRect32.bottom = inRect.bottom;
	}
	else
	{
		pFilterRecord->outRect.right = (int16)inRect.right;
		pFilterRecord->outRect.top = (int16)inRect.top;
		pFilterRecord->outRect.left = (int16)inRect.left;
		pFilterRecord->outRect.bottom = (int16)inRect.bottom;
	}
}

void SetMaskRect(FilterRecord *pFilterRecord, VRect inRect)
{
	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		pFilterRecord->bigDocumentData->maskRect32.right = inRect.right;
		pFilterRecord->bigDocumentData->maskRect32.top = inRect.top;
		pFilterRecord->bigDocumentData->maskRect32.left = inRect.left;
		pFilterRecord->bigDocumentData->maskRect32.bottom = inRect.bottom;
	}
	else
	{
		pFilterRecord->maskRect.right = (int16)inRect.right;
		pFilterRecord->maskRect.top = (int16)inRect.top;
		pFilterRecord->maskRect.left = (int16)inRect.left;
		pFilterRecord->maskRect.bottom = (int16)inRect.bottom;
	}
}

/* Retrieve the entire source image and mask from PS, and load them by reference into Source and SourceMask. */
void InitSourceImage(FilterRecord *pFilterRecord, CImg &Source, CImg &SourceMask)
{
	VPoint size = GetImageSize(pFilterRecord);
	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		size.h = pFilterRecord->bigDocumentData->imageSize32.h;
		size.v = pFilterRecord->bigDocumentData->imageSize32.v;
	}
	else
	{
		size.h = pFilterRecord->imageSize.h;
		size.v = pFilterRecord->imageSize.v;
	}

	VRect filterRect;
	filterRect.top = 0;
	filterRect.left = 0;
	filterRect.right = size.h;
	filterRect.bottom = size.v;
	SetOutRect(pFilterRecord, filterRect);
	SetMaskRect(pFilterRecord, filterRect);

	pFilterRecord->inputRate = (int32)1 << 16; // 1
	pFilterRecord->maskRate = (int32)1 << 16; // 1
	pFilterRecord->outLoPlane = pFilterRecord->inLoPlane = 0;
	pFilterRecord->outHiPlane = pFilterRecord->inHiPlane = pFilterRecord->planes - 1;
	pFilterRecord->inputPadding = 255;
	pFilterRecord->maskPadding = 255;

	EnterCriticalSection(&g_PSCritSec);
	int iRet = pFilterRecord->advanceState();
	LeaveCriticalSection(&g_PSCritSec);
	if(iRet != noErr) throw PhotoshopErrorException(iRet);

	int iWidth = size.h;
	int iHeight = size.v;
	Source.Hold((uint8_t *) pFilterRecord->outData, iWidth, iHeight, pFilterRecord->depth/8, pFilterRecord->planes, pFilterRecord->outRowBytes);

	if(pFilterRecord->maskData)
	{
		/* We have a real mask, so copy it. */
		SourceMask.Alloc(size.h, size.v, 1, 1, pFilterRecord->maskRowBytes);
		memcpy(SourceMask.m_pData, pFilterRecord->maskData, SourceMask.m_iStrideBytes*size.v);
	}
	else
	{
		/* We have no mask, so create our own. */
		SourceMask.Alloc(size.h, size.v, 1, 1);
		memset(SourceMask.m_pData, 0xFF, SourceMask.m_iStrideBytes*size.v);
	}

	/* If the region was set as a rectangle, it's reflected in GetFilterRect() rather than the
	 * filter mask, so we have to copy it over. */
	VRect MarqueeRect;
	if(pFilterRecord->bigDocumentData && pFilterRecord->bigDocumentData->PluginUsing32BitCoordinates)
	{
		MarqueeRect.top = pFilterRecord->bigDocumentData->filterRect32.top;
		MarqueeRect.bottom = pFilterRecord->bigDocumentData->filterRect32.bottom;
		MarqueeRect.left = pFilterRecord->bigDocumentData->filterRect32.left;
		MarqueeRect.right = pFilterRecord->bigDocumentData->filterRect32.right;
	}
	else
	{
		MarqueeRect.top = pFilterRecord->filterRect.top;
		MarqueeRect.bottom = pFilterRecord->filterRect.bottom;
		MarqueeRect.left = pFilterRecord->filterRect.left;
		MarqueeRect.right = pFilterRecord->filterRect.right;
	}
	SourceMask.MaskRegion(MarqueeRect.top, MarqueeRect.bottom, MarqueeRect.left, MarqueeRect.right);
}

/* Convert from filter data to a BGRA image suitable for display with StretchDIBits. */
static void ConvertFromFilterImage(FilterRecord *pFilterRecord, const CImg &Temp, CImg &Dest)
{
	/*
	 * Converting for preview is annoying.  There are a lot of cases, PS doesn't provide any
	 * color-conversion (other than one-color-at-a-time color services, which is useless here),
	 * and the documentation doesn't list the ways these may be used, so the only way to
	 * know which combinations we might need to preview is testing.
	 *
	 * There are five possible channel types:
	 *
	 * inLayerPlanes, inTransparencyMask, inLayerMasks, inInvertedLayerMasks, inNonLayerPlanes
	 *
	 * The interleaved channel data is in this order.  Color data might be included in inLayerPlanes
	 * (for layers) or inNonLayerPlanes (for the background).
	 *
	 * Alpha channels on the background are ambiguous; they're both included in inNonLayerPlanes.
	 * We'd probably need to dig into documentInfo to figure that out.
	 *
	 * First, count the number of channels from the beginning to treat as color data, and figure
	 * out if we have a channel we're treating as an alpha mask.
	 *
	 * If we have inLayerPlanes, treat those planes as color channels; we may have 1 (monochrome),
	 * 3 (eg. RGB, LAB) or 4 (eg. CMYK).  This may be followed by an alpha channel if inTransparencyMask
	 * is set.
	 */
	int iNumColorChannels = 0;
	bool bHasAlphaChannel = false;
	if(pFilterRecord->inLayerPlanes > 0)
	{
		/* We have inLayerPlanes.  Count them as the color channels, check inTransparencyMask
		 * for an alpha channel, and ignore any channels after.  We probably won't have any
		 * inNonLayerPlanes. */
		iNumColorChannels = pFilterRecord->inLayerPlanes;
		bHasAlphaChannel = pFilterRecord->inTransparencyMask > 0;
	}
	else if(pFilterRecord->inNonLayerPlanes > 0 &&
		(pFilterRecord->inLayerPlanes + pFilterRecord->inTransparencyMask + pFilterRecord->inLayerMasks + pFilterRecord->inInvertedLayerMasks) == 0)
	{
		/* We have inNonLayerPlanes, and nothing else.  Treat them as color channels. */
		iNumColorChannels = pFilterRecord->inNonLayerPlanes;
		bHasAlphaChannel = false;
	}
	else
	{
		/* We have neither, or all of the values are 0.  The docs say that all of these may
		 * be 0 (without giving any examples of when to test it); in that case, just treat
		 * all planes as color channels. */
		iNumColorChannels = pFilterRecord->planes;
	}

	if(iNumColorChannels == 1)
	{
		/* If we have only one color channel, render it as monochrome.  This may be a selected
		 * layer mask.  We might have an alpha channel. */
		Blit8bY_8bBGRA(Temp.m_pData, Dest.m_pData,
			Dest.m_iWidth, Dest.m_iHeight,
			Temp.m_iChannels,
			bHasAlphaChannel,
			Temp.m_iStrideBytes, Dest.m_iStrideBytes);
	}
	else if(iNumColorChannels == 3)
	{
		/* If we have three color channels, treat it as RGB.  This may also be a less
		 * common color mode, like LAB. */
		if(bHasAlphaChannel)
			Blit8bRGBA_8bBGRA(Temp.m_pData, Dest.m_pData,
				Dest.m_iWidth, Dest.m_iHeight,
				Temp.m_iChannels,
				Temp.m_iStrideBytes, Dest.m_iStrideBytes);
		else
			Blit8bRGB_8bBGRA(Temp.m_pData, Dest.m_pData,
				Dest.m_iWidth, Dest.m_iHeight,
				Temp.m_iChannels,
				Temp.m_iStrideBytes, Dest.m_iStrideBytes);
	}
	else
	{
		/* We have something else.  This might be CMYK (iNumColorChannels == 4); it might be
		 * a monochrome background with an alpha channel (which shows up as inNonLayerPlanes == 2).
		 */
	}
}

/* img is the source image, stored by InitSourceImage.  Convert the image to a format
 * suitable for previewing. */
void SetFromImage(FilterRecord *pFilterRecord, const CImg &img, CImg &dest)
{
	/* Copy the whole image, converted to 8-bit BGRA. */
	CImg Temp;
	if(img.m_iBytesPerChannel == 1)
	{
		Temp.Hold(img);
	}
	else if(img.m_iBytesPerChannel == 2)
	{
		Temp.Alloc(img);

		Blit16b_8b((uint16_t *) img.m_pData, Temp.m_pData,
			Temp.m_iWidth, Temp.m_iHeight,
			img.m_iChannels,
			img.m_iStrideBytes, Temp.m_iStrideBytes);
	}

	dest.Alloc(Temp.m_iWidth, Temp.m_iHeight, 1, 4);

	ConvertFromFilterImage(pFilterRecord, Temp, dest);
}

/* This lock is used for all calls to PS APIs. */
CRITICAL_SECTION g_PSCritSec;

/* Init or destroy the PS API lock.  Since we need PS APIs to allocate memory, memory allocation
 * hasn't been set up yet, so this needs to do nothing but init the lock. */
void PhotoshopInitLock()
{
	InitializeCriticalSection(&g_PSCritSec);
}

void PhotoshopFreeLock()
{
	DeleteCriticalSection(&g_PSCritSec);
}

/*
 * Photoshop makes threading a massive pain.
 *
 * Its APIs aren't threadsafe, so we need to serialize all calls to PS.  Subtly, this also means
 * we need to serialize with Photoshop itself when it receives window messages.
 *
 * To do this takes a serious hack: we're going to take over all windows in the thread, and lock
 * g_PSCritSec while they're running.  When we're done, we'll restore it.
 *
 * We don't want to do this for our own window, so we run AddWindowThreadLocks before creating
 * it.
 */

static map<HWND, WNDPROC> *pWinProcs;

static LRESULT LockWindowProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC hOldWndProc = (*pWinProcs)[hWnd];

	EnterCriticalSection(&g_PSCritSec);
	LRESULT ret = CallWindowProc(hOldWndProc, hWnd, wMsg, wParam, lParam);
	LeaveCriticalSection(&g_PSCritSec);
	return ret;
}

static BOOL CALLBACK GetChildWndProcs(HWND hWnd, LPARAM lParam)
{
	(*pWinProcs)[hWnd] = (WNDPROC) GetWindowLongPtr(hWnd, GWLP_WNDPROC);
	return TRUE;
}

static BOOL CALLBACK GetThreadWndProcs(HWND hWnd, LPARAM lParam)
{
	(*pWinProcs)[hWnd] = (WNDPROC) GetWindowLongPtr(hWnd, GWLP_WNDPROC);

	EnumChildWindows(hWnd, GetChildWndProcs, NULL);
	return TRUE;
}

void AddWindowThreadLocks()
{
	pWinProcs = new map<HWND, WNDPROC>;
	EnumThreadWindows(GetCurrentThreadId(), GetThreadWndProcs, NULL);
	for(map<HWND, WNDPROC>::iterator it = pWinProcs->begin();
		it != pWinProcs->end(); ++it)
	{
		HWND hWnd = it->first;
		WNDPROC hOldWndProc = it->second;
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR) LockWindowProc);
	}

}

void FreeWindowThreadLocks()
{
	for(map<HWND, WNDPROC>::iterator it = pWinProcs->begin();
		it != pWinProcs->end(); ++it)
	{
		HWND hWnd = it->first;
		WNDPROC hOldWndProc = it->second;
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR) hOldWndProc);
	}

	delete pWinProcs;
	pWinProcs = NULL;
}
