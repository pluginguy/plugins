#ifndef ALGORITHM_SHARED_H
#define ALGORITHM_SHARED_H

#include <string>
using namespace std;

/* Settings are configuration that affect the output. */
struct AlgorithmSettings
{
	AlgorithmSettings();
	string GetAsString() const;

	float m_fInputScale;
	float m_fPreBlur;
	float amplitude;
	float sharpness;
	float anisotropy;
	float alpha;
	float sigma;
	float gfact;
	float dl;
	float da;
	float gauss_prec;
	unsigned int interpolation;
	__int32 partial_stage_output;
	__int32 iterations;
	bool fast_approx;
	bool alt_amplitude;
};

/* Options are configuration that don't affect the output. */
struct AlgorithmOptions
{
	AlgorithmOptions();

	/* If zero, uses one thread per processor.  If positive, uses the specified number of
	 * threads.  If negative, uses fewer threads than processors. */
	int nb_threads;

	bool m_bGPU;

	enum DisplayMode
	{
		DISPLAY_SINGLE,
		DISPLAY_INSIDE,
		DISPLAY_SIDE_BY_SIDE
	};

	DisplayMode m_DisplayMode;
};

#endif
