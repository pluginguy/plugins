#include "AlgorithmShared.h"
#include "StringUtil.h"

AlgorithmSettings::AlgorithmSettings()
{
	/* The parameters for the algorithm are for an image with color components in the
	 * typical [0,255] range.  If the input scale is different, m_fInputScale is the
	 * factor to bring the data to that range.  (In practice, all this does is scale
	 * gfact.) */
	m_fInputScale = 1.0f;
	m_fPreBlur = 0.0f;
	amplitude = 60.0f;
	sharpness = 0.7f;
	anisotropy = 0.3f;
	alpha = 0.6f;
	sigma = 1.1f;
	gfact = 1.0f;
	dl = 0.8f;
	da = 30.0f;
	gauss_prec = 2.0f;
	interpolation = 0;
	partial_stage_output = 0;
	iterations = 1;
	fast_approx = true;
	alt_amplitude = true;
}

string AlgorithmSettings::GetAsString() const
{
	const AlgorithmSettings DefaultSettings;
	string sBuf;
#define TO_STR(name, fmt, digits) \
	if(name != DefaultSettings.name) { \
		if(!sBuf.empty()) sBuf += " "; \
		sBuf += StringUtil::ssprintf(fmt, (float)name) + " " + StringUtil::FormatFloat((float)name, digits); \
	}

	TO_STR(amplitude, "-dt", 3);
	TO_STR(sharpness, "-p", 3);
	TO_STR(anisotropy, "-a", 3);
	TO_STR(alpha, "-alpha", 3);
	TO_STR(sigma, "-sigma", 3);
	TO_STR(m_fPreBlur, "-gauss", 3);
	TO_STR(iterations, "-iter", 3);
	TO_STR(gfact, "-fact", 3);
	TO_STR(dl, "-dl", 3);
	TO_STR(da, "-da", 3);
	TO_STR(gauss_prec, "-prec", 3);
	TO_STR(interpolation, "-interp", 3);
	if(fast_approx)
	{
		if(!sBuf.empty()) sBuf += " ";
		sBuf += "-fast";
	}
	if(alt_amplitude)
	{
		if(!sBuf.empty()) sBuf += " ";
		sBuf += "-alt";
	}
	return sBuf;
}

AlgorithmOptions::AlgorithmOptions()
{
	nb_threads = 0;
	m_DisplayMode = DISPLAY_SINGLE;
	m_bGPU = false;
}

