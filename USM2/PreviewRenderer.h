#ifndef PREVIEW_RENDERER_H
#define PREVIEW_RENDERER_H

#include "CImgI.h"
#include "Algorithm.h"

struct PRRect
{
	int top, left, bottom, right;
};

struct PRPoint
{
	int v, h;
};

struct PreviewRenderer
{
	struct RenderedPreview
	{
		RenderedPreview() { iX = iY = 0; }
		void Swap(RenderedPreview &rhs)
		{
			PreviewImage.Swap(rhs.PreviewImage);
			FilteringBuf.Swap(rhs.FilteringBuf);
			swap(rhs.Settings, Settings);
			swap(rhs.Options, Options);
			swap(rhs.iX, iX);
			swap(rhs.iY, iY);
			swap(rhs.PreviewRegion, PreviewRegion);
			swap(rhs.PreviewOffset, PreviewOffset);
		}

		bool operator==(const RenderedPreview &rhs) const
		{
			return SamePosition(rhs) && 
				!memcmp(&Settings, &rhs.Settings, sizeof(Settings)) &&
				!memcmp(&Options, &rhs.Options, sizeof(Options));
		}

		bool SamePosition(const RenderedPreview &si) const;
		CImg PreviewImage;

		/* This holds a copy of PreviewImage while it's being processed, so we can selectively
		 * copy the results. */
		CImg FilteringBuf;
		Algorithm::Settings Settings;
		Algorithm::Options Options;
		int iX, iY;		/* Top-left coordinate within the full image */

		/* The region within Preview that's actually being displayed. */
		PRRect PreviewRegion;

		/* The offset within the preview where Preview will be displayed; the rest will
		 * show the original image. */
		PRPoint PreviewOffset;

	private:
		RenderedPreview &operator=(const RenderedPreview &rhs);
	};

	PreviewRenderer();
	~PreviewRenderer();
	void SetPreviewPosition(int iX, int iY, int iPreviewWidth, int iPreviewHeight);
	void UpdateProxyBuffer(auto_ptr<Algorithm::Callbacks> pCallbacks);
	bool IsPreviewRunning() const;
	bool CheckPreviewCompletion(float &fPercentDone);
	void SetupPreview(RenderedPreview &preview) const;

	/* The size of the actual preview widget: */
	int iWantedPreviewX;
	int iWantedPreviewY;
	int iWantedPreviewWidth;
	int iWantedPreviewHeight;

	/* Processed preview image and original.  iX and iY will be <= iPreviewX, iPreviewY. */
	RenderedPreview CurrentPreview;

	/* Unprocessed image. */
	RenderedPreview Image;
	CImg Source;
	CImg SourceMask;

	string m_sError;

	/* The last processed preview image.  This is available while Preview.pImage is NULL
	 * and we're processing a new preview. */
	RenderedPreview LastPreview;

	/* Preview processor running in the background, if any. */
	auto_ptr<Algorithm> m_pAlgorithm;
	bool m_bAlgorithmPending;

	/* The current and previous settings in the UI.  These aren't necessarily applied. */
	Algorithm::Settings CurrentSettings;
	Algorithm::Settings LastSettings;
	Algorithm::Options CurrentOptions;

	/* The options which are actually applied, and the final results. */
	Algorithm::Settings FilterSettings;
	Algorithm::Options FilterOptions;
};

#endif
