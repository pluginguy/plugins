#include "StringUtil.h"
#include <string.h>
#include <stdarg.h>
#include <float.h>
#include <windows.h>

string StringUtil::vssprintf(const char *szFormat, va_list va)
{
	string sStr;

	char *pBuf = NULL;
	int iChars = 1;
	int iUsed = 0;
	int iTry = 0;

	do
	{
		iChars += iTry * 2048;
		pBuf = (char*) _alloca(sizeof(char)*iChars);
		iUsed = vsnprintf(pBuf, iChars-1, szFormat, va);
		++iTry;
	} while(iUsed < 0);

	// assign whatever we managed to format
	sStr.assign(pBuf, iUsed);
	return sStr;
}

string StringUtil::ssprintf(const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	return vssprintf(fmt, va);
}

string StringUtil::GetWindowsError(int iErr)
{
	char szBuf[1024] = "";
        int iLen = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, iErr, 0, szBuf, sizeof(szBuf), NULL);

	/* Work around an odd Windows bug: some error messages end with a CRLF. */
	if(iLen > 2 && szBuf[iLen-2] == '\r' && szBuf[iLen-1] == '\n')
		szBuf[iLen-2] = 0;
	return szBuf;
}

void StringUtil::Replace(string &s, const char *szOld, const char *szNew)
{
        int iOldLen = (int)strlen(szOld);
        if(iOldLen == 0)
                return;

	char empty[1] = {0};
	if(!szNew)
		szNew = empty;
        int iNewLen = (int)strlen(szNew);

        size_t iIdx = 0;
        while((iIdx=s.find(szOld, iIdx)) != string::npos)
        {
                s.replace(s.begin() + iIdx, s.begin() + iIdx + iOldLen, szNew);
                iIdx += iNewLen;
        }
}

#include <stdio.h>
string StringUtil::FormatFloat(float f, int iMaxDigitsAfterDecimal)
{
	char szBuf[1024];
	_snprintf(szBuf, sizeof(szBuf), "%.*f", iMaxDigitsAfterDecimal, f);
	szBuf[sizeof(szBuf-1)] = 0;

	string s = szBuf;
	if(iMaxDigitsAfterDecimal)
	{
		while(s.size() && s[s.size()-1] == '0')
			s.erase(s.end()-1, s.end());
		if(s[s.size()-1] == '.')
			s.erase(s.end()-1, s.end());
	}

	return s;
}
