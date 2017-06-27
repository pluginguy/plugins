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

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

extern FilterRecord *gFilterRecord;
extern SPBasicSuite *sSPBasic;

#endif
