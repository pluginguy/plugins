#ifndef PHOTOSHOP_H
#define PHOTOSHOP_H

#include "PIDefines.h"
#include "PITypes.h"

// Work around PITypes.h trying to #define true and false, which doesn't work.
#undef true
#undef false

#include "PIAbout.h"
#include "PIFilter.h"
#include "PIUtilities.h"
#include "Algorithm.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

/* These must both be POD types; Photoshop may move them or deallocate them without
 * notice when we're not active. */
typedef struct Parameters
{
	void Init();

	Algorithm::Settings FilterSettings;
	Algorithm::Options FilterOptions;
} Parameters;

extern FilterRecord *gFilterRecord;
extern SPBasicSuite *sSPBasic;
extern HINSTANCE g_hInstance;

#endif
