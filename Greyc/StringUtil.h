#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <string>
#include <vector>
using namespace std;

namespace StringUtil
{
	string vssprintf(const char *szFormat, va_list argList);
	string ssprintf(const char *fmt, ...);
	string GetWindowsError(int iErr);
	void Replace(string &s, const char *szOld, const char *szNew);
	string FormatFloat(float f, int iMaxDigitsAfterDecimal = 6);
}

#endif
