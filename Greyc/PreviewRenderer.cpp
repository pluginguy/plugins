#include "PreviewRenderer.h"

/*
 * Update the processed image from the current UI settings.
 *
 * The UI needs to be able to toggle between the processed and unprocessed image quickly,
 * without reprocessing when returning to the processed view, and to scroll around the
 * unprocessed image efficiently.
 *
 * Keep the whole unprocessed image in memory, and leave it unchanged; this can be used
 * for all unprocessed views, and for previewing processed views.  We don't use this
 * directly for final image processing, because we should be able to support 16-bit
 * processing at some point.
 */

bool PreviewRenderer::RenderedPreview::SamePosition(const RenderedPreview &si) const
{
	if(PreviewImage.Empty())
		return false;
	return
		this->PreviewImage.m_iWidth == si.PreviewImage.m_iWidth &&
		this->PreviewImage.m_iHeight == si.PreviewImage.m_iHeight &&
		this->iX == si.iX &&
		this->iY == si.iY;
}

PreviewRenderer::PreviewRenderer()
{
	m_pAlgorithm.reset(new AlgorithmRemote);
	m_bAlgorithmPending = false;
}

PreviewRenderer::~PreviewRenderer()
{
}

/* Configure preview for the requested preview settings, but don't actually run the preview. */
void PreviewRenderer::SetupPreview(RenderedPreview &preview) const
{
	int iWidth = Image.PreviewImage.m_iWidth, iHeight = Image.PreviewImage.m_iHeight;
	preview.iX = Image.iX;
	preview.iY = Image.iY;
	preview.PreviewImage.Alloc(iWidth, iHeight, 1, 4);

	/* The region within Preview that's actually being displayed: */
	preview.PreviewRegion.top = Image.iY - preview.iY;
	preview.PreviewRegion.left = Image.iX - preview.iX;
	preview.PreviewRegion.bottom = min(preview.PreviewRegion.top + iHeight, iHeight);
	preview.PreviewRegion.right = min(preview.PreviewRegion.left + iWidth, iWidth);
	preview.PreviewOffset.h = Image.iX - preview.iX;
	preview.PreviewOffset.v = Image.iY - preview.iY;

	preview.Settings = FilterSettings;
	preview.Options = FilterOptions;

	switch(FilterOptions.m_DisplayMode)
	{
	case AlgorithmOptions::DISPLAY_SINGLE:
		break;
	case AlgorithmOptions::DISPLAY_INSIDE:
	{
		/* Move the active preview region inwards. */
		int iXChop = (preview.PreviewRegion.bottom - preview.PreviewRegion.top) / 8;
		int iYChop = (preview.PreviewRegion.right - preview.PreviewRegion.left) / 8;
		preview.PreviewRegion.top += iYChop;
		preview.PreviewRegion.bottom -= iYChop;
		preview.PreviewRegion.left += iXChop;
		preview.PreviewRegion.right -= iXChop;
		preview.PreviewOffset.h += iXChop;
		preview.PreviewOffset.v += iYChop;
		break;
	}
	case AlgorithmOptions::DISPLAY_SIDE_BY_SIDE:
	{
		/* Trim off the left half of the preview region. */
		int iXChop = (preview.PreviewRegion.right - preview.PreviewRegion.left) / 2;
		preview.PreviewRegion.left += iXChop;
		preview.PreviewOffset.h += iXChop;
		break;
	}
	}
}

/* XXX: get rid of this dependency */
#include "Photoshop.h"
#include "PhotoshopHelpers.h"
extern FilterRecord *gFilterRecord;

void PreviewRenderer::SetPreviewPosition(int iX, int iY, int iWidth, int iHeight)
{
	ScaleArea(AlgorithmRemote::GetRequiredOverlapFactor(), iX, iY, iWidth, iHeight);

	/* Clamp bounds. */
	iX = clamp(iX, 0, Source.m_iWidth);
	iY = clamp(iY, 0, Source.m_iHeight);
	iWidth = clamp(iWidth, 0, Source.m_iWidth - iX);
	iHeight = clamp(iHeight, 0, Source.m_iHeight - iY);

	/* If there's an overlap between the old position and the new position, optimize by
	 * moving the old data to the new position and only blitting in the changed data. */
	int iFromX = 0, iFromY = 0;
	int iToX = 0, iToY = 0;
	if(iX < Image.iX)	iToX += Image.iX - iX;
	if(iX > Image.iX)	iFromX += iX - Image.iX;
	if(iY < Image.iY)	iToY += Image.iY - iY;
	if(iY > Image.iY)	iFromY += iY - Image.iY;

	CImg temp;
	temp.Alloc(iWidth, iHeight, 1, 4);

	/* Copy the area within the image that's moved.  This usually covers most of the image, and
	 * it's much faster since we don't need to do any conversions. */
	int iMoveWidth = min(Image.PreviewImage.m_iWidth, iWidth) - iToX - iFromX;
	int iMoveHeight = min(Image.PreviewImage.m_iHeight, iHeight) - iToY - iFromY;
	iMoveWidth = max(0, iMoveWidth);
	iMoveHeight = max(0, iMoveHeight);

	for(int y = 0; y < iMoveHeight; ++y)
	{
		const uint8_t *pFrom = Image.PreviewImage.ptr(iFromX, iFromY+y);
		uint8_t *pTo = temp.ptr(iToX, iToY+y);
		memmove(pTo, pFrom, iMoveWidth * Image.PreviewImage.m_iBytesPerChannel * Image.PreviewImage.m_iChannels);
	}

	/* Copy the edges; this part needs to be converted.  Top edge:*/
	if(iToY > 0)
	{
		CImg SrcRegion;
		SrcRegion.Hold(Source, iX, iY, iWidth, iToY);
		CImg DestRegion;
		DestRegion.Hold(temp, 0, 0, iWidth, iToY);
		SetFromImage(gFilterRecord, SrcRegion, DestRegion);
	}

	/* Top edge: */
	if(iMoveHeight + iToY < iHeight)
	{
		int iYToCopy = iMoveHeight + iToY;
		CImg SrcRegion;
		SrcRegion.Hold(Source, iX, iY + iYToCopy, iWidth, iHeight - iYToCopy);
		CImg DestRegion;
		DestRegion.Hold(temp, 0, iYToCopy, iWidth, iHeight - iYToCopy);
		SetFromImage(gFilterRecord, SrcRegion, DestRegion);
	}

	/* Left edge: */
	if(iToX > 0)
	{
		CImg SrcRegion;
		SrcRegion.Hold(Source, iX, iY, iToX, iHeight);
		CImg DestRegion;
		DestRegion.Hold(temp, 0, 0, iToX, iHeight);
		SetFromImage(gFilterRecord, SrcRegion, DestRegion);
	}

	/* Right edge: */
	if(iMoveWidth + iToX < iWidth)
	{
		int iXToCopy = iMoveWidth + iToX;
		CImg SrcRegion;
		SrcRegion.Hold(Source, iX + iXToCopy, iY, iWidth - iXToCopy, iHeight);
		CImg DestRegion;
		DestRegion.Hold(temp, iXToCopy, 0, iWidth - iXToCopy, iHeight);
		SetFromImage(gFilterRecord, SrcRegion, DestRegion);
	}

	temp.Swap(Image.PreviewImage);
	Image.iX = iX;
	Image.iY = iY;
}

/* Allocate and update Preview. */
void PreviewRenderer::UpdateProxyBuffer(auto_ptr<AlgorithmRemote::Callbacks> pCallbacks)
{
	RenderedPreview NewPreview;
	SetupPreview(NewPreview);

	/* If the existing (or currently-loading) preview is already up to date, stop. */
	if(CurrentPreview == NewPreview)
		return;

	/* If we're already running, abort. */
	if(m_bAlgorithmPending)
	{
		printf("%.3f: Stopping preview renderer\n", gettime());
		m_pAlgorithm->Abort();
		printf("%.3f: Stopped\n", gettime());
		
		/* This preview didn't finish, so free it. */
		CurrentPreview.PreviewImage.Free();
	}

	/* If this preview is the last one we generated, just switch to it. */
	if(LastPreview == NewPreview)
	{
		LastPreview.Swap(CurrentPreview);
		return;
	}

	/* If the last preview (if any) is at the same position as the one we're rendering
	 * now, then keep it around so the UI can render it until we finish rendering this
	 * one. */
	if(CurrentPreview.SamePosition(NewPreview))
	{
		LastPreview.PreviewImage.Free();
		LastPreview.Swap(CurrentPreview);
	}
	else if(!LastPreview.PreviewImage.Empty())
	{
		/* We still have a LastPreview, which means we interrupted processing before it finished
		 * and started a new one.  Keep LastPreview until the new processing finishes. */
	}

	CurrentPreview.Swap(NewPreview);

	if(!CurrentPreview.SamePosition(LastPreview))
	{
		/* We have an old preview, but the new preview is in a new location.
		 * Clear the old preview; it's not valid as a temporary display. */
		this->LastPreview.PreviewImage.Free();
	}

	//CurrentPreview.PreviewImage.Alloc(CurrentPreview.PreviewImage.m_iWidth, CurrentPreview.PreviewImage.m_iHeight, Image.PreviewImage.m_iBytesPerChannel, Image.PreviewImage.m_iChannels);
	/* Copy the original image for the new preview region. */
	CurrentPreview.PreviewImage.CopyFrom(Image.PreviewImage, 0, 0, 0, 0, CurrentPreview.PreviewImage.m_iWidth, CurrentPreview.PreviewImage.m_iHeight);

	/* Duplicate PreviewImage into FilteringBuf; this is what we'll give to Algorithm. */
	CurrentPreview.FilteringBuf.Alloc(CurrentPreview.PreviewImage);
	memcpy(CurrentPreview.FilteringBuf.m_pData, CurrentPreview.PreviewImage.m_pData, CurrentPreview.PreviewImage.m_iStrideBytes * CurrentPreview.PreviewImage.m_iHeight);

	/* Give FilteringBuf to m_pAlgorithm for processing. */
	m_pAlgorithm->SetTarget(CurrentPreview.FilteringBuf);
	m_pAlgorithm->SetCallbacks(pCallbacks);


	/*
	 * iPreviewX, iPreviewY, iWantedPreviewWidth and iWantedPreviewHeight are the dimensions
	 * the UI needs available to render.
	 *	 
	 * We'll actually render more than this, to reduce artifacts around the edge.  The actual
	 * coordinates of the rendered preview are stored in iPreviewRenderedX,
	 * iPreviewRenderedY.
	 *
	 * We don't care about zooming; that's handled by the UI.
	 */
	 
	// XXX: the preview should blend alpha masks, not just 0/1 it

	/* Mask out anything outside the preview, in addition to anything the user
	 * has masked out. */
	// CurrentPreview.FilteringMask.Assign(SourceMask);
	// CurrentPreview.FilteringMask.MaskRegion(CurrentPreview.iY, CurrentPreview.iY + CurrentPreview.PreviewImage.m_iHeight, CurrentPreview.iX, CurrentPreview.iX + CurrentPreview.PreviewImage.m_iWidth);
	
	// m_pAlgorithm->CopyMaskFrom(pMaskData.get(), iMaskStride);

	/* Start the preview filter. */
	m_pAlgorithm->GetSettings() = FilterSettings;
	m_pAlgorithm->GetOptions() = FilterOptions;

	/* Always preview with all threads. */
	m_pAlgorithm->GetOptions().nb_threads = 0;
	printf("%.3f: Starting preview renderer\n", gettime());

	m_pAlgorithm->Run();
	m_bAlgorithmPending = true;
}

bool PreviewRenderer::IsPreviewRunning() const
{
	return m_bAlgorithmPending;
}

/* If the preview is updating and is complete, copy the finished image.  Return false if the
 * preview is still running, true if the preview is not running or has finished. */
bool PreviewRenderer::CheckPreviewCompletion(float &fPercentDone)
{
	if(!m_bAlgorithmPending)
		return true;

	m_pAlgorithm->UpdateState();

	fPercentDone = m_pAlgorithm->Progress();

	if(!m_pAlgorithm->GetFinished())
		return false;

	m_bAlgorithmPending = false;

	if(m_pAlgorithm->GetError(m_sError))
	{
		printf("Error: %s\n", m_sError.c_str());
		return true;
	}

	/* Retrieve the finished data from m_pAlgorithm. */
	int iXInOffset = CurrentPreview.PreviewRegion.left;
	int iYInOffset = CurrentPreview.PreviewRegion.top;
	int iXOutOffset = CurrentPreview.PreviewOffset.h;
	int iYOutOffset = CurrentPreview.PreviewOffset.v;
	int iWidth = CurrentPreview.PreviewRegion.right - CurrentPreview.PreviewRegion.left + 1;
	int iHeight = CurrentPreview.PreviewRegion.bottom - CurrentPreview.PreviewRegion.top + 1;
	m_pAlgorithm->Finalize();
	CurrentPreview.PreviewImage.CopyFrom(CurrentPreview.FilteringBuf, iXInOffset, iYOutOffset, iXOutOffset, iYOutOffset, iWidth, iHeight);
	printf("%.3f: finished\n", gettime());
	return true;
}
