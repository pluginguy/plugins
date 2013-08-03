/* License information is at the bottom of the file. */

#include "Unsharp.h"
#include <math.h>

struct iir_param
{
	float B,b1,b2,b3,b0,q;
	float *p;

	iir_param()
	{
		p = NULL;
	}

	~iir_param()
	{
		delete [] p;
	}

	void init(float radius, int length)
	{
		if (radius >= 2.5) q = 0.98711f * radius - 0.96330f;
		else q = 3.97156f - 4.14554f * sqrtf(1.0f-0.26891f * radius);

		b0 = 1.57825f + ((0.422205f * q  + 1.4281f) * q + 2.44413f) * q;
		b1 = ((1.26661f * q +2.85619f) * q + 2.44413f) * q / b0;
		b2 = - ((1.26661f*q +1.4281f) * q * q ) / b0;
		b3 = 0.422205f * q * q * q / b0;
		B = 1.0f - (b1 + b2 + b3);

		int w = int(radius)+10;
		p = new float[length+2*w];
	}

	void filter(int radius, float *data, int width)
	{
		int w = radius;

		float *const lp = data;
		float *const rp = data + width-1+w;

		int i;

		for (i=1; i<=w; i++) data[-i] = data[i]; /* mirror edges */
		for (i=1; i<=w; i++) data[i+width-1] = data[-i+width-1];
	  
		{
			data = lp-w;
			float d1, d2, d3;
			d1 = d2 = d3 = *data;
			while(data <=  rp)
			{
				*data *= B;
				*data += b3 * d3;      
				*data += b2 * (d3 = d2);    
				*data += b1 * (d2 = d1); 
				d1 = *data++;
			} 
		}
	  
		data--;   
		{
			float d1, d2, d3;
			d1 = d2 = d3 = *data;

			while(data >= lp)
			{
				*data *= B;
				*data += b3 * d3;      
				*data += b2 * (d3 = d2);    
				*data += b1 * (d2 = d1); 
				d1 = *data--;
			} 
		}
	}

	/*
	 * This function is written as if it is blurring a column at a time,
	 * even though it can operate on rows, too.  There is no difference
	 * in the processing of the lines, at least to the blur_line function.
	 */
	void blur_line(int radius, float *data, int len, long stride)
	{
		const int w = radius+10;

		for(int row = 0, idx=0 ; idx < len; row += stride, idx++)
			p[idx+w] = data[row];

		filter(radius, p+w, len);

		for(int row = 0, idx=0; idx < len; row += stride, idx++)
			data[row] = p[idx+w];
	}
}; 

struct iir_param_sse
{
	__m128 B,b1,b2,b3,b0,q;
	__m128 *p;
	void *pReal;

	iir_param_sse()
	{
		p = NULL;
		pReal = NULL;
	}

	~iir_param_sse()
	{
		delete [] pReal;
	}

	void init(float radius, int length)
	{
		iir_param dummy;
		dummy.init(radius, length);

		q = _mm_load1_ps(&dummy.q);
		b0 = _mm_load1_ps(&dummy.b0);
		b1 = _mm_load1_ps(&dummy.b1);
		b2 = _mm_load1_ps(&dummy.b2);
		b3 = _mm_load1_ps(&dummy.b3);
		B = _mm_load1_ps(&dummy.B);

		int w = int(radius)+10;
		// XXX allocation, stride
		int iSize = length+2*w;
		p = (__m128 *) CImgF::aligned_alloc(iSize * sizeof(__m128), &pReal);
	}

	void filter(int radius, __m128 *data, int width)
	{
		int w = radius;

		__m128 *const lp = data;
		__m128 *const rp = data + width-1+w;

		int i;

		for (i=1; i<=w; i++) data[-i] = data[i]; /* mirror edges */
		for (i=1; i<=w; i++) data[i+width-1] = data[-i+width-1];
	  
		{
			data = lp-w;
			__m128 d1, d2, d3;
			d1 = d2 = d3 = *data;
			while(data <=  rp)
			{
				*data = _mm_mul_ps(*data, B); // *data *= B;
				*data = _mm_add_ps(*data, _mm_mul_ps(b3, d3)); // *data += b3 * d3;      
				d3 = d2;
				*data = _mm_add_ps(*data, _mm_mul_ps(b2, d2)); // *data += b2 * d2;
				d2 = d1;
				*data = _mm_add_ps(*data, _mm_mul_ps(b1, d1)); // *data += b1 * d1;
				d1 = *data++;
			} 
		}
	  
		data--;   
		{
			__m128 d1, d2, d3;
			d1 = d2 = d3 = *data;

			while(data >= lp)
			{
				*data = _mm_mul_ps(*data, B); // *data *= B;
				*data = _mm_add_ps(*data, _mm_mul_ps(b3, d3)); // *data += b3 * d3;      
				d3 = d2;
				*data = _mm_add_ps(*data, _mm_mul_ps(b2, d2)); // *data += b2 * d2;
				d2 = d1;
				*data = _mm_add_ps(*data, _mm_mul_ps(b1, d1)); // *data += b1 * d1;
				d1 = *data--;
			} 
		}
	}

	/*
	 * This function is written as if it is blurring a column at a time,
	 * even though it can operate on rows, too.  There is no difference
	 * in the processing of the lines, at least to the blur_line function.
	 */
	void blur_line(int radius, __m128 *data, int len, long stride)
	{
		const int w = radius+10;

		for(int row = 0, idx=0 ; idx < len; row += stride, idx++)
		{
			__m128 v = data[row];
			p[idx+w] = v;
		}

		filter(radius, p+w, len);

		for(int row = 0, idx=0; idx < len; row += stride, idx++)
			data[row] = p[idx+w];
	}
}; 

static float noise_factor(float value, float shadow, float midtone, float light, float high)
{
	if(value <= 0.25f)
		return shadow;
	else if(value <= 0.5f)
	{
		float v = (0.5f-value) / 0.25f;
		return shadow * v + midtone * (1.0f-v);
	}
	else if(value <= 0.75f)
	{
		float v = (0.75f-value) / 0.25f;
		return midtone * v + light * (1.0f-v);
	}
	else
	{
		float v = (1.0f-value) / 0.25f;
		return light * v + high * (1.0f-v);
	}
}

void unsharp_region_apply_gamma(CImgF &img, float gamma, volatile bool *pStopRequest, volatile LONG *pProgress)
{
	bool bSSE = img.sse_compatible();

	if(bSSE)
	{
		__m128 gamma128 = _mm_load1_ps(&gamma);
		cimgI_forY(img, y)
		{
			progress_and_check_cancel;

			__m128 *p = (__m128 *) img.ptr(0, y);
			cimgI_forX(img, x)
			{
				__m128 val = powf_sse2(*p, gamma128);
				*p = val;
				++p;
			}
		}
	}
	else
	{
		cimgI_forY(img, y)
		{
			progress_and_check_cancel;

			cimgI_for_ptrXV(img, y, p)
			{
				*p = powf(*p, gamma);
			}
		}
	}
}

void unsharp_region_apply_blur_horiz(CImgF &img, float radius, volatile bool *pStopRequest, volatile LONG *pProgress)
{
	if(img.sse_compatible())
	{
		iir_param_sse iir;
		iir.init(radius, img.width);

		cimgI_forY(img, row)
		{
			progress_and_check_cancel;
			iir.blur_line((int) radius, img.ptr128(0, row), img.width, 1);
		}
	}
	else
	{
		iir_param iir;
		iir.init(radius, img.width);

		cimgI_forY(img, row)
		{
			progress_and_check_cancel;
			cimgI_forV(img, b)
				iir.blur_line((int) radius, img.ptr(0, row, b), img.width, img.dim);
		}
	}
}

void unsharp_region_apply_blur_vert(CImgF &img, float radius, volatile bool *pStopRequest, volatile LONG *pProgress)
{
	if(img.sse_compatible())
	{
		iir_param_sse iir;
		iir.init(radius, img.height);

		int stride = img.stride * sizeof(float) / sizeof(__m128);
		cimgI_forX(img, col)
		{
			progress_and_check_cancel;
			iir.blur_line((int) radius, img.ptr128(col, 0), img.height, stride);
		}
	}
	else
	{
		iir_param iir;
		iir.init(radius, img.height);

		cimgI_forX(img, col)
		{
			progress_and_check_cancel;
			cimgI_forV(img, b)
				iir.blur_line((int) radius, img.ptr(col, 0, b), img.height, img.stride);
		}
	}
}

#define FR 0.212671f
#define FG 0.715160f
#define FB 0.072169f
void unsharp_region_combine(const CImgF &img, CImgF &dest, float amountup, float amountdown, float threshold,
			    float shadow, float midtone, float light, float high, volatile bool *pStopRequest, volatile LONG *pProgress)
{
	/* merge the source and destination (which currently contains the blurred version) images */
	cimgI_forY(img, row)
	{
		progress_and_check_cancel;
		const float *s = img.ptr(0, row, 0);
		float *d = dest.ptr(0, row, 0);

		cimgI_forX(img, u)
		{
			float lum;
			{
				/* If it's RGB or RGBA, we know what it is.  Otherwise, just average the channels.  This
				 * is completely wrong for YUV/LAB. */
				if (img.dim == 3 || img.dim == 4)
				{
					lum = (FR*s[0] + FG*s[1] + FB*s[2]);
				}
				else
				{
					for(int i = 0; i < img.dim; ++i)
						lum += s[i];
					lum /= img.dim;
				}
			}

			const float a = noise_factor(lum, shadow, midtone, light, high);

			for(int v = 0; v < img.dim; ++v)
			{
				float value = s[v];
				float dvalue = d[v];
				float diff = value - dvalue;

				if(diff > threshold)
				{
					diff -= threshold;
					value += diff * a * amountup * sqrtf(1.0f-value);
				}
				else if (diff < -threshold)
				{
					diff += threshold;
					value += diff * a * amountdown * sqrtf(value);
				}

				d[v] = clamp(value,0.0f,1.0f);
			}

			s += img.dim;
			d += dest.dim;
		}
	}
}

/*
 * Perform an unsharp mask on the region, given a source region, dest.
 * region, width and height of the regions.
 */
void unsharp_region(
		const CImgF &img, CImgF &dest,
                float radius, float amountup,
		float gamma, float amountdown, float threshold,
		float shadow, float midtone, float light, float high, volatile bool *pStopRequest, volatile LONG *pProgress)
{
double fstart = gettime(), ff;
	dest.assign(img);

ff = gettime(); printf("xxx 0: %f\n", ff - fstart); fstart = ff;
	if(gamma != 1)
		unsharp_region_apply_gamma(dest, gamma, pStopRequest, pProgress);
ff = gettime(); printf("xxx 1: %f\n", ff - fstart); fstart = ff;

	unsharp_region_apply_blur_horiz(dest, radius, pStopRequest, pProgress);
ff = gettime(); printf("xxx 2: %f\n", ff - fstart); fstart = ff;
	unsharp_region_apply_blur_vert(dest, radius, pStopRequest, pProgress);
ff = gettime(); printf("xxx 3: %f\n", ff - fstart); fstart = ff;

	if(gamma != 1)
		unsharp_region_apply_gamma(dest, 1.0f / gamma, pStopRequest, pProgress);
ff = gettime(); printf("xxx 4: %f\n", ff - fstart); fstart = ff;
	cimgI_for(dest, p)
	{
		*p = clamp(*p, 0.0f, 1.0f);
	}

ff = gettime(); printf("xxx 5: %f\n", ff - fstart); fstart = ff;
	unsharp_region_combine(img, dest, amountup, amountdown, threshold, shadow, midtone, light, high, pStopRequest, pProgress);
	
ff = gettime(); printf("xxx 6: %f\n", ff - fstart); fstart = ff;
}

/* Modifications from the original USM2 code are in the public domain. */

/*
 * Copyright (C) 2005 Peter Heckert
 *
 * peter /dot/ heckert /at/ arcor /dot/ de
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
