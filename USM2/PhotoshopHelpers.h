#ifndef PHOTOSHOP_HELPERS_H
#define PHOTOSHOP_HELPERS_H

#include "PIFilter.h"

#include "Helpers.h"
#include "SPTypes.h"

class PhotoshopErrorException: public exception
{
public:
	PhotoshopErrorException(int iErr) { m_iErr = iErr; }

	int m_iErr;
};

class ExceptionSPErr: public Exception
{
public:
	ExceptionSPErr(string sText, SPErr iErr) throw(): Exception(StringUtil::ssprintf("%s: %i", sText.c_str(), iErr))
	{
		m_iErr = iErr;
	}
	SPErr GetError() const { return m_iErr; }

private:
	SPErr m_iErr;
};

VPoint GetImageSize(FilterRecord *pFilterRecord);
VRect GetFilterRect(FilterRecord *pFilterRecord);
void SetInRect(FilterRecord *pFilterRecord, VRect inRect);
void SetOutRect(FilterRecord *pFilterRecord, VRect inRect);
void SetMaskRect(FilterRecord *pFilterRecord, VRect inRect);
class CImg;
void InitSourceImage(FilterRecord *pFilterRecord, CImg &Source, CImg &SourceMask);
void SetFromImage(FilterRecord *pFilterRecord, const CImg &img, CImg &dest);

extern CRITICAL_SECTION g_PSCritSec;
void PhotoshopInitLock();
void PhotoshopFreeLock();
void AddWindowThreadLocks();
void FreeWindowThreadLocks();

#endif
