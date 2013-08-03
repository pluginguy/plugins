#ifndef CIMGI_H
#define CIMGI_H

#include <stdio.h>
#include <algorithm>
#include "Helpers.h"
using namespace std;

// #define DEBUG

/*
 * This class has a basic API like CImg, but uses an interleaved format.  CImg uses a planar
 * format, which prevents SSE optimization.
 *
 * This only supports floating-point textures.  To enable SSE optimizations, the image must
 * have four color components.
 */

#define cimgI_for1(bound,i) for (int i = 0; i<(int)(bound); ++i)
#define cimgI_forX(img,x) cimgI_for1((img).width,x)
#define cimgI_forY(img,y) cimgI_for1((img).height,y)
#define cimgI_forV(img,v) cimgI_for1((img).dim,v)
#define cimgI_forXY(img,x,y) cimgI_forY(img,y) cimgI_forX(img,x)
#define cimgI_forXV(img,x,v) cimgI_forX(img,x) cimgI_forV(img,v)
#define cimgI_forYV(img,y,v) cimgI_forY(img,y) cimgI_forV(img,v)
#define cimgI_forXYV(img,x,y,v) cimgI_forXY(img,x,y) cimgI_forV(img,v)


#define cimgI_for_ptrXV(img,y,p) for (float *p = (img).ptr(0,y,0), *p##end = (img).ptr((img).width-1,y,(img).dim-1); p<=p##end; ++p)
#define cimgI_for_ptrXV_const(img,y,p) for (const float *p = (img).ptr(0,y,0), *p##end = (img).ptr((img).width-1,y,(img).dim-1); p<=p##end; ++p)

#define CImgI_3x3(I) float I[9]; \
                      float &I##pp = I[0]; float &I##cp = I[1]; float &I##np = I[2]; \
                      float &I##pc = I[3]; float &I##cc = I[4]; float &I##nc = I[5]; \
                      float &I##pn = I[6]; float &I##cn = I[7]; float &I##nn = I[8]; \
                      I##pp = I##cp = I##np = \
                      I##pc = I##cc = I##nc = \
                      I##pn = I##cn = I##nn = 0

#define cimgI_for3(bound,i) \
 for (int i = 0, _p1##i = 0, \
      _n1##i = 1>=(bound)?(int)(bound)-1:1; \
      _n1##i<(int)(bound) || i==--_n1##i; \
      _p1##i = i++, ++_n1##i)

#define cimgI_for3x3(img,x,y,v,I) \
  cimgI_for3((img).height,y) for (int x = 0, \
   _p1##x = 0, \
   _n1##x = (int)( \
   (I[0] = I[1] = (img)(0,_p1##y,v)), \
   (I[3] = I[4] = (img)(0,y,v)), \
   (I[6] = I[7] = (img)(0,_n1##y,v)),	\
   1>=(img).width?(int)((img).width)-1:1); \
   (_n1##x<(int)((img).width) && ( \
   (I[2] = (img)(_n1##x,_p1##y,v)), \
   (I[5] = (img)(_n1##x,y,v)), \
   (I[8] = (img)(_n1##x,_n1##y,v)),1)) || \
   x==--_n1##x; \
   I[0] = I[1], I[1] = I[2], \
   I[3] = I[4], I[4] = I[5], \
   I[6] = I[7], I[7] = I[8], \
   _p1##x = x++, ++_n1##x)

/* Loop over every pixel value in the image.  This includes padding between rows.  This data
 * is guaranteed to be a valid number (not NaN), and can be read and written to, but the
 * contents are undefined. */
#define cimgI_for(img,ptr) for (float *ptr = (img).data + (img).size(); (ptr--)>(img).data; )

typedef unsigned char uint8_t;

class CImg;
class CImgF
{
public:
	float *data;
	void *real_data;
	int width;
	int height;
	int dim;
	int stride; /* width * height * dim, possibly plus padding */
	bool owned;

	CImgF();
	CImgF(const CImgF &rhs);
	~CImgF();
	void free();

	static float *aligned_alloc(int iSize, void **pRealAlloc);
	void alloc(int iWidth, int iHeight, int iChannels, int iStride = -1);
	void assign(const float *pBuffer, int iWidth, int iHeight, int iBytesPerPixel, int iStride);
	void assign(const CImgF &img) { return assign(img.data, img.width, img.height, img.dim, img.stride); }
	void Hold(float *pBuffer, int iWidth, int iHeight, int iChannels, int iStride);
	void alias(const CImgF &img, int iX, int iY, int iWidth);
	CImgF &swap(CImgF &img);
	void fill(float f);
	void scale(float fFactor);
	void normalize(float a, float b);
	void maxmin(float &fMax, float &fMin) const;
	bool sse_compatible() const;

	void draw_image(const CImgF &sprite, const int x0, const int y0=0, int iWidth = -1, int iHeight = -1);

	const float *ptr(const unsigned int x, const unsigned int y=0, const unsigned int v=0) const {
		return const_cast<CImgF*>(this)->ptr(x,y,v);
	}
	float *ptr(const unsigned int x, const unsigned int y=0, const unsigned int v=0) {
		return data + x*dim + y*stride + v;
	}
	__m128 *ptr128(const unsigned int x, const unsigned int y=0) {
		return (__m128 *) (data + x*dim + y*stride);
	}
	float &operator()(const unsigned int x, const unsigned int y, const unsigned int v) {
		return data[x*dim + y*stride + v];
	}

	const float &operator()(const unsigned int x, const unsigned int y, const unsigned int v) const {
		return data[x*dim + y*stride + v];
	}

	bool is_empty() const
	{
		return !(data && width && height && dim);
	}

	int size() const
	{
		return height * stride;
	}

	float linear_pix2d(const float fx, const float fy, const int v=0) const;

	void CopyFrom(const CImg &source, int iSourceX, int iSourceY, int iDestX, int iDestY, int iWidth, int iHeight);
	void CopyTo(const CImg &source, int iSourceX, int iSourceY, int iDestX, int iDestY, int iWidth, int iHeight) const;

private:
};

/* A simple wrapper for integer images.  This is mostly just enough to hold an
 * image, and to blit between each other and CImgF.  The heavy lifting is all
 * done in CImgF. */
#define cimgIM_forX(img,x) cimgI_for1((img).m_iWidth,x)
#define cimgIM_forY(img,y) cimgI_for1((img).m_iHeight,y)
#define cimgIM_forXY(img,x,y) cimgIM_forY(img,y) cimgIM_forX(img,x)
class CImg
{
public:
	CImg() { m_pData = NULL; Free(); }
	~CImg() { Free(); }

	void Assign(uint8_t *pData, int iWidth, int iHeight, int iBytesPerChannel, int iChannels, int iStrideBytes);
	void Alloc(const CImg &rhs);
	void Alloc(int iWidth, int iHeight, int iBytesPerChannel, int iChannels, int iStrideBytes = -1);
	void Hold(uint8_t *pData, int iWidth, int iHeight, int iBytesPerChannel, int iChannels, int iStrideBytes);
	void Hold(const CImg &rhs, int iX = 0, int iY = 0, int iWidth = -1, int iHeight = -1);
	void Free();
	bool Empty() const { return !m_iWidth || !m_iHeight; }
	void Swap(CImg &img);
	void ConvertTo8BPP();
	void CopyFrom(const CImg &source, int iSourceX, int iSourceY, int iDestX, int iDestY, int iWidth, int iHeight);
	void MaskRegion(int iTop, int iBottom, int iLeft, int iRight, bool bMaskOutside = true);

	const uint8_t *ptr(int x, int y=0) const { return m_pData + x*m_iChannels*m_iBytesPerChannel + y*m_iStrideBytes; }
	const uint8_t &operator()(int x, int y) const { return *ptr(x,y); }
	uint8_t *ptr(int x, int y=0) { return m_pData + x*m_iChannels*m_iBytesPerChannel + y*m_iStrideBytes; }
	uint8_t &operator()(int x, int y) { return *ptr(x,y); }

	uint8_t *m_pData;
	int m_iWidth;
	int m_iHeight;
	int m_iStrideBytes;
	int m_iBytesPerChannel;
	int m_iChannels;
	bool m_bOwned;

private:
	CImg(const CImg &cpy);
	CImg &operator=(const CImg &rhs);
};

class Blocks
{
public:
	void LoadFromSourceImage(const CImg &SourceImage, int iOverlapPixels, int iMaxPixelsPerBlock = -1);
	void DeleteMaskedBlocks(const CImg &mask);
	void SaveOverlaps();

	void GetBlock(CImgF &WorkImage, int iBlock, int iChannels);
	void GetBlockMask(CImg &WorkMask, CImg &SourceMask, int iBlock);
	void StoreBlock(const CImgF &WorkImage, int iBlock);

	/* Get the total number of rows represented by m_Blocks, for Progress: */
	int GetTotalRows() const;
	int GetTotalCols() const;
	size_t GetTotalBlocks() const { return m_Blocks.size(); }
	int GetMaxBlockWidth() const;
	int GetMaxBlockHeight() const;

	void SetLimitTo4096(bool b) { m_bLimitTo4096 = b; }

	static const int g_iMaxPixelsPerBlock;

private:
	bool m_bLimitTo4096;
	int m_iOverlapPixels;

	/* The image is split into chunks for processing, to cap memory requirements.  We split
	 * on both X and Y, so we can limit texture resolution for OpenGL mode.  Blocks have
	 * overlap, to avoid artifacts around each block. */
	struct Rect { int t, l, width, height; };
	/* Blocks in the source image, including overlapping space: */
	vector<Rect> m_Blocks;
	/* The active, non-overlapping region inside each block, which is what we actually copy out: */
	vector<Rect> m_BlockRegion;

	CImg m_HorizontalOverlaps;
	CImg m_VerticalOverlaps;

	CImg m_SourceImage;
};

#endif
