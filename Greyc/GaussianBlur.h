#ifndef GAUSSIAN_BLUR_H
#define GAUSSIAN_BLUR_H

#include "CImgI.h"

void gaussian_blur_estimation(CImgF &i, float a, volatile bool *pStopRequest);

#endif
