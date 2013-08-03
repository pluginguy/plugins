#ifndef SCRIPTING_H
#define SCRIPTING_H

#include "PhotoshopKeys.h"
#include "Algorithm.h"

typedef struct Parameters
{
	void Init();

	AlgorithmSettings FilterSettings;
	AlgorithmOptions FilterOptions;
} Parameters;

void ReadScriptParameters(Parameters &params);
void WriteScriptParameters(const Parameters &params);
void ReadRegistryParameters(Parameters &params);
void WriteRegistryParameters(const Parameters &params);

#endif
