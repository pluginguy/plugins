#include "DericheBlur.h"
#include "Helpers.h"
#include "CImgI.h"
#include <math.h>

void deriche_SSE_row_X_fwd(float *ptrX, float *ptrY, int iWidth, float a0, float a1, float b1, float b2, float coefp, int iStrideBytes)
{
	/* can avoid loads; benchmark to see if it's worth it
	 * also, emms */
/*	__asm
	{
		movd mm0, a0
		movd mm1, a1
		movd mm2, b1
		movd mm3, b2
	}
*/	__asm
	{
		push eax
		push ebx
		push ecx
		push edx
		push esi

		mov eax, ptrX
		mov edx, ptrY
		mov esi, iWidth

		mov ebx, coefp
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0

		// xmm4: xp = *ptrX
		movaps xmm4, [eax]
		// xmm2: yb = xp*coefp
		movaps xmm2, xmm4
		mulps xmm2, xmm0

		// xmm3: yp = xp*coefp
		movaps xmm3, xmm2

		// const float yc = a0*xc + a1*xp + b1*yp + b2*yb
do_loop:
		// xmm1: xc
		movaps xmm1, [eax]
		add eax, [iStrideBytes]
		
		// xmm5 = a0*xc
		movaps xmm5, xmm1
		mov ebx, a0
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm5, xmm0

		// xmm5 += a1*xp
		movaps xmm7, xmm4
		mov ebx, a1
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm7, xmm0
		addps xmm5, xmm7

		// xmm5 -= b1*yp
		movaps xmm7, xmm3
		mov ebx, b1
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm7, xmm0
		subps xmm5, xmm7

		// xmm5 -= b2*yb
		movaps xmm7, xmm2
		mov ebx, b2
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm7, xmm0
		subps xmm5, xmm7
		// xmm5 = yc

		// *ptrY = yc
		movaps [edx], xmm5
		add edx, 16			// stride in Y is always 4 floats

		movaps xmm4, xmm1 // xp = xc
		movaps xmm2, xmm3 // yb = yp
		movaps xmm3, xmm5 // yp = yc

		dec esi
		jnz do_loop

		pop esi
		pop edx
		pop ecx
		pop ebx
		pop eax
	}
}

void deriche_SSE_row_X_rev(float *ptrX, float *ptrY, int iWidth, float a2, float a3, float b1, float b2, float coefn, int iStrideBytes)
{
	__asm
	{
		mov eax, ptrX
		mov edx, ptrY
		mov esi, iWidth

		mov ebx, coefn
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0

		// xmm1: xn = *ptrX
		movaps xmm4, [eax]
		// xmm4: xa = xn
		movaps xmm1, xmm4
		// xmm3: yn = xn*coefn
		movaps xmm3, xmm4
		mulps xmm3, xmm0
		// xmm2: ya = yn
		movaps xmm2, xmm3

		// const float yc = a2*xn + a3*xa - b1*yn - b2*ya
do_loop:
		// xmm6: xc
		movaps xmm6, [eax]
		
		// xmm5 = a2*xn
		movaps xmm5, xmm4
		mov ebx, a2
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm5, xmm0

		// xmm5 += a3*xa
		movaps xmm7, xmm1
		mov ebx, a3
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm7, xmm0
		addps xmm5, xmm7

		// xmm5 -= b1*yn
		movaps xmm7, xmm3
		mov ebx, b1
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm7, xmm0
		subps xmm5, xmm7

		// xmm5 -= b2*ya
		movaps xmm7, xmm2
		mov ebx, b2
		movd xmm0, ebx
		punpckldq xmm0, xmm0
		punpckldq xmm0, xmm0
		mulps xmm7, xmm0
		subps xmm5, xmm7
		// xmm5 = yc

		// *ptrX = *ptrY+yc;
		movaps xmm1, [edx]	// *ptrY
		addps xmm1, xmm5	// += yc
		movaps [eax], xmm1

		sub eax, [iStrideBytes]
		sub edx, 16

		movaps xmm1, xmm4 // xa = xn
		movaps xmm4, xmm6 // xn = xc
		movaps xmm2, xmm3 // ya = yn
		movaps xmm3, xmm5 // yn = yc

		dec esi
		jnz do_loop
	}
}

void deriche_C_row_X_fwd(float *ptrX_, float *ptrY_, int iWidth, float a0, float a1, float b1, float b2, float coefp, int iStride, int iChannels)
{
	for(int v = 0; v < iChannels; ++v)
	{
		float *ptrX = ptrX_ + v;
		float *ptrY = ptrY_ + v;

		float xp = *ptrX;
		float yb = coefp*xp;
		float yp = yb;
		for (int m=0; m<iWidth; ++m)
		{
			const float xc = *ptrX;
			ptrX += iStride;
			const float yc = *ptrY = a0*xc + a1*xp - b1*yp - b2*yb;
			ptrY += iChannels;
			xp = xc; yb = yp; yp = yc;
		}
	}
}

void deriche_C_row_X_rev(float *ptrX_, float *ptrY_, int iWidth, float a2, float a3, float b1, float b2, float coefn, int iStride, int iChannels)
{
	for(int v = 0; v < iChannels; ++v)
	{
		float *ptrX = ptrX_ + v;
		float *ptrY = ptrY_ + v;

		float xn = *ptrX;
		float xa = xn;
		float yn = coefn*xn;
		float ya = yn;
		for (int n=iWidth-1; n>=0; --n)
		{
			const float xc = *ptrX;
			const float yc = a2*xn + a3*xa - b1*yn - b2*ya;
			xa = xn; xn = xc; ya = yn; yn = yc;
			*ptrX = *ptrY+yc;
			ptrY -= iChannels;
			ptrX -= iStride;
		}
	}
}

void deriche(CImgF &img, const float sigma, const char axe)
{
	if (img.is_empty() || sigma<0.1) return;
	const float nsigma = sigma<0.1f?0.1f:sigma;
        const float alpha = 1.695f/nsigma;
        const float ema = expf(-alpha);
        const float ema2 = expf(-2*alpha);
        const float b1 = -2*ema;
        const float b2 = ema2;
        const float k = (1-ema)*(1-ema)/(1+2*alpha*ema-ema2);
        const float a0 = k;
        const float a1 = k*(alpha-1)*ema;
        const float a2 = k*(alpha+1)*ema;
        const float a3 = -k*ema2;
	const float coefp = (a0+a1)/(1+b1+b2);
	const float coefn = (a2+a3)/(1+b1+b2);

	bool bSSE = !!(GetCPUID() & CPUID_SSE);
	if(img.dim != 4)
		bSSE = false;
	if(img.stride & 0x3) // not aligned
		bSSE = false;

	CImgF Y;
	Y.alloc(max(img.height, img.width), 1, img.dim);
	switch(axe)
	{
	case 'x':
		cimgI_forY(img,y)
		{
			if(bSSE)
				deriche_SSE_row_X_fwd(img.ptr(0,y,0), Y.ptr(0,0,0), img.width, a0, a1, b1, b2, coefp, img.dim * sizeof(float));
			else
				deriche_C_row_X_fwd(img.ptr(0,y,0), Y.ptr(0,0,0), img.width, a0, a1, b1, b2, coefp, img.dim, img.dim);

			if(bSSE)
				deriche_SSE_row_X_rev(img.ptr(img.width-1,y,0), Y.ptr(img.width-1,0,0), img.width, a2, a3, b1, b2, coefn, img.dim * sizeof(float));
			else
				deriche_C_row_X_rev(img.ptr(img.width-1,y,0), Y.ptr(img.width-1,0,0), img.width, a2, a3, b1, b2, coefn, img.dim, img.dim);
		}
		break;
	case 'y':
		cimgI_forX(img,x)
		{
			if(bSSE)
				deriche_SSE_row_X_fwd(img.ptr(x,0,0), Y.ptr(0,0,0), img.height, a0, a1, b1, b2, coefp, img.stride * sizeof(float));
			else
				deriche_C_row_X_fwd(img.ptr(x,0,0), Y.ptr(0,0,0), img.height, a0, a1, b1, b2, coefp, img.stride, img.dim);

			if(bSSE)
				deriche_SSE_row_X_rev(img.ptr(x,img.height-1,0), Y.ptr(img.height-1,0,0), img.height, a2, a3, b1, b2, coefn, img.stride * sizeof(float));
			else
				deriche_C_row_X_rev(img.ptr(x,img.height-1,0), Y.ptr(img.height-1,0,0), img.height, a2, a3, b1, b2, coefn, img.stride, img.dim);
		}
		break;
	}
}

void deriche(CImgF &img, const float sigma)
{
	deriche(img, sigma, 'x');
	deriche(img, sigma, 'y');
}
