#ifndef HELPERS_H
#define HELPERS_H

#include "StringUtil.h"

#include <exception>
#include <string>
#include <windows.h>
#include <intrin.h>
using namespace std;

#pragma intrinsic (_InterlockedIncrement)
#define InterlockedIncrement _InterlockedIncrement

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

template<typename T>
inline T clamp(T i, T low, T high)
{
	return max(low, min(i, high));
}

class Exception: public exception
{
public:
        Exception(string sMessage) throw() { m_sMessage = sMessage; }
        virtual ~Exception() throw() { }
        virtual Exception *Copy() const { return new Exception(*this); }
        const char *what() const throw() { return m_sMessage.c_str(); }
        string GetMessage() const { return m_sMessage; }

private:
        string m_sMessage;
};

class Instance
{
public:
	Instance();
	~Instance();

	HINSTANCE Get() const { return m_hAppInstance; }

private:
	HINSTANCE m_hAppInstance;
};


/*
 * To divide work across threads, we split the image into rows.  Threads call GetSlice to be
 * assigned a row to process.
 *
 * We don't simply split the image into one large slice per thread, because if one thread
 * is working more slowly than the rest (eg. due to CPU contention with some other process,
 * or due to some parts of the image being more expensive to handle), then it'll bottleneck
 * the whole process.  For example, if threads 1, 2 and 3 take 10 seconds to complete their
 * work, and thread 4 takes 20 seconds, then the rest of the threads will be idle for 10
 * seconds, instead of splitting the work more evenly.
 *
 * Returns true if iStartY is set.  If there is no more work to do, returns false.
 */

class Slices
{
public:
	void Init(int iDimY);
	bool Get(int &iStartY);
	void Reset();

private:
	int m_iDimY;
	volatile LONG m_iNextY;
};

void SetThreadName(int iThreadNo);
bool ThisProcessHasFocus();
void SetWindowStyle(HWND hWnd, int iMask);
void ClearWindowStyle(HWND hWnd, int iMask);
void SetClipboardFromString(HWND hWnd, string s);
double gettime();

#define CPUID_MMX	0x00800000
#define CPUID_SSE	0x02000000
#define CPUID_SSE2	0x04000000
#if !defined(_WIN64)
int GetCPUID();
#endif

#if defined(_WIN64)
inline long int lrintf(float f)
{
        return f >= 0.0f ? (int)floorf(f + 0.5f) : (int)ceilf(f - 0.5f);
}
#else
inline long int lrintf(float f)
{
	int retval;
	_asm fld f;
	_asm fistp retval;
	return retval;
}
#endif

template<typename T>
T align(T val, unsigned n)
{
	unsigned i = (unsigned) val;
	i += n - 1;
	i -= i % n;
	return (T) i;
}

static inline float scale(float x, float l1, float h1, float l2, float h2)
{
	return (x - l1) * (h2 - l2) / (h1 - l1) + l2;
}

#define progress { if(pProgress) InterlockedIncrement(pProgress); }
#define check_cancel { if (*pStopRequest) { printf("cancelled\n"); return; } }
#define progress_and_check_cancel { check_cancel; progress; }

#define M_PI 3.1415926535897932384


/* exp2f4 and log2f4 from http://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html */
#define EXP_POLY_DEGREE 3

#define POLY0(x, c0) _mm_set1_ps(c0)
#define POLY1(x, c0, c1) _mm_add_ps(_mm_mul_ps(POLY0(x, c1), x), _mm_set1_ps(c0))
#define POLY2(x, c0, c1, c2) _mm_add_ps(_mm_mul_ps(POLY1(x, c1, c2), x), _mm_set1_ps(c0))
#define POLY3(x, c0, c1, c2, c3) _mm_add_ps(_mm_mul_ps(POLY2(x, c1, c2, c3), x), _mm_set1_ps(c0))
#define POLY4(x, c0, c1, c2, c3, c4) _mm_add_ps(_mm_mul_ps(POLY3(x, c1, c2, c3, c4), x), _mm_set1_ps(c0))
#define POLY5(x, c0, c1, c2, c3, c4, c5) _mm_add_ps(_mm_mul_ps(POLY4(x, c1, c2, c3, c4, c5), x), _mm_set1_ps(c0))
static inline __m128 exp2f4(__m128 x)
{
	__m128i ipart;
	__m128 fpart, expipart, expfpart;

	x = _mm_min_ps(x, _mm_set1_ps( 129.00000f));
	x = _mm_max_ps(x, _mm_set1_ps(-126.99999f));

	/* ipart = int(x - 0.5) */
	ipart = _mm_cvtps_epi32(_mm_sub_ps(x, _mm_set1_ps(0.5f)));

	/* fpart = x - ipart */
	fpart = _mm_sub_ps(x, _mm_cvtepi32_ps(ipart));

	/* expipart = (float) (1 << ipart) */
	expipart = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(ipart, _mm_set1_epi32(127)), 23));

	/* minimax polynomial fit of 2**x, in range [-0.5, 0.5[ */
#if EXP_POLY_DEGREE == 5
	expfpart = POLY5(fpart, 9.9999994e-1f, 6.9315308e-1f, 2.4015361e-1f, 5.5826318e-2f, 8.9893397e-3f, 1.8775767e-3f);
#elif EXP_POLY_DEGREE == 4
	expfpart = POLY4(fpart, 1.0000026f, 6.9300383e-1f, 2.4144275e-1f, 5.2011464e-2f, 1.3534167e-2f);
#elif EXP_POLY_DEGREE == 3
	expfpart = POLY3(fpart, 9.9992520e-1f, 6.9583356e-1f, 2.2606716e-1f, 7.8024521e-2f);
#elif EXP_POLY_DEGREE == 2
	expfpart = POLY2(fpart, 1.0017247f, 6.5763628e-1f, 3.3718944e-1f);
#else
#error
#endif

   return _mm_mul_ps(expipart, expfpart);
}

#define LOG_POLY_DEGREE 5

static inline __m128 log2f4(__m128 x)
{
	__m128i exp = _mm_set1_epi32(0x7F800000);
	__m128i mant = _mm_set1_epi32(0x007FFFFF);

	__m128 one = _mm_set1_ps( 1.0f);

	__m128i i = _mm_castps_si128(x);

	__m128 e = _mm_cvtepi32_ps(_mm_sub_epi32(_mm_srli_epi32(_mm_and_si128(i, exp), 23), _mm_set1_epi32(127)));

	__m128 m = _mm_or_ps(_mm_castsi128_ps(_mm_and_si128(i, mant)), one);

	__m128 p;

	/* Minimax polynomial fit of log2(x)/(x - 1), for x in range [1, 2[ */
#if LOG_POLY_DEGREE == 6
	p = POLY5( m, 3.1157899f, -3.3241990f, 2.5988452f, -1.2315303f,  3.1821337e-1f, -3.4436006e-2f);
#elif LOG_POLY_DEGREE == 5
	p = POLY4(m, 2.8882704548164776201f, -2.52074962577807006663f, 1.48116647521213171641f, -0.465725644288844778798f, 0.0596515482674574969533f);
#elif LOG_POLY_DEGREE == 4
	p = POLY3(m, 2.61761038894603480148f, -1.75647175389045657003f, 0.688243882994381274313f, -0.107254423828329604454f);
#elif LOG_POLY_DEGREE == 3
	p = POLY2(m, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f);
#else
#error
#endif

	/* This effectively increases the polynomial degree by one, but ensures that log2(1) == 0*/
	p = _mm_mul_ps(p, _mm_sub_ps(m, one));

	return _mm_add_ps(p, e);
}

static inline __m128 powf_sse2(__m128 x, __m128 y)
{
	__m128 xlog = log2f4(x);
	__m128 xlogy = _mm_mul_ps(xlog, y);

	return exp2f4(xlogy);
}

void ScaleArea(float f, int &iX, int &iY, int &iWidth, int &iHeight);

void Blit8bY_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	bool bWithAlpha,
	int iInStride, int iOutStride);
void Blit8bRGB_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	int iInStride, int iOutStride);
void Blit8bRGBA_8bBGRA(const uint8_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iInChannels,
	int iInStride, int iOutStride);
void Blit16b_8b(const uint16_t *pInBuf, uint8_t *pOutBuf,
	int iWidth, int iHeight,
	int iChannels,
	int iInStride, int iOutStride);

#endif
