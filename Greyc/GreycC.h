#ifndef GREYC_C_H
#define GREYC_C_H

#include "CImgI.h"
#include "Helpers.h"

void do_blur_anisotropic_prep(CImgF &img, CImgF &G, volatile bool *pStopRequest, volatile LONG *pProgress,
                        float fPreBlur, float alpha, float sigma, float geom_factor, int stage);

void do_blur_anisotropic(const CImgF &G, CImgF &G2, volatile bool *pStopRequest, volatile LONG *pProgress,
			Slices *pSlices, float sharpness, float anisotropy);

void do_blur_anisotropic_init_for_angle(const CImgF &G, CImgF &W, volatile bool *pStopRequest, volatile LONG *pProgress,
			Slices *pSlices, float theta, const float dl);

void do_blur_anisotropic_with_vectors_angle(CImgF &img, CImgF &W, const CImg &mask,  CImgF &dest,
			volatile bool *pStopRequest, volatile LONG *pProgress,
			Slices *pSlices,
			const bool alt_amplitude,
			const float amplitude,
			const float dl,
			const float gauss_prec, const unsigned int interpolation,
			const bool fast_approx);

void do_blur_anisotropic_finalize(const CImgF &dest, CImgF &img, int N,
	const CImg &mask, Slices *pSlices, volatile bool *pStopRequest);

#endif
