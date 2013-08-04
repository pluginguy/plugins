#include "CImgI.h"
#include "Helpers.h"
#include <algorithm>
using namespace std;

CImgF::CImgF(const CImgF &rhs)
{
	width = height = dim = stride = 0;
	data = NULL;
	real_data = NULL;
	owned = true;
	assign(rhs);
}

CImgF::CImgF()
{
	width = height = dim = stride = 0;
	data = NULL;
	real_data = NULL;
	owned = true;
}

void CImgF::free()
{
	if(owned)
		delete[] real_data;
	width = height = dim = stride = 0;
	data = NULL;
	real_data = NULL;
}

CImgF::~CImgF()
{
	free();
}

CImgF &CImgF::swap(CImgF &img)
{
	std::swap(width, img.width);
	std::swap(height, img.height);
	std::swap(dim, img.dim);
	std::swap(stride, img.stride);
	std::swap(data, img.data);
	std::swap(real_data, img.real_data);
	std::swap(owned, img.owned);

	return img;
}

float *CImgF::aligned_alloc(int iSize, void **pRealAlloc)
{
	/* Hack to guarantee memalign(n, 16) for SSE: */
	*pRealAlloc = new float[iSize + 16];
	uintptr_t iPtr = (uintptr_t) *pRealAlloc;
	iPtr += 0xF;
	iPtr &= ~0xF;
	return (float *) iPtr;
}

void CImgF::alloc(int iWidth, int iHeight, int iBytesPerPixel, int iStride)
{
	if(iStride == -1)
	{
		/* Make sure the stride is a multiple of 4, so the actual stride (in bytes) will remain
		 * a multiple of 16. */
		iStride = align(iWidth * iBytesPerPixel, 4);
	}

	int iSize = iHeight * iStride;
	if(iSize != size())
	{
		if(owned)
			delete[] real_data;
		data = NULL;
		real_data = NULL;
		data = aligned_alloc(iSize, &real_data);
	}

	width = iWidth;
	height = iHeight;
	dim = iBytesPerPixel;
	stride = iStride;
	owned = true;
}

void CImgF::scale(float fFactor)
{
	if(this->sse_compatible())
	{
		__m128 factor128 = _mm_load1_ps(&fFactor);
		cimgI_forY(*this, y)
		{
			__m128 *p = (__m128 *) this->ptr(0, y);
			cimgI_forX(*this, x)
			{
				__m128 val = _mm_mul_ps(*p, factor128);
				*p = val;
				++p;
			}
		}
	}
	else
	{
		cimgI_for(*this,ptr)
			*ptr *= fFactor;
	}
}

void CImgF::fill(float f)
{
	cimgI_for(*this,p)
		*p = f;
}

// Return a reference to the maximum pixel value and return also the minimum pixel value.
void CImgF::maxmin(float &fMin, float &fMax) const
{
	if(is_empty())
	{
		fMax = 0;
		fMin = 0;
		return;
	}
	const float *ptrmax = data;
	float max_value = *ptrmax, min_value = max_value;
	cimgI_forY(*this,y)
		cimgI_for_ptrXV_const(*this,y,ptr)
		{
			const float val = *ptr;
			if (val>max_value) { max_value = val; ptrmax = ptr; }
			if (val<min_value) min_value = val;
		}
	fMax = *ptrmax;
	fMin = min_value;
}

bool CImgF::sse_compatible() const
{
#if defined(_WIN64)
        return true;
#else
	if(!(GetCPUID() & CPUID_SSE))
		return false;
	if(dim != 4)
		return false;
	if(stride & 0x3) // not aligned
		return false;
	return true;
#endif
}

void CImgF::normalize(const float a, const float b)
{
	if(is_empty())
		return;
	float m, M;
	maxmin(m, M);

	const float fm = m, fM = M;
	if(m==M)
		return fill(0);
	if (m==a && M==b)
		return;
	cimgI_forY(*this,y)
		cimgI_for_ptrXV(*this,y,ptr)
			*ptr = (*ptr-fm)/(fM-fm)*(b-a)+a;
}

void CImgF::alias(const CImgF &img, int iX, int iY, int iWidth)
{
	width = img.width;
	height = img.height;
	dim = img.dim;
	stride = img.stride;
	data = img.data;
	real_data = NULL;
	owned = false;
}

void CImgF::assign(const float *pBuffer, int iWidth, int iHeight, int iChannels, int iStride)
{
	if(iStride == -1)
		iStride = align(iWidth * iChannels, 4);
	const int siz = iHeight*iStride;
	const int curr_siz = size();
	if(pBuffer+siz<data || pBuffer>=data+size())
	{
		alloc(iWidth, iHeight, iChannels, iStride);
		memcpy(data,pBuffer,siz*sizeof(float));
	} else {
		void *new_real_data;
		float *new_data = aligned_alloc(siz, &new_real_data);
		memcpy(new_data, pBuffer, siz*sizeof(float));
		delete[] real_data;
		data = new_data;
		real_data = new_real_data;
		width = iWidth; height = iHeight; dim = iChannels; stride = iStride;
	}

}

void CImgF::Hold(float *pBuffer, int iWidth, int iHeight, int iChannels, int iStride)
{
	free();
	data = pBuffer;
	real_data = NULL;
	width = iWidth;
	height = iHeight;
	dim = iChannels;
	stride = iStride;
	owned = false;
}

template<typename T>
void CopyFromBuf(CImgF &out, const T *pSource, int iSourceChannels, int iSourceStride,
	int iSourceX, int iSourceY, int iDestX, int iDestY,
	int iWidth, int iHeight)
{
	pSource += iSourceY * iSourceStride + iSourceX * iSourceChannels;

	const int iPlanesToCopy = min(out.dim, iSourceChannels);
	const int iInPlanesToSkip = iSourceChannels - iPlanesToCopy;
	const int iOutPlanesToSkip = out.dim - iPlanesToCopy;

	iWidth = min(iWidth, out.width - iDestX);
	iHeight = min(iHeight, out.height - iDestY);

        for(int y=0; y < iHeight; ++y)
	{
		const T *pIn = pSource + y*iSourceStride;
		float *pOut = out.ptr(iDestX, iDestY + y);
		for(int x = 0; x < iWidth; ++x)
		{
			for(int plane = 0; plane < iPlanesToCopy; ++plane)
			{
				*pOut = *pIn;
				++pIn;
				++pOut;
			}
			pIn += iInPlanesToSkip;
			pOut += iOutPlanesToSkip;
		}
	}
}

void CImgF::CopyFrom(const CImg &source, int iSourceX, int iSourceY, int iDestX, int iDestY, int iWidth, int iHeight)
{
	if(source.m_iBytesPerChannel == 1)
		::CopyFromBuf<uint8_t>(*this, (const uint8_t *) source.m_pData, source.m_iChannels, source.m_iStrideBytes, iSourceX, iSourceY, iDestX, iDestY, iWidth, iHeight);
	else if(source.m_iBytesPerChannel == 2)
		::CopyFromBuf<uint16_t>(*this, (const uint16_t *) source.m_pData, source.m_iChannels, source.m_iStrideBytes / sizeof(uint16_t), iSourceX, iSourceY, iDestX, iDestY, iWidth, iHeight);
	else
		throw Exception("CImgF::CopyFrom: invalid iBytesPerChannel");
}

template<typename T>
void CopyToBuf(const CImgF &in, T *pDest, int iDestChannels, int iDestStride,
	int iSourceX, int iSourceY, int iDestX, int iDestY,
	int iWidth, int iHeight, int iMax)
{
	pDest += iDestY * iDestStride + iDestX * iDestChannels;

	const int iPlanesToCopy = min(in.dim, iDestChannels);
	const int iInPlanesToSkip = in.dim - iPlanesToCopy;
	const int iOutPlanesToSkip = iDestChannels - iPlanesToCopy;
	iWidth = min(iWidth, in.width - iSourceX);
	iHeight = min(iHeight, in.height - iSourceY);

        for(int y=0; y < iHeight; ++y)
	{
		const float *pIn = in.ptr(iSourceX, iSourceY + y);
		T *pOut = pDest + y*iDestStride;
		for(int x = 0; x < iWidth; ++x)
		{
			for(int plane = 0; plane < iPlanesToCopy; ++plane)
			{
				*pOut = min(iMax, (int) lrintf(*pIn));
				++pIn;
				++pOut;
			}
			pIn += iInPlanesToSkip;
			pOut += iOutPlanesToSkip;
		}
	}
}

void CImgF::CopyTo(const CImg &dest, int iSourceX, int iSourceY, int iDestX, int iDestY, int iWidth, int iHeight) const
{
	if(dest.m_iBytesPerChannel == 1)
		::CopyToBuf<uint8_t>(*this, (uint8_t *) dest.m_pData, dest.m_iChannels, dest.m_iStrideBytes, iSourceX, iSourceY, iDestX, iDestY, iWidth, iHeight, 0xFF);
	else if(dest.m_iBytesPerChannel == 2)
		::CopyToBuf<uint16_t>(*this, (uint16_t *) dest.m_pData, dest.m_iChannels, dest.m_iStrideBytes / sizeof(uint16_t), iSourceX, iSourceY, iDestX, iDestY, iWidth, iHeight, 0xFFFF);
	else
		throw Exception("CImgF::CopyTo: invalid iBytesPerChannel");
}

void CImgF::draw_image(const CImgF &sprite, int x0, int y0, int iWidth, int iHeight)
{
	if(is_empty())
	      return;

	if(iWidth == -1)
		iWidth = sprite.width;
	if(iHeight == -1)
		iHeight = sprite.height;

	if(dim != sprite.dim)
		throw Exception("CImgF::draw_image requires identical formats");

	if(sprite.is_empty())
		return;
        if(sprite.data == this->data)
		return;

	const float *pSource = sprite.data;

	/* If x0,y0 are negative, push the source position up and reduce the width/height to copy. */
	if(y0 < 0)
	{
		y0 = -y0;
		pSource += y0 * sprite.stride;
		iHeight -= y0;
		y0 = 0;
	}

	if(x0 < 0)
	{
		x0 = -x0;
		pSource += x0 * sprite.dim;
		iWidth -= x0;
		x0 = 0;
	}

	const int lX = min(iWidth, width-x0);
	const int lY = min(iHeight, height-y0);

	float *pDest = ptr(x0, y0);
	const int iBytes = lX * dim * sizeof(float);
	for(int y=0; y<lY; ++y)
	{
		memcpy(pDest, pSource, iBytes);
		pDest += stride;
		pSource += sprite.stride;
	}
}

void CImg::Assign(uint8_t *pData, int iWidth, int iHeight, int iBytesPerChannel, int iChannels, int iStrideBytes)
{
	Free();

	m_pData = pData;
	m_iWidth = iWidth;
	m_iHeight = iHeight;
	m_iStrideBytes = iStrideBytes;
	m_iBytesPerChannel = iBytesPerChannel;
	m_iChannels = iChannels;
	m_bOwned = true;

#if defined(DEBUG)
	for(int i = 0; i < m_iHeight * m_iStrideBytes; ++i)
	{
		volatile static int x = m_pData[i];
	}
#endif
}

void CImg::Alloc(const CImg &rhs)
{
	Alloc(rhs.m_iWidth, rhs.m_iHeight, rhs.m_iBytesPerChannel, rhs.m_iChannels, rhs.m_iStrideBytes);
}

void CImg::Alloc(int iWidth, int iHeight, int iBytesPerChannel, int iChannels, int iStrideBytes)
{
	Free();

	if(iStrideBytes == -1)
		iStrideBytes = iWidth * iBytesPerChannel * iChannels;

	m_iWidth = iWidth;
	m_iHeight = iHeight;
	m_iStrideBytes = iStrideBytes;
	m_iBytesPerChannel = iBytesPerChannel;
	m_iChannels = iChannels;
	m_pData = new uint8_t[m_iStrideBytes * m_iHeight];
	m_bOwned = true;
}

void CImg::Hold(uint8_t *pData, int iWidth, int iHeight, int iBytesPerChannel, int iChannels, int iStrideBytes)
{
	Assign(pData, iWidth, iHeight, iBytesPerChannel, iChannels, iStrideBytes);
	m_bOwned = false;
}

void CImg::Hold(const CImg &rhs, int iX, int iY, int iWidth, int iHeight)
{
	if(iWidth == -1)
		iWidth = rhs.m_iWidth;
	if(iHeight == -1)
		iHeight  = rhs.m_iHeight;

	iX = min(iX, rhs.m_iWidth);
	iY = min(iY, rhs.m_iHeight);
	iWidth = min(iWidth, rhs.m_iWidth - iX);
	iHeight = min(iHeight, rhs.m_iHeight - iY);

	Assign((uint8_t *) rhs.ptr(iX, iY), iWidth, iHeight, rhs.m_iBytesPerChannel, rhs.m_iChannels, rhs.m_iStrideBytes);
	m_bOwned = false;
}

void CImg::Free()
{
	if(m_bOwned)
		delete[] m_pData;
	m_pData = NULL;
	m_iWidth = m_iHeight = m_iStrideBytes = m_iBytesPerChannel = m_iChannels = 0;
	m_bOwned = false;
}

void CImg::Swap(CImg &img)
{
	swap(m_pData, img.m_pData);
	swap(m_iWidth, img.m_iWidth);
	swap(m_iHeight, img.m_iHeight);
	swap(m_iStrideBytes, img.m_iStrideBytes);
	swap(m_iBytesPerChannel, img.m_iBytesPerChannel);
	swap(m_iChannels, img.m_iChannels);
	swap(m_bOwned, img.m_bOwned);
}

/* Convert from 16bpp to 8bpp in-place.  Don't compact the image. */
void CImg::ConvertTo8BPP()
{
	if(m_iBytesPerChannel == 1)
		return;
	else if(m_iBytesPerChannel != 2)
		throw Exception("CImg::ConvertTo8BPP: invalid iBytesPerChannel");

	cimgIM_forY(*this,y)
	{
		const uint16_t *pIn = (uint16_t *) ptr(0,y);
		uint8_t *pOut = ptr(0,y);
		int iElements = m_iChannels * m_iWidth;
		while(iElements--)
		{
			uint16_t iVal = *(pIn++);
			*(pOut++) = uint8_t(iVal * 10 / 1285);
		}
	}

	m_iBytesPerChannel = 1;
}

float CImgF::linear_pix2d(const float fx, const float fy, const int v) const
{
	const float nfx = fx<0?0:(fx>width-1?width-1:fx);
	const float nfy = fy<0?0:(fy>height-1?height-1:fy);
	const unsigned int x = (unsigned int)nfx;
	const unsigned int y = (unsigned int)nfy;
	const float dx = nfx-x;
	const float  dy = nfy-y;
	const unsigned int nx = dx>0?x+1:x;
	const unsigned int ny = dy>0?y+1:y;
	const float Icc = (*this)(x,y,v);
	const float Inc = (*this)(nx,y,v);
	const float Icn = (*this)(x,ny,v);
	const float Inn = (*this)(nx,ny,v);
	return Icc + dx*(Inc-Icc + dy*(Icc+Inn-Icn-Inc)) + dy*(Icn-Icc);
}

void CImg::CopyFrom(const CImg &source, int iSourceX, int iSourceY, int iDestX, int iDestY, int iWidth, int iHeight)
{
	if(source.m_iBytesPerChannel != m_iBytesPerChannel)
		throw Exception("CopyFrom: incompatible m_iBytesPerPixel");
	if(source.m_iChannels != m_iChannels)
		throw Exception("CopyFrom: incompatible m_iChannels");

	iWidth = min(iWidth, source.m_iWidth - iSourceX);
	iHeight = min(iHeight, source.m_iHeight - iSourceY);
	iWidth = min(iWidth, m_iWidth - iDestX);
	iHeight = min(iHeight, m_iHeight - iDestY);

	const int iChannelsToCopy = min(source.m_iChannels, m_iChannels);
	const int iInChannelsToSkip = source.m_iChannels - iChannelsToCopy;
	const int iOutChannelsToSkip = m_iChannels - iChannelsToCopy;
	const int iBytesToCopy = iChannelsToCopy * m_iBytesPerChannel;
	const int iInBytesToSkip = iInChannelsToSkip * m_iBytesPerChannel;
	const int iOutBytesToSkip = iOutChannelsToSkip * m_iBytesPerChannel;

	const uint8_t *pSource = source.m_pData;
	uint8_t *pDest = m_pData;
	pSource += iSourceY * source.m_iStrideBytes + iSourceX * source.m_iBytesPerChannel * m_iChannels;
	pDest += iDestY * m_iStrideBytes + iDestX * m_iBytesPerChannel * m_iChannels;

	for(int y=0; y < iHeight; ++y)
	{
		const uint8_t *pIn = pSource + y*source.m_iStrideBytes;
		uint8_t *pOut = pDest + y*m_iStrideBytes;
		for(int x = 0; x < iWidth; ++x)
		{
			for(int iChannel = 0; iChannel < iBytesToCopy; ++iChannel)
			{
				*pOut = *pIn;
				++pIn;
				++pOut;
			}
		}
		pIn += iInBytesToSkip;
		pOut += iOutBytesToSkip;
	}
}

/* Mask out (set to 0) everything outside the specified region (if bMaskOutside is false, inside). */
void CImg::MaskRegion(int iTop, int iBottom, int iLeft, int iRight, bool bMaskOutside)
{
	iLeft = clamp(iLeft, 0, m_iWidth);
	iRight = clamp(iRight, 0, m_iWidth);
	iTop = clamp(iTop, 0, m_iHeight);
	iBottom = clamp(iBottom, 0, m_iHeight);

	if(bMaskOutside)
	{
		for(int y = 0; y < iTop; ++y)
			memset(ptr(0, y), 0, m_iWidth);
		for(int y = iTop; y < iBottom; ++y)
		{
			memset(ptr(0, y), 0, iLeft);
			memset(ptr(iRight, y), 0, m_iWidth - iRight);
		}

		for(int y = iBottom; y < m_iHeight; ++y)
			memset(ptr(0, y), 0, m_iWidth);
	}
	else
	{
		for(int y = iTop; y < iBottom; ++y)
			memset(ptr(iLeft, y), 0, iRight - iLeft);
	}
}

/*
 * To reduce peak memory usage, process the image by breaking it into blocks.  We
 * need to be able to do this in both dimensions, so we can also keep blocks under
 * 4096x4096 for OpenGL.
 *
 * Blocks overlap, to prevent seams near the edges.
 *
 * Tricky: the source of each block needs to be the original image data, not the result
 * of previous blocks where the blocks overlap.  We want to avoid making a whole separate
 * copy of the image, to save memory.  So, keep a "backup" of the overlap area of each
 * block.  These are the only areas where the results of one block can affect the others.
 */

const int Blocks::g_iMaxPixelsPerBlock = 5000000;

void Blocks::LoadFromSourceImage(const CImg &SourceImage, int iOverlapPixels, int iMaxPixelsPerBlock)
{
	if(iMaxPixelsPerBlock == -1)
		iMaxPixelsPerBlock = g_iMaxPixelsPerBlock;

	m_SourceImage.Hold(SourceImage);
	m_iOverlapPixels = iOverlapPixels;
	m_Blocks.clear();
	m_BlockRegion.clear();

	if(m_SourceImage.m_iWidth == 0 || m_SourceImage.m_iHeight == 0)
		return;

	int iSliceWidth = m_SourceImage.m_iWidth;
	if(m_bLimitTo4096)
		iSliceWidth = min(4096 - iOverlapPixels*2, iSliceWidth);
	const int iSliceHeight = iMaxPixelsPerBlock / iSliceWidth;

	/* Split each pass into blocks of iSliceHeight,iSliceWidth each. */
	int iStartRow = 0;

	while(iStartRow < (int) m_SourceImage.m_iHeight)
	{
		int iStartCol = 0;
		while(iStartCol < (int) m_SourceImage.m_iWidth)
		{
			/* Process [iSliceTop,iSliceBottom) in the source image. */
			const int iSliceTop = iStartRow;
			const int iSliceBottom = min(iStartRow + iSliceHeight, (int) m_SourceImage.m_iHeight);
			const int iSliceRows = iSliceBottom - iSliceTop;

			/* We want iOverlapPixels of overlap on the top and bottom, which we'll process but
			 * not copy.  Clamp these when they fall off the edge of the image. */
			const int iTopBuffer = min(iOverlapPixels, iSliceTop);
			const int iBottomBuffer = min(iOverlapPixels, (int) m_SourceImage.m_iHeight - iSliceBottom);

			/* Likewise, process [iSliceLeft,iSliceRight). */
			const int iSliceLeft = iStartCol;
			const int iSliceRight = min(iStartCol + iSliceWidth, (int) m_SourceImage.m_iWidth);
			const int iSliceCols = iSliceRight - iSliceLeft;
			const int iLeftBuffer = min(iOverlapPixels, iSliceLeft);
			const int iRightBuffer = min(iOverlapPixels, (int) m_SourceImage.m_iWidth - iSliceRight);

			Rect block;
			block.l = iSliceLeft - iLeftBuffer;
			block.width = iSliceCols + iLeftBuffer + iRightBuffer;
			block.t = iSliceTop - iTopBuffer;
			block.height = iSliceRows + iBottomBuffer + iTopBuffer;

			Rect region;
			region.l = iLeftBuffer;
			region.width = iSliceCols;
			region.t = iTopBuffer;
			region.height = iSliceRows;

			printf("Block queued: %ix%i\n", iStartCol, iStartRow);
			m_Blocks.push_back(block);
			m_BlockRegion.push_back(region);

			iStartCol += iSliceWidth;
		}

		iStartRow += iSliceHeight;
	}
}

void Blocks::DeleteMaskedBlocks(const CImg &mask)
{
	if(mask.Empty())
		return;

	/* Optimization: if this block is completely masked out, skip it. */
	for(size_t i = 0; i < m_Blocks.size(); ++i)
	{
		Rect &block = m_Blocks[i];
		Rect &region = m_BlockRegion[i];
		bool bMasked = true;
		for(int y = block.t + region.t; y < block.t + region.t + region.height; ++y)
		{
			for(int x = block.l + region.l; x < block.l + region.l + region.width; ++x)
			{
				if(mask(x,y))
				{
					bMasked = false;
					break;
				}
			}
		}

		if(bMasked)
		{
			m_Blocks.erase(m_Blocks.begin()+i);
			m_BlockRegion.erase(m_BlockRegion.begin()+i);
			--i;
		}
	}
}

void Blocks::SaveOverlaps()
{
	/*
	 * Back up the overlapping portions.  The image looks like this:
	 *
	 * HHHHHHHHHHH
	 * V  0 v  1 V
	 * V    v    V
	 * hhhhhhhhhhh
	 * V  2 V  3 V
	 * V    V    V
	 * HHHHHHHHHHH
	 *
	 * We store them together like this:
	 *
	 * HorizontalOverlaps:
	 * HHHHHH   block 0 top
	 * hhhhhh   block 0 bottom
	 * HHHHHH   block 1 top
	 * hhhhhh   block 1 bottom
	 * HHHHHH   block 2 top
	 * hhhhhh   block 2 bottom
	 * HHHHHH   block 3 top
	 * hhhhhh   block 3 bottom
	 * HHHHHH   block 4 top
	 * hhhhhh   block 4 bottom
	 *
	 * VerticalOverlaps:
	 * VvVvVvVv
	 * VvVvVvVv
	 * 00112233
	 * LRLRLRLR
	 *
	 * Although many of the overlapping regions are usually the same, we don't depend
	 * on that, so blocks can be optimized and removed by PrepareBlocks().
	 *
	 * The corners are actually stored as part of both vertical and horizontal overlaps,
	 * since it's simpler to treat the two overlaps the same.
	 */
	m_HorizontalOverlaps.Alloc(GetMaxBlockWidth(), (int)m_Blocks.size() * m_iOverlapPixels * 2, m_SourceImage.m_iBytesPerChannel, m_SourceImage.m_iChannels);
	m_VerticalOverlaps.Alloc((int)m_Blocks.size() * m_iOverlapPixels * 2, GetMaxBlockHeight(), m_SourceImage.m_iBytesPerChannel, m_SourceImage.m_iChannels);

	for(int iBlock = 0; iBlock < (int)m_Blocks.size(); ++iBlock)
	{
		const Rect &r = m_Blocks[iBlock];
		const Rect &br = m_BlockRegion[iBlock];

		/* Top horizontal overlap: */
		int iStoreTop = iBlock*2 * m_iOverlapPixels;
		m_HorizontalOverlaps.CopyFrom(m_SourceImage, r.l, r.t,				0, iStoreTop, r.width, m_iOverlapPixels);

		/* Bottom horizontal overlap: */
		iStoreTop = (iBlock*2+1) * m_iOverlapPixels;
		m_HorizontalOverlaps.CopyFrom(m_SourceImage, r.l, r.t + br.t + br.height,	0, iStoreTop, r.width, m_iOverlapPixels);

		/* Left vertical overlap: */
		int iStoreLeft = iBlock*2 * m_iOverlapPixels;
		m_VerticalOverlaps.CopyFrom(m_SourceImage, r.l, r.t,				iStoreLeft, 0, m_iOverlapPixels, r.height);

		/* Right vertical overlap: */
		iStoreLeft = (iBlock*2+1) * m_iOverlapPixels;
		m_VerticalOverlaps.CopyFrom(m_SourceImage, r.l + br.l + br.width, r.t,		iStoreLeft, 0, m_iOverlapPixels, r.height);
	}
}

void Blocks::GetBlock(CImgF &WorkImage, int iBlock, int iChannels)
{
	const Rect &r = m_Blocks[iBlock];
	const Rect &br = m_BlockRegion[iBlock];
	WorkImage.alloc(r.width, r.height, iChannels);

	/* Copy the new source block. */
	WorkImage.CopyFrom(m_SourceImage, r.l, r.t, 0, 0, r.width, r.height);

	/* Copy the overlap back from the backups. */
	int iStoreTop = iBlock*2 * m_iOverlapPixels;
	WorkImage.CopyFrom(m_HorizontalOverlaps, 0, iStoreTop,	0, 0,			r.width, m_iOverlapPixels);
	iStoreTop = (iBlock*2+1) * m_iOverlapPixels;
	WorkImage.CopyFrom(m_HorizontalOverlaps, 0, iStoreTop,	0, br.t + br.height,	r.width, m_iOverlapPixels);

	int iStoreLeft = iBlock*2 * m_iOverlapPixels;
	WorkImage.CopyFrom(m_VerticalOverlaps, iStoreLeft, 0,	0, 0,			m_iOverlapPixels, r.height);
	iStoreLeft = (iBlock*2+1) * m_iOverlapPixels;
	WorkImage.CopyFrom(m_VerticalOverlaps, iStoreLeft, 0,	br.l + br.width, 0,	m_iOverlapPixels, r.height);
}

void Blocks::GetBlockMask(CImg &WorkMask, CImg &SourceMask, int iBlock)
{
	const Rect &r = m_Blocks[iBlock];
	const Rect &br = m_BlockRegion[iBlock];
	WorkMask.Hold(SourceMask.ptr(r.l, r.t), r.width, r.height, 1, 1, SourceMask.m_iStrideBytes);
}

void Blocks::StoreBlock(const CImgF &WorkImage, int iBlock)
{
	const Rect &r = m_Blocks[iBlock];
	const Rect &br = m_BlockRegion[iBlock];

	/* The amount of overlap for each block: */
	int iTopBuffer = r.t + br.t;
	int iLeftBuffer = r.l + br.l;

	/* Copy the finished data back out. */
	WorkImage.CopyTo(m_SourceImage, br.l, br.t, iLeftBuffer, iTopBuffer, br.width, br.height);
}

int Blocks::GetTotalRows() const
{
	int iTotalRows = 0;
	for(size_t i = 0; i < m_Blocks.size(); ++i)
		iTotalRows += m_Blocks[i].height;

	return iTotalRows;
}

int Blocks::GetTotalCols() const
{
	int iTotal = 0;
	for(size_t i = 0; i < m_Blocks.size(); ++i)
		iTotal += m_Blocks[i].width;

	return iTotal;
}

int Blocks::GetMaxBlockWidth() const
{
	int iMax = 0;
	for(size_t i = 0; i < m_Blocks.size(); ++i)
		iMax = max(iMax, m_Blocks[i].width);

	return iMax;
}

int Blocks::GetMaxBlockHeight() const
{
	int iMax = 0;
	for(size_t i = 0; i < m_Blocks.size(); ++i)
		iMax = max(iMax, m_Blocks[i].height);

	return iMax;
}
