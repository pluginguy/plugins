#ifndef UNSHARP_H
#define UNSHARP_H

#include "CImgI.h"
#include "Helpers.h"

void unsharp_region(
		const CImgF &img, CImgF &dest,
                float radius, float amount,
		float gamma, float threshold, float threshold2,
		float shadow, float midtone, float light, float high, volatile bool *pStopRequest, volatile LONG *pProgress);

#endif
