#ifndef GREYC_GPU_H
#define GREYC_GPU_H

#include "GPU.h"
#include "CImgI.h"
#include <windows.h>

class GreycGPU
{
public:
	void ProcessAnisotropic(CImgF &img, const CImgF &G,
		volatile bool *pStopRequest, volatile LONG *pProgress,
		const bool alt_amplitude,
		const float amplitude,
		const float da,
		const float dl,
		const float gauss_prec,
		const float sharpness,
		const float anisotropy,
		const unsigned int interpolation,
		const bool fast_approx,
		const bool bHighBitDepth);

	void Cleanup();

private:
	void gpu_blur_anisotropic_main(const GPUTexture &SourceImage, const CImgF &G,
		volatile bool *pStopRequest, volatile LONG *pProgress,
		int iWidth,
		int iHeight,
		const bool alt_amplitude,
		const float amplitude,
		const float da,
		const float dl,
		const float gauss_prec,
		const float sharpness,
		const float anisotropy,
		const unsigned int interpolation,
		const bool fast_approx,
		bool bHighBitDepth);

	void InitShaders();

	// void GPUDeriche(const GPUTexture &image, int iWidth, int iHeight, float sigma, int iInternalFormat);

	GPU gpu;
	GPUShader IIRShader;
	GPUShader EigenShader;
	GPUShader PrepShader;
	GPUShader MainShader;
	GPUShader MultShader;

};

#endif
