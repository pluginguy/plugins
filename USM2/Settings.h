#ifndef SETTINGS_H
#define SETTINGS_H

#include "PIDefines.h"
#include "PIActions.h"
#include "PITerminology.h"

#ifndef Rez
#include "Photoshop.h"
#endif

#define vendorName		"Sharpen"
#define plugInName		"Unsharp Mask 2"
#define plugInAETEComment	"Unsharp Mask 2"
#define plugInSuiteID		'nsrp'
#define plugInClassID		plugInSuiteID
#define plugInEventID		plugInClassID
#define plugInUniqueID		"30444ab6-1a51-410d-b8f4-6fba246f8736"

#define plugInCopyrightYear	"2000"
#define plugInDescription	"Unsharp"

#define keyData			'datA'

#define keyAmountUp		'amtU'
#define keyAmountDown		'amtD'
#define keyHigh			'higH'
#define keyLight		'ligT'
#define keyMidtone		'mdtN'
#define keyShadow		'shdW'
#define keyPartialStageOutput	'pstO'
#define keyThreads		'thrD'
#define keyDisplayMode		'dspM'

#define keyIgnoreSelection	'ignS'
#define displayModeNormal	'dpM0'
#define displayModeInside	'dpM1'
#define displayModeSideBySide	'dpM2'

#define typeDisplayMode		'dspm'
#ifndef Rez
#include "Helpers.h"

void ReadScriptParameters(Parameters &params);
void WriteScriptParameters(const Parameters &params);
void ReadRegistryParameters(Parameters &params);
void WriteRegistryParameters(const Parameters &params);

#endif
#endif
