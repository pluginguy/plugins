#include "GaussianBlur.h"
#include "Helpers.h"
#include <math.h>

static void box_blur_C_row(const float *pBoxStart, const float *pBoxEnd, float *pOut, int x, int N, float *fSum,
			    float fSumWeight, float fLeftEdgeWeight, float fRightEdgeWeight, int pixel_stride)
{
	for(; x < N; ++x)
	{
		/* Read the pixels on the left and right sides of the box. */
		const float wL = *pBoxStart;
		const float wR = *pBoxEnd;

		/* The left pixel will be included in fTotal; subtract it. */
		*fSum -= wL;

		/* Add the total sum and the two weighted pixels on each end to get the value for the pixel
		 * in the center of the box. */
		*pOut = *fSum*fSumWeight + wL*fLeftEdgeWeight + wR*fRightEdgeWeight;

		/* Add the pixel on the right side of the box to the total, for the following pixels that
		 * include it. */
		*fSum += wR;

		pBoxStart += pixel_stride;
		pBoxEnd += pixel_stride;
		pOut += pixel_stride;
	}
}

static void box_blur_SSE_row(const float *pBoxStart, const float *pBoxEnd, float *pOut, int x, int N, float *pSum,
			    float fSumWeight, float fLeftEdgeWeight, float fRightEdgeWeight, int pixel_stride)
{
	int iRowCount = N - x;
	pixel_stride *= sizeof(float);

	__asm
	{
		push esi
		push edi
		push eax
		push ebx
		push ecx
		push edx

		mov esi, iRowCount

		mov eax, pSum
		movups xmm5, [eax]	// xmm5: fSum

		mov eax, pBoxStart
		mov ebx, pBoxEnd
		mov edx, pOut
		mov edi, pixel_stride

main_loop:
		movaps xmm0, [eax]	// xmm0: wL = *pBoxStart
		movaps xmm1, [ebx]	// xmm1: wR = *pBoxEnd

		subps xmm5, xmm0	// *fSum -= wL

		mov ecx, fSumWeight
		movd xmm7, ecx
		punpckldq xmm7, xmm7
		punpckldq xmm7, xmm7
		movups xmm3, xmm5	// fSum
		mulps xmm3, xmm7	// fSum * fSumWeight

		mov ecx, fLeftEdgeWeight
		movd xmm7, ecx
		punpckldq xmm7, xmm7
		punpckldq xmm7, xmm7
		movups xmm6, xmm0	// wL
		mulps xmm6, xmm7	// wL * fLeftEdgeWeight
		addps xmm3, xmm6

		mov ecx, fRightEdgeWeight
		movd xmm7, ecx
		punpckldq xmm7, xmm7
		punpckldq xmm7, xmm7
		movups xmm6, xmm1	// wL
		mulps xmm6, xmm7	// wL * fLeftEdgeWeight
		addps xmm3, xmm6

		addps xmm5, xmm1	// *fSum += wR

		movaps [edx], xmm3	// *pOut = result

		add eax, edi		// pBoxStart += pixel_stride
		add ebx, edi		// pBoxEnd += pixel_stride
		add edx, edi		// pOut += pixel_stride
	
		dec esi
		jnz main_loop

		pop edx
		pop ecx
		pop ebx
		pop eax
		pop edi
		pop esi
	}
}


/*
 * fBoxWidth: the side of the box to average.
 *
 * This doesn't handle border conditions: the edge will be artifacted.  gaussian_blur_estimation
 * adds enough color-repeated padding around the edge that the black area will never reach
 * the active image area.
 */
/* Optimize: avoid the extra copy by having an output buffer and ping-ponging the results. */
void box_blur(CImgF &img, float fBoxWidth, bool bHoriz)
{
	/* Convert fBoxWidth from the size of the box to the distance to average in each direction. */
	fBoxWidth /= 2.0f;

	CImgF out;
	out.alloc(img.width, img.height, img.dim, img.stride);
	out.fill(0);

	/*
	 * [1] fBoxWidth 0.75, offset = 0.5,
	 * aaaabbbbccccddddeeee
	 *        _----_                (- = 0.25)
	 *  c = c + b*0.25 + d*0.25
	 *  c /= 1.5
	 *
	 * [2] fBoxWidth 1.75, offset = 0.5,
	 * aaaabbbbccccddddeeee
	 *    _------------_
	 *  c = b + c + d + a*0.25 + e*0.25
	 *  c /= 3.5
	 */

	const float fStart = -fBoxWidth + 0.5f;
	const float fEnd = fStart + fBoxWidth*2;

	/* The distance between the weighted value on the right side of the box and the pixel receiving it. */
	const int iBoxOffset = (int) floorf(fBoxWidth + 0.5f + 1e-05f); /* 1.75 -> 2, distance from e to c */

	/* The distance from the right weighted value to the left weighted value.  (This may be the same as iBoxOffset,
	 * eg. [2].)  iBoxWidth >= iBoxOffset. */
	int iBoxWidth = int(floorf(fEnd) - floorf(fStart) + 1e-05f);
	float fRightEdgeWidth;
	float fLeftEdgeWidth;

	if(iBoxWidth == 0)
	{
		fRightEdgeWidth = fEnd - fStart;
		fLeftEdgeWidth = 0;
	}
	else
	{
		fRightEdgeWidth = fEnd - floorf(fEnd);
		fLeftEdgeWidth = ceilf(fStart + 1e-05f) - fStart;
	}

	bool bSSE = !!(GetCPUID() & CPUID_SSE);
	if(img.dim != 4)
		bSSE = false;
	if(img.stride & 0x3) // not aligned
		bSSE = false;

	/* Each pixel is the sum of fTotal (the non-fractional part in the middle of the region) and
	 * each fractional border pixel.  These won't sum to 1. */
	const int iSumWidth = lrintf(fBoxWidth*2 - fLeftEdgeWidth - fRightEdgeWidth);

	const float fSumWeight = iSumWidth == 0? 0: (1.0f / (fBoxWidth*2));
	const float fRightEdgeWeight = fRightEdgeWidth / (fBoxWidth*2);
	const float fLeftEdgeWeight = fLeftEdgeWidth / (fBoxWidth*2);

	if(bHoriz)
	{
		const int N = (int) img.width, pixel_stride = img.dim;
		for(int y = 0; y < (int) img.height; ++y)
		{
			CImgF box_sum;
			box_sum.alloc(img.dim, 1, 1);
			box_sum.fill(0);

			/* Walk the box up to the first pixel where the whole box is in-bounds; we'll
			 * start the output there. */
			int iStartX = min(N, max(iBoxWidth, iBoxOffset));
			cimgI_forV(img,v)
			{
				float &fSum = box_sum(v,0,0);
				int x = 0;
				/* Fill in the box sum up to the first sample. */
				for(x = -iSumWidth-1; x < 0; ++x)
					fSum += img(0,y,v);

				for(; x<iStartX; ++x)
				{
					fSum -= img(clamp(x-iBoxWidth, 0, N-1), y,v);
					fSum += img(clamp(x, 0, N-1), y,v);
				}
			}

			/* If iBoxWidth > iBoxOffset, we won't actually blur the first (iBoxWidth-iBoxOffset) pixels;
			 * this is part of the edge artifacting mentioned above.  Copy these source pixels, so we don't
			 * leave them uninitialized. */
			cimgI_forV(img,v)
			{
				for(int x = 0; x < iBoxOffset; ++x)
					out(x,y,v) = img(x,y,v);
			}

			/* Fast path: pixels with nothing averaged from out of bounds. */
			if(bSSE)
			{
				const float *pBoxStart = img.ptr(iStartX-iBoxWidth,y,0);
				const float *pBoxEnd = img.ptr(iStartX,y,0);
				float *pOut = out.ptr(iStartX-iBoxOffset,y,0);
				box_blur_SSE_row(pBoxStart, pBoxEnd, pOut, iStartX, N, box_sum.ptr(0,0,0), fSumWeight, fLeftEdgeWeight, fRightEdgeWeight, pixel_stride);
			}
			else
			{
				cimgI_forV(img,v)
				{
					const float *pBoxStart = img.ptr(iStartX-iBoxWidth,y,v);
					const float *pBoxEnd = img.ptr(iStartX,y,v);
					float *pOut = out.ptr(iStartX-iBoxOffset,y,v);
					box_blur_C_row(pBoxStart, pBoxEnd, pOut, iStartX, N, box_sum.ptr(v,0,0), fSumWeight, fLeftEdgeWeight, fRightEdgeWeight, pixel_stride);
				}
			}
		}
	}
	else
	{
		/* Ugh.  Duplicating this is less ugly than a big #define (which is impossible to
		 * debug), but this is still a mess. */
		const int N = (int) img.height, pixel_stride = img.stride;
		for(int x = 0; x < (int) img.width; ++x)
		{
			CImgF box_sum;
			box_sum.alloc(img.dim, 1, 1);
			box_sum.fill(0);

			int iStartY = min(N, max(iBoxWidth, iBoxOffset));
			cimgI_forV(img,v)
			{
				float &fSum = box_sum(v,0,0);
				int y = 0;
				/* Fill in the box sum up to the first sample. */
				for(y = -iSumWidth-1; y < 0; ++y)
					fSum += img(x,0,v);

				for(; y<iStartY; ++y)
				{
					fSum -= img(x, clamp(y-iBoxWidth, 0, N-1),v);
					fSum += img(x, clamp(y, 0, N-1),v);
				}
			}

			/* Fast path: pixels with nothing averaged from out of bounds. */
			if(bSSE)
			{
				const float *pBoxStart = img.ptr(x, iStartY-iBoxWidth,0);
				const float *pBoxEnd = img.ptr(x,iStartY,0);
				float *pOut = out.ptr(x, iStartY-iBoxOffset,0);
				box_blur_SSE_row(pBoxStart, pBoxEnd, pOut, iStartY, N, box_sum.ptr(0,0,0), fSumWeight, fLeftEdgeWeight, fRightEdgeWeight, pixel_stride);
			}
			else
			{
				cimgI_forV(img,v)
				{
					float &fSum = box_sum(v,0,0);
					const float *pBoxStart = img.ptr(x, iStartY-iBoxWidth,v);
					const float *pBoxEnd = img.ptr(x,iStartY,v);
					float *pOut = out.ptr(x, iStartY-iBoxOffset,v);
					box_blur_C_row(pBoxStart, pBoxEnd, pOut, iStartY, N, &fSum, fSumWeight, fLeftEdgeWeight, fRightEdgeWeight, pixel_stride);
				}
			}
		}
	}

	img.swap(out);
}

/*
 * I'm not sure how Photoshop's blurring radius corresponds to the actual blurring
 * amount.  I don't want to introduce a brand new blurring factor; our blur should
 * line up with Photoshop's.  
 *
 * So, as an evil hack, take samples of where our blur lines up closely with Photoshop's,
 * record the corresponding blur factors, and interpolate.  Beyond about 2 in our scaling,
 * it's roughly linear.  It's difficult to get exact numbers, because Photoshop quantizes
 * the radius to 0.1.
 */
static float ScaleAlpha(float a)
{
	struct
	{
		float fPhotoshop;
		float fBox;
	}
	samples[] =
	{
		{  0.0f,   1.0f, },
		{  0.3f,   1.1f, },
		{  0.5f,   1.2f, },
		{  0.7f,   1.3f, },
		{  0.8f,   1.4f, },
		{  0.9f,   1.5f, },
		{  1.05f,  1.7f, },
		{  1.1f,   1.8f, },
		{  1.15f,  1.9f, },
		{  1.2f,   2.0f, },
		{  1.45f,  3.0f, },
		{  2.6f,   5.0f, },
		{  5.15f, 10.0f, },
		{ 10.1f,  20.0f, },
		{ 15.2f,  30.0f, },
		{ 25.15f, 50.0f, },
		{ -1, -1 }
	};

	/* Given a Gaussian blur radius in Photoshop units, interpolate our own box size.
	 * A box size of 1 or less is no blurring. */
	int i;
	for(i = 1; samples[i].fPhotoshop >= 0; ++i)
	{
		if(samples[i].fPhotoshop >= a)
			break;
	}

	if(samples[i].fPhotoshop < 0)
	{
		/* The value is off the end, so extrapolate. */
		--i;
	}

	/* Interpolate between samples[i-1] and samples[i].  */
	return scale(a, samples[i].fPhotoshop, samples[i-1].fPhotoshop, samples[i].fBox, samples[i-1].fBox);
}

/* Approximate a Gaussian blur with three box filters. */
void gaussian_blur_estimation(CImgF &i, float a, volatile bool *pStopRequest)
{
	a = ScaleAlpha(a);

	/* Handle the border like Photoshop does: add a border around the image, repeat border
	 * pixels into the empty space, and then keep it around for each box filter pass.  This
	 * is different than just emulating GL_CLAMP, since the border area will be blurred after
	 * each pass, too. */
	int iBuffer = int(ceilf(a))*3;
	CImgF iCopy;
	iCopy.alloc(i.width + iBuffer*2, i.height + iBuffer*2, i.dim);
	iCopy.draw_image(i, iBuffer, iBuffer);

	for(int y = 0; y < iBuffer; ++y) cimgI_forXV(iCopy,x,v)
	{
		iCopy(x,y,v) = iCopy(
			clamp(x, iBuffer, int(iCopy.width) - iBuffer - 1),
			clamp(y, iBuffer, int(iCopy.height) - iBuffer - 1), v);
	}
	for(int y = iBuffer; y < (int) iCopy.height - iBuffer; ++y)
	{
		for(int x = 0; x < iBuffer; ++x)
		{
			cimgI_forV(iCopy,v)
			{
				iCopy(x,y,v) = iCopy(
					clamp(x, iBuffer, int(iCopy.width) - iBuffer - 1),
					clamp(y, iBuffer, int(iCopy.height) - iBuffer - 1), v);
			}
		}
		for(int x = iCopy.width - iBuffer; x < (int) iCopy.width; ++x)
		{
			cimgI_forV(iCopy,v)
			{
				iCopy(x,y,v) = iCopy(
					clamp(x, iBuffer, int(iCopy.width) - iBuffer - 1),
					clamp(y, iBuffer, int(iCopy.height) - iBuffer - 1), v);
			}
		}
	}
	for(int y = iCopy.height - iBuffer; y < (int) iCopy.height; ++y) cimgI_forXV(iCopy,x,v)
	{
		iCopy(x,y,v) = iCopy(
			clamp(x, iBuffer, int(iCopy.width) - iBuffer - 1),
			clamp(y, iBuffer, int(iCopy.height) - iBuffer - 1), v);
	}
	box_blur(iCopy, a, true);		check_cancel;
	box_blur(iCopy, a, true);		check_cancel;
	box_blur(iCopy, a, true);		check_cancel;
	box_blur(iCopy, a, false);		check_cancel;
	box_blur(iCopy, a, false);		check_cancel;
	box_blur(iCopy, a, false);		check_cancel;

	i.draw_image(iCopy, -iBuffer, -iBuffer);
}
