#ifndef UI_H
#define UI_H

#include "PreviewRenderer.h"

enum UIResult
{
	UI_CANCEL = 0,	// quit and don't save
	UI_SAVE_ONLY,	// save but don't apply to image
	UI_OK		// save and apply to image
};

UIResult DoUI(PreviewRenderer *pFilter);
bool ShowProgressDialog(Algorithm *pAlgo);
void DoAbout();

#endif
