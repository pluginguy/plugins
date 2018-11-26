/* Implement the main dialog interface. */

#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define _WIN32_WINDOWS 0x0400

#include "Photoshop.h"
#include "PhotoshopHelpers.h"
#include "UI.h"
#include "Settings.h"
#include "FilterBigDocument.h"
#include "resource.h"
#include "Helpers.h"
#include <math.h>
#include <assert.h>
#include <commctrl.h>
#include <algorithm>
using namespace std;

struct UIData
{
	UIData();
	void PaintProxy(HWND hDlg);
	void RedrawProxyItem(HWND hDlg);
	void UpdateProgressMeter(HWND hDlg);
	void UpdateProxyItem(HWND hDlg);
	void ApplyOptions(HWND hDlg);
	bool ChangeZoom(bool bIn);
	float GetPreviewZoom();
	void PaintImage(HWND hDlg, const PreviewRenderer::RenderedPreview &image);
	void SetPreviewPosition(HWND hDlg, int iX, int iY);
	void SetDlgItems(HWND hDlg, const Algorithm::Settings &s, const Algorithm::Options &o);
	int UpdateThreadInput(HWND hDlg);
	void UpdateDisplayAfterSettingsChange(HWND hDlg);

	PreviewRenderer *m_pFilter;
	bool bRightButtonHeld;
	bool bDraggingPreview;
	bool bDraggingSnapped;
	HCURSOR g_hCursorHand;
	HCURSOR g_hOldCursor;
	int iPreviewX, iPreviewY;
	int iDragAnchorX;
	int iDragAnchorY;
	int iDragOrigX;
	int iDragOrigY;
	UIResult iUIResult;
	char szExceptionMessage[1024];
	bool bAltIsHeld;
	bool bAltKeypressWasUsed;
	int g_iFocusedEditControl;
	int iPreviewZoomStep;
};

UIData::UIData()
{
	m_pFilter = NULL;
	bRightButtonHeld = false;
	bDraggingPreview = false;
	bDraggingSnapped = false;
	g_hCursorHand = NULL;
	g_hOldCursor = NULL;
	iPreviewX = 0, iPreviewY = 0;
	iDragAnchorX = 0;
	iDragAnchorY = 0;
	iDragOrigX = 0;
	iDragOrigY = 0;
	iUIResult = UI_CANCEL;
	szExceptionMessage[0] = 0;
	bAltIsHeld = false;
	bAltKeypressWasUsed = false;
	g_iFocusedEditControl = -1;
	iPreviewZoomStep = 0;
}

namespace
{
	void ShowAboutDialog(HWND hParent)
	{
		MessageBox(hParent, "Original USM2 by Peter Heckert, GPL.", plugInName, MB_OK);
	}
}

	bool UIData::ChangeZoom(bool bIn)
	{
		int iOld = iPreviewZoomStep;
		if(bIn)
			++iPreviewZoomStep;
		else
			--iPreviewZoomStep;
		iPreviewZoomStep = min(iPreviewZoomStep, +10);
		iPreviewZoomStep = max(iPreviewZoomStep, -10);
		if(iPreviewZoomStep == iOld)
			return false;

		return true;
	}
	float UIData::GetPreviewZoom()
	{
		float fZoom[] =
		{
			0.38f,
			0.42f,
			0.46f,
			0.51f,
			0.56f,
			0.62f,
			0.68f,
			0.75f,
			0.82f,
			0.9f,
			1.0f, // 0
			1.1f,
			1.21f,
			1.33f,
			1.46f,
			1.61f,
			1.77f,
			1.95f,
			2.14f,
			2.36f,
			3.59f
		};

		return fZoom[iPreviewZoomStep + 10];
	}

	void UIData::PaintImage(HWND hDlg, const PreviewRenderer::RenderedPreview &image)
	{
		RECT wRect;
		GetClientRect(hDlg, &wRect);

		PAINTSTRUCT ps;
		HDC hDC = BeginPaint(hDlg, &ps);	

		/* Paint the black frame. */
		FrameRect(hDC, &wRect, (HBRUSH) GetStockObject(BLACK_BRUSH));	
		InflateRect(&wRect, -1, -1);
		BITMAPINFO bi;
		memset(&bi, 0, sizeof(bi));
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = image.PreviewImage.m_iWidth;
		bi.bmiHeader.biHeight = image.PreviewImage.m_iHeight;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		/*
		 * Render image.PreviewImage into hDC.  The draw area is wRect.  Zoom the display
		 * by GetPreviewZoom().  The top-left source pixel to draw is iImageX/iImageY.
		 *
		 * StretchDIBits makes this a pain.  We can handle the scaling by scaling the source
		 * dimensions, but then we won't know the exact bounds of what's being rendered if
		 * it doesn't fill the area completely; we can calculate it, but the rounding isn't
		 * clearly defined, so that's error-prone.
		 *
		 * If the zoom won't fill the area, then handle the scaling by adjusting wRect.  Otherwise,
		 * handle it by adjusting iSourceWidth.
		 */
		int iImageX = iPreviewX - image.iX;
		int iImageY = iPreviewY - image.iY;
		int iDestX = wRect.left;
		int iDestY = wRect.top;

		int iDestWidth = wRect.right - wRect.left;
		int iDestHeight = wRect.bottom - wRect.top;
		int iSourceImageWidth = image.PreviewImage.m_iWidth - iImageX;
		int iSourceImageHeight = image.PreviewImage.m_iHeight - iImageY;
		if(iPreviewX < 0)
		{
			iDestX += int(-iPreviewX * GetPreviewZoom());
			iImageX = 0;
			iDestWidth -= int(-iPreviewX * GetPreviewZoom());
			iSourceImageWidth -= -iPreviewX;
		}
		if(iPreviewY < 0)
		{
			iDestY += int(-iPreviewY * GetPreviewZoom());
			iImageY = 0;
			iDestHeight -= int(-iPreviewY * GetPreviewZoom());
			iSourceImageHeight -= -iPreviewY;
		}

		int iSourceWidth;
		if(iSourceImageWidth >= lrintf(iDestWidth / GetPreviewZoom()))
		{
			iSourceWidth = lrintf(iDestWidth / GetPreviewZoom());
		}
		else
		{
			iSourceWidth = iSourceImageWidth;
			iDestWidth = lrintf(iSourceImageWidth * GetPreviewZoom());
		}

		int iSourceHeight;
		if(iSourceImageHeight >= lrintf(iDestHeight / GetPreviewZoom()))
		{
			iSourceHeight = lrintf(iDestHeight / GetPreviewZoom());
		}
		else
		{
			iSourceHeight = iSourceImageHeight;
			iDestHeight = lrintf(iSourceImageHeight * GetPreviewZoom());
		}

		if(iDestWidth > 0 && iDestHeight > 0)
		{
			int i = StretchDIBits(
				hDC,
				iDestX, iDestY,					// destination upper-left corner
				iDestWidth, iDestHeight,			// size of destination rectangle
				iImageX, iSourceHeight+1+iImageY,		// source upper-left corner
				iSourceWidth, -iSourceHeight,			// size of source rectangle
				image.PreviewImage.m_pData, &bi, DIB_RGB_COLORS, SRCCOPY);
			if(i == GDI_ERROR)
			{
				int q = 1;
			}
		}

		/* If we aren't filling the whole window, clear the unused area.  Don't just clear the
		 * whole area before blitting; it'll cause flicker. */
		{
			RECT BottomEmptyRect(wRect);
			BottomEmptyRect.top = max(BottomEmptyRect.top, iDestY + iDestHeight);
			if(BottomEmptyRect.bottom > BottomEmptyRect.top)
				FillRect(hDC, &BottomEmptyRect, (HBRUSH) GetStockObject(BLACK_BRUSH));

			RECT RightEmptyRect(wRect);
			RightEmptyRect.left = max(RightEmptyRect.left, iDestX + iDestWidth);
			if(RightEmptyRect.right > RightEmptyRect.left)
				FillRect(hDC, &RightEmptyRect, (HBRUSH) GetStockObject(BLACK_BRUSH));	

			RECT LeftEmptyRect(wRect);
			LeftEmptyRect.right = min(LeftEmptyRect.right, iDestX);
			if(LeftEmptyRect.right > LeftEmptyRect.left)
				FillRect(hDC, &LeftEmptyRect, (HBRUSH) GetStockObject(BLACK_BRUSH));

			RECT TopEmptyRect(wRect);
			TopEmptyRect.bottom = min(TopEmptyRect.bottom , iDestY);
			if(TopEmptyRect.bottom > TopEmptyRect.top)
				FillRect(hDC, &TopEmptyRect, (HBRUSH) GetStockObject(BLACK_BRUSH));
		}

		EndPaint(hDlg, (LPPAINTSTRUCT) &ps);
	}

	void UIData::PaintProxy(HWND hDlg)
	{
		/* If the mouse is down on the preview, or if the preview is still rendering, draw
		 * the original image. */
		if(bDraggingPreview)
		{
			/* When we're dragging the image around, always draw the original image. */
			PaintImage(hDlg, m_pFilter->Image);
		}
		else if(m_pFilter->m_bAlgorithmPending && !m_pFilter->LastPreview.PreviewImage.Empty())
			PaintImage(hDlg, m_pFilter->LastPreview);
		else if(!m_pFilter->CurrentPreview.PreviewImage.Empty())
			PaintImage(hDlg, m_pFilter->CurrentPreview);
		else
			PaintImage(hDlg, m_pFilter->Image);
	}

	void UIData::RedrawProxyItem(HWND hDlg)
	{
		InvalidateRect(GetDlgItem(hDlg, ID_PROXY_ITEM), NULL, FALSE);
	}

	void UIData::UpdateProgressMeter(HWND hDlg)
	{
		float fPercentDone;
		if(m_pFilter->CheckPreviewCompletion(fPercentDone))
		{
			RedrawProxyItem(hDlg);
			ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS), SW_HIDE);
		}
		else
		{
			int i = int(fPercentDone * 1000);
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETPOS, i, 0);
		}
	}

namespace
{
	struct AlgorithmFinishedSignalCallback: public Algorithm::Callbacks
	{
		AlgorithmFinishedSignalCallback(HWND hDlg)
		{
			m_hDlg = hDlg;
		}

		void Finished()
		{
			PostMessage(m_hDlg, WM_TIMER, 1, 0);
		}

	private:
		HWND m_hDlg;
	};
}

	void UIData::UpdateProxyItem(HWND hDlg)
	{
		auto_ptr<Algorithm::Callbacks> pCallbacks(new AlgorithmFinishedSignalCallback(hDlg));
		m_pFilter->UpdateProxyBuffer(pCallbacks);

		if(m_pFilter->IsPreviewRunning())
		{
			UpdateProgressMeter(hDlg);
			ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS), SW_SHOW);
		}
		else
		{
			ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS), SW_HIDE);
		}
		RedrawProxyItem(hDlg);
	}

	void UIData::ApplyOptions(HWND hDlg)
	{
		m_pFilter->FilterOptions = m_pFilter->CurrentOptions;
		UpdateProxyItem(hDlg);
	}

namespace
{
	void SetDlgItemFloat(HWND hDlg, int ID, const char *szFmt, float fValue)
	{
		char buf[1024];
		_snprintf(buf, 1024, szFmt, fValue);
		SetDlgItemText(hDlg, ID, buf);
	}

	float GetDlgItemFloat(HWND hDlg, int ID)
	{
		char buf[1024];
		GetDlgItemText(hDlg, ID, buf, sizeof(buf));

		float fRet = 0;
		sscanf(buf, "%f", &fRet);
		return fRet;
	}

	bool CoordInDlgItem(HWND hDlg, int ID, LPARAM lParam, bool bScreenCoords, int *iX = NULL, int *iY = NULL)
	{
		RECT wRect;
		if(bScreenCoords)
			GetWindowRect(GetDlgItem(hDlg, ID), &wRect);	
		else
			GetClientRect(GetDlgItem(hDlg, ID), &wRect);	

		short iXCoord = LOWORD(lParam);
		short iYCoord = HIWORD(lParam);
		if(iXCoord < wRect.left || iXCoord > wRect.right || iYCoord < wRect.top || iYCoord > wRect.bottom)
			return false;

		if(iX)
			*iX = iXCoord - wRect.left;
		if(iY)
			*iY = iYCoord - wRect.top;
		return true;
	}
}

	void UIData::SetPreviewPosition(HWND hDlg, int iX, int iY)
	{
		RECT wRect;
		GetClientRect(GetDlgItem(hDlg, ID_PROXY_ITEM), &wRect);	

		int iPreviewWidth = wRect.right - wRect.left - 2; // - 2 for border
		int iPreviewHeight = wRect.bottom - wRect.top - 2;
		iPreviewWidth = lrintf(iPreviewWidth / GetPreviewZoom());
		iPreviewHeight = lrintf(iPreviewHeight / GetPreviewZoom());

		iPreviewWidth = wRect.right - wRect.left - 2; // - 2 for border
		iPreviewHeight = wRect.bottom - wRect.top - 2;
		iPreviewWidth = lrintf(iPreviewWidth / GetPreviewZoom());
		iPreviewHeight = lrintf(iPreviewHeight / GetPreviewZoom());

		iPreviewX = iX;
		iPreviewY = iY;
		m_pFilter->SetPreviewPosition(iX, iY, iPreviewWidth, iPreviewHeight);
	}

	void UIData::SetDlgItems(HWND hDlg, const Algorithm::Settings &s, const Algorithm::Options &o)
	{
		SetDlgItemFloat(hDlg, IDC_EDIT_RADIUS,		"%-.3g", s.fRadius);
		SetDlgItemFloat(hDlg, IDC_EDIT_AMOUNT_UP,	"%-.3g", s.fAmountUp);
		SetDlgItemFloat(hDlg, IDC_EDIT_GAMMA,		"%-.3g", s.fGamma);
		SetDlgItemFloat(hDlg, IDC_EDIT_AMOUNT_DOWN,	"%-.3g", s.fAmountDown);
		SetDlgItemFloat(hDlg, IDC_EDIT_THRESHOLD,	"%-.3g", s.fThreshold);
		SetDlgItemFloat(hDlg, IDC_EDIT_HIGHLIGHT_NOISE,	"%-.3g", s.fHigh);
		SetDlgItemFloat(hDlg, IDC_EDIT_LIGHT_NOISE,	"%-.3g", s.fLight);
		SetDlgItemFloat(hDlg, IDC_EDIT_MIDTONE_NOISE,	"%-.3g", s.fMidtone);
		SetDlgItemFloat(hDlg, IDC_EDIT_SHADOW_NOISE,	"%-.3g", s.fShadow);
		SendMessage(GetDlgItem(hDlg, IDC_DISPLAY_MODE), CB_SETCURSEL, o.m_DisplayMode, 0);
	}	

	int UIData::UpdateThreadInput(HWND hDlg)
	{
		char buf[1024];
		GetDlgItemText(hDlg, IDC_THREADS, buf, sizeof(buf));
		if(!strcmp(buf, "Auto"))
			return 0;
		else if(!strcmp(buf, "Auto-1"))
			return -1;
		else
		{
			int iRet = atoi(buf);
			iRet = max(iRet, 1);
			iRet = min(iRet, 32);
			return iRet;
		}
	}

	void UIData::UpdateDisplayAfterSettingsChange(HWND hDlg)
	{
		m_pFilter->FilterSettings = m_pFilter->CurrentSettings;
		m_pFilter->FilterOptions = m_pFilter->CurrentOptions;
		UpdateProxyItem(hDlg);
	}

namespace
{
	bool IsIntControl(int item)
	{
/*		switch(item)
		{
		case IDC_EDIT_ITERATIONS:
			return true;
		}
*/		return false;
	}

	int *GetIntControl(Algorithm::Settings &pSettings, int item)
	{
/*		switch(item)
		{
		case IDC_EDIT_ITERATIONS:       return &pSettings.iterations;
		}
*/		assert(false);
		return NULL;
	}
	float *GetFloatControl(Algorithm::Settings &pSettings, int item)
	{
		switch(item)
		{
		case IDC_EDIT_RADIUS:		return &pSettings.fRadius;
		case IDC_EDIT_AMOUNT_UP:	return &pSettings.fAmountUp;
		case IDC_EDIT_AMOUNT_DOWN:	return &pSettings.fAmountDown;
		case IDC_EDIT_GAMMA:		return &pSettings.fGamma;
		case IDC_EDIT_THRESHOLD:	return &pSettings.fThreshold;
		case IDC_EDIT_HIGHLIGHT_NOISE:	return &pSettings.fHigh;
		case IDC_EDIT_LIGHT_NOISE:	return &pSettings.fLight;
		case IDC_EDIT_MIDTONE_NOISE:	return &pSettings.fMidtone;
		case IDC_EDIT_SHADOW_NOISE:	return &pSettings.fShadow;
		}
		assert(false);
		return NULL;
	}

	struct EditControlData
	{
		int iIDC;
		bool bIntegerControl;
		
		bool bClampBottom;
		float fBottom;

		bool bClampTop;
		float fTop;

		float fMouseWheelDelta;
	};
	EditControlData Controls[] =
	{
		{ IDC_EDIT_RADIUS,		false,	true, 0.25f,	true, 500.0f,	0.1f },
		{ IDC_EDIT_AMOUNT_UP,		false,	true, 0,	true, 10,	0.1f },
		{ IDC_EDIT_AMOUNT_DOWN,		false,	true, 0,	true, 10,	0.1f },
		{ IDC_EDIT_GAMMA,		false,	true, 0.1f,	true, 5,	0.1f },
		{ IDC_EDIT_THRESHOLD,		false,	true, 0,	true, 1,	0.01f },
		{ IDC_EDIT_HIGHLIGHT_NOISE,	false,	true, 0,	true, 1,	0.01f },
		{ IDC_EDIT_LIGHT_NOISE,		false,	true, 0,	true, 1,	0.01f },
		{ IDC_EDIT_MIDTONE_NOISE,	false,	true, 0,	true, 1,	0.01f },
		{ IDC_EDIT_SHADOW_NOISE,	false,	true, 0,	true, 1,	0.01f }
	};
	const int iNumControls = sizeof(Controls) / sizeof(*Controls);

	void GetControlValueFromDialog(const EditControlData *pControl, HWND hDlg, int &iVal, float &fVal)
	{
		if(pControl->bIntegerControl)
			iVal = GetDlgItemInt(hDlg, pControl->iIDC, NULL, false);
		else
			fVal = GetDlgItemFloat(hDlg, pControl->iIDC);
	}

	void SaveControlValueToDialog(const EditControlData *pControl, HWND hDlg, int iVal, float fVal)
	{
		if(pControl->bIntegerControl)
			SetDlgItemInt(hDlg, pControl->iIDC, iVal, false);
		else
			SetDlgItemFloat(hDlg, pControl->iIDC, "%.3g", fVal);
	}
	
	void GetControlValue(const EditControlData *pControl, Algorithm::Settings &settings, int &iVal, float &fVal)
	{
		if(pControl->bIntegerControl)
			iVal = *GetIntControl(settings, pControl->iIDC);
		else
			fVal = *GetFloatControl(settings, pControl->iIDC);
	}

	bool SaveControlValue(const EditControlData *pControl, Algorithm::Settings &settings, int iVal, float fVal)
	{
		if(pControl->bIntegerControl)
		{
			int *pVal = GetIntControl(settings, pControl->iIDC);
			if(*pVal == iVal)
				return false;
			*pVal = iVal;
			return true;
		}
		else
		{
			float *pVal = GetFloatControl(settings, pControl->iIDC);
			if(*pVal == fVal)
				return false;
			*pVal = fVal;
			return true;
		}
	}

	const EditControlData *GetControlIndexFromID(int iIDC)
	{
		int iControl = -1;
		for(int i = 0; i < iNumControls; ++i)
		{
			if(Controls[i].iIDC == iIDC)
				return &Controls[i];
		}
		assert(false);
		return NULL;
	}

	void ClampControlValue(const EditControlData *pControl, int &iVal, float &fVal)
	{
		if(pControl->bClampBottom)
		{
			if(pControl->bIntegerControl)
				iVal = max(iVal, (int) pControl->fBottom);
			else
				fVal = max(fVal, pControl->fBottom);
		}

		if(pControl->bClampTop)
		{
			if(pControl->bIntegerControl)
				iVal = min(iVal, (int) pControl->fTop);
			else
				fVal = min(fVal, pControl->fTop);
		}
	}

	WNDPROC hOldProxyWndProc;
	LRESULT ProxyWindowProc(HWND hWnd, UINT wMsg, WPARAM wParam, LPARAM lParam)
	{
		UIData *pData = (UIData *) GetWindowLongPtr(hWnd, GWLP_USERDATA);

		switch(wMsg)
		{
		case WM_PAINT:
			pData->PaintProxy(hWnd);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		}
		return CallWindowProc(hOldProxyWndProc, hWnd, wMsg, wParam, lParam);
	}

	BOOL WINAPI UIProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam)
	{
		UIData *pData = (UIData *) GetWindowLongPtr(hDlg, GWLP_USERDATA);

		/* There aren't any messages that consistently tell us when ALT is pressed and released. */
		if(pData != NULL)
		{
			bool bOn = !!(GetKeyState(VK_LMENU) & 0x8000);

			if(bOn && !pData->bAltIsHeld)
			{
				pData->bAltIsHeld = true;
				SetDlgItemText(hDlg, IDOK, "&Save");
				SetDlgItemText(hDlg, IDCANCEL, "&Reset");
			}
			else if(!bOn && pData->bAltIsHeld)
			{
				pData->bAltIsHeld = false;
				SetDlgItemText(hDlg, IDOK, "OK");
				SetDlgItemText(hDlg, IDCANCEL, "Cancel");
			}
		}

		int item, cmd;
		switch(wMsg)
		{
		case WM_TIMER:
		case WM_NCHITTEST:
		case WM_SETCURSOR:
		case WM_MOUSEFIRST:
			break;
		default:
			//printf("msg %x, %i %i\n", wMsg, wParam, lParam);
			break;
		}

		switch(wMsg)
		{
		case WM_TIMER:
			if(wParam == 1)
			{
				pData->UpdateProgressMeter(hDlg);

				if(!pData->m_pFilter->m_sError.empty())
				{
					string sError(pData->m_pFilter->m_sError);
					pData->m_pFilter->m_sError.clear();
					MessageBox(hDlg, sError.c_str(), plugInName, MB_OK);
				}
				return TRUE;
			}
			return FALSE;

		case WM_DESTROY:
			DestroyCursor(pData->g_hCursorHand);
			return TRUE;
		case WM_INITDIALOG:
			pData = (UIData *) lParam;
			SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);

			{
				HWND hItem = GetDlgItem(hDlg, ID_PROXY_ITEM);
				SetWindowLongPtr(hItem, GWLP_USERDATA, lParam);
				hOldProxyWndProc = (WNDPROC) GetWindowLongPtr(hItem, GWLP_WNDPROC);
				SetWindowLongPtr(hItem, GWLP_WNDPROC, (LONG_PTR) ProxyWindowProc);
			}

			SetTimer(hDlg, 1, 50, NULL);
			pData->g_hCursorHand = LoadCursor(NULL, IDC_HAND); 
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
			ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS), SW_HIDE);

			pData->m_pFilter->CurrentSettings = pData->m_pFilter->FilterSettings;
			pData->m_pFilter->CurrentOptions = pData->m_pFilter->FilterOptions;
			pData->m_pFilter->LastSettings = pData->m_pFilter->FilterSettings;

			SetFocus(GetDlgItem(hDlg, IDC_EDIT_RADIUS));

			pData->SetPreviewPosition(hDlg, 0, 0);

			{
				HMENU hSys = GetSystemMenu(hDlg, FALSE);
				AppendMenu(hSys, MF_SEPARATOR, 0, NULL);
				AppendMenu(hSys, MF_STRING|MF_ENABLED, 0, "&About");
			}

			{
				HWND hCombo = GetDlgItem(hDlg, IDC_DISPLAY_MODE); 
				const char *szString[] =
				{
					"Normal",
					"Inside",
					"Side by side",
					NULL
				};
				for(int i=0; szString[i]; ++i)
				{
					LRESULT dwIndex = SendMessage(hCombo, CB_ADDSTRING, i, (LPARAM) szString[i]);
					SendMessage(hCombo, CB_SETITEMDATA, dwIndex, dwIndex - 1); 
				}
 			}
			{
				HWND hCombo = GetDlgItem(hDlg, IDC_THREADS);
				const char *szString[] =
				{
					"Auto-1",
					"Auto",
					NULL
				};
				
				SYSTEM_INFO si;
				GetSystemInfo(&si);
				int iNumThreads = si.dwNumberOfProcessors;
				int iCount = 0;

				for(int i=0; szString[i]; ++i)
				{
					LRESULT dwIndex = SendMessage(hCombo, CB_ADDSTRING, iCount, (LPARAM) szString[i]);
					SendMessage(hCombo, CB_SETITEMDATA, dwIndex, dwIndex - 1); 
					++iCount;
				}

				for(int i=0; i < iNumThreads; ++i)
				{
					char buf[1024];
					sprintf(buf, "%i", i + 1);
					LRESULT dwIndex = SendMessage(hCombo, CB_ADDSTRING, iCount, (LPARAM) buf);
					SendMessage(hCombo, CB_SETITEMDATA, dwIndex, dwIndex - 1); 
					++iCount;
				}
			}

			pData->SetDlgItems(hDlg, pData->m_pFilter->CurrentSettings, pData->m_pFilter->CurrentOptions);
			pData->UpdateProxyItem(hDlg);
			return TRUE;

		case WM_COMMAND:
		{
			item = LOWORD (wParam);
			cmd = HIWORD (wParam);
			Algorithm::Settings &settings = pData->bRightButtonHeld? pData->m_pFilter->LastSettings: pData->m_pFilter->CurrentSettings;
			Algorithm::Options &options = pData->m_pFilter->CurrentOptions;

			if(cmd == EN_SETFOCUS)
			{
				pData->g_iFocusedEditControl = LOWORD (wParam);
				return TRUE;
			}
			else if(cmd == EN_KILLFOCUS)
			{
				pData->g_iFocusedEditControl = -1;
				return TRUE;
			}

			if(pData->bRightButtonHeld)
				return TRUE;

			if(cmd == EN_CHANGE)
			{
				const EditControlData *pControl = GetControlIndexFromID(item);

				int iVal;
				float fVal;
				GetControlValueFromDialog(pControl, hDlg, iVal, fVal);
				ClampControlValue(pControl, iVal, fVal);
				SaveControlValue(pControl, settings, iVal, fVal);
			}
			else if(cmd == CBN_SELCHANGE)
			{
				switch (item)
				{
				case IDC_DISPLAY_MODE:	options.m_DisplayMode = (Algorithm::Options::DisplayMode) SendMessage(GetDlgItem(hDlg, item), CB_GETCURSEL, 0, 0); pData->ApplyOptions(hDlg); break;
				}

				/* Work around an ugly Win32 bug: CBN_EDITCHANGE should be received after CBN_SELCHANGE, since
				 * changing the selection is "an action that may have altered the text in the edit control portion
				 * of a combo box"), but it isn't. */
				PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(item, CBN_EDITCHANGE), lParam);
			}
			else if(cmd == CBN_EDITCHANGE)
			{
				switch (item)
				{
				case IDC_THREADS: options.nb_threads = pData->UpdateThreadInput(hDlg); break;
				}
			}
			else if(cmd == BN_CLICKED)
			{
				switch(item)
				{
				case IDOK:
				case IDCANCEL:
				case ID_SAVE_ONLY:
					if(item == IDCANCEL && pData->bAltIsHeld)
					{
						/* Just restore defaults. */
						pData->bAltKeypressWasUsed = true;
						pData->m_pFilter->CurrentSettings = Algorithm::Settings();
						pData->SetDlgItems(hDlg, pData->m_pFilter->CurrentSettings, pData->m_pFilter->CurrentOptions);
						break;
					}

					pData->m_pFilter->LastSettings = pData->m_pFilter->FilterSettings;
					pData->m_pFilter->FilterSettings = settings;
					pData->m_pFilter->FilterOptions = options;
					if(item == IDOK)
					{
						if(pData->bAltIsHeld)
						{
							pData->bAltKeypressWasUsed = true;
							pData->iUIResult = UI_SAVE_ONLY;
						}
						else
							pData->iUIResult = UI_OK;
					}
					else if(item == ID_SAVE_ONLY)
						pData->iUIResult = UI_SAVE_ONLY; /* sent from the modal loop */
					else
						pData->iUIResult = UI_CANCEL;
					printf("UI closing (%i)\n", item);
					EndDialog(hDlg, item);

					break;
				case IDC_COMPARE:
					pData->m_pFilter->LastSettings = pData->m_pFilter->FilterSettings;
					break;
				case IDC_COPY:
					SetClipboardFromString(hDlg, settings.GetAsString());
					break;
				}
			}

			pData->UpdateDisplayAfterSettingsChange(hDlg);

			return TRUE;
		}
		case WM_KEYDOWN:
		{
/*			if(wParam == VK_UP)
			{
				pData->m_pFilter->Image.iY += 100;
				return TRUE;
			}
			if(wParam == VK_DOWN)
			{
				pData->m_pFilter->Image.iY -= 100;
				return TRUE;
			}
*/
			return FALSE;
		}

		case WM_SYSCOMMAND:
			if(wParam == SC_KEYMENU)
			{
				/* The user pressed ALT, and then either released it or pressed an alt-key.
				 * If something was done as an ALT modifier, like alt-mousewheel to zoom the
				 * preview, and then no alt-key is pressed, eat the event so we don't trigger
				 * the menu.  This is a bit brittle: if we don't get this event when ALT is
				 * released for some reason, we'll misbehave. */
				if(pData->bAltKeypressWasUsed)
				{
					pData->bAltKeypressWasUsed = false;

					/* Only eat ALT releases, not keys pressed with ALT held. */
					if(lParam == 0)
						return TRUE;
				}
			}

			if(wParam == 0)
			{
				ShowAboutDialog(hDlg);
				return TRUE;
			}

			/* Treat alt-enter as simply enter, except with the ALT ("save") modifier applied. */
			if(wParam == SC_KEYMENU && lParam == 13)
			{
				PostMessage(hDlg, WM_COMMAND, MAKEWORD(ID_SAVE_ONLY, BN_CLICKED), 0);
				return TRUE;
			}
			return FALSE;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONUP:
		{
			if(pData->bRightButtonHeld)
			{
				if(wMsg == WM_LBUTTONDOWN || wMsg == WM_LBUTTONDBLCLK)
				{
					ReleaseCapture();
					SetCursor(pData->g_hOldCursor);
					pData->bRightButtonHeld = false;
					swap(pData->m_pFilter->CurrentSettings, pData->m_pFilter->LastSettings);
					pData->RedrawProxyItem(hDlg);
					return TRUE;
				}
			}

			if(wMsg == WM_LBUTTONDOWN || wMsg == WM_LBUTTONDBLCLK)
			{
				if(CoordInDlgItem(hDlg, ID_PROXY_ITEM, lParam, false))
				{
					SetCapture(hDlg);
					pData->g_hOldCursor = SetCursor(pData->g_hCursorHand);
					pData->bDraggingPreview = true;
					pData->bDraggingSnapped = false;
					pData->iDragAnchorX = LOWORD(lParam);
					pData->iDragAnchorY = HIWORD(lParam);
					pData->iDragOrigX = pData->iPreviewX;
					pData->iDragOrigY = pData->iPreviewY;

					pData->RedrawProxyItem(hDlg);
					return TRUE;
				}
			}

			if(wMsg == WM_LBUTTONUP && pData->bDraggingPreview)
			{
				ReleaseCapture();
				SetCursor(pData->g_hOldCursor);
				pData->bDraggingPreview = false;
				/* If dragging didn't snap, we didn't move the cursor, so don't spend time recalculating
				 * the preview; just redraw it. */
				if(pData->bDraggingSnapped)
					pData->UpdateProxyItem(hDlg);
				else
					pData->RedrawProxyItem(hDlg);
				pData->bDraggingSnapped = false;
			}

			return FALSE;
		}

		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONUP:
		{
			/* Right clicking shows the last preview, if any. */
			if(wMsg == WM_RBUTTONDOWN || wMsg == WM_RBUTTONDBLCLK)
			{
				if(CoordInDlgItem(hDlg, ID_PROXY_ITEM, lParam, false))
				{
					SetCapture(hDlg);
					pData->g_hOldCursor = SetCursor(pData->g_hCursorHand);
					pData->bRightButtonHeld = true;
					pData->m_pFilter->FilterSettings = pData->m_pFilter->LastSettings;
					pData->SetDlgItems(hDlg, pData->m_pFilter->FilterSettings, pData->m_pFilter->FilterOptions);
					pData->UpdateProxyItem(hDlg);
					return TRUE;
				}
			}

			if(wMsg == WM_RBUTTONUP && pData->bRightButtonHeld)
			{
				ReleaseCapture();
				SetCursor(pData->g_hOldCursor);
				pData->bRightButtonHeld = false;
				pData->m_pFilter->FilterSettings = pData->m_pFilter->CurrentSettings;
				pData->SetDlgItems(hDlg, pData->m_pFilter->FilterSettings, pData->m_pFilter->FilterOptions);
				pData->UpdateProxyItem(hDlg);
			}
			return FALSE;
		}

		case WM_MOUSEMOVE:
		{
			if(pData->bDraggingPreview)
			{
				short iX = LOWORD(lParam);
				short iY = HIWORD(lParam);
				int iDistanceX = (pData->iDragAnchorX - iX);
				int iDistanceY = (pData->iDragAnchorY - iY);
				if(abs(iDistanceX) > 10 || abs(iDistanceY) > 10)
					pData->bDraggingSnapped = true;

				if(pData->bDraggingSnapped)
				{
					int iX = pData->iDragOrigX + int(iDistanceX / pData->GetPreviewZoom());
					int iY = pData->iDragOrigY + int(iDistanceY / pData->GetPreviewZoom());
					pData->SetPreviewPosition(hDlg, iX, iY);

					pData->RedrawProxyItem(hDlg);
				}
				return TRUE;
			}
			return FALSE;
		}
		case WM_MOUSEWHEEL:
		{
			short iDelta = HIWORD(wParam);
			if(pData->bAltIsHeld)
			{
				pData->bAltKeypressWasUsed = true;
				int iX, iY;
				if(CoordInDlgItem(hDlg, ID_PROXY_ITEM, lParam, true, &iX, &iY))
				{
					/* Adjust iX/iY for the border. */
					--iX;
					--iY;

					/* iX, iY is the cursor position within the preview.  Zoom in, anchoring the position
					 * on the cursor. */
					int iImageX = pData->iPreviewX + int(iX / pData->GetPreviewZoom());
					int iImageY = pData->iPreviewY + int(iY / pData->GetPreviewZoom());

					if(!pData->ChangeZoom(iDelta > 0))
						return TRUE; // no change

					iX = iImageX - int(iX / pData->GetPreviewZoom());
					iY = iImageY - int(iY / pData->GetPreviewZoom());
					pData->SetPreviewPosition(hDlg, iX, iY);
					
					pData->UpdateProxyItem(hDlg);

					return TRUE;
				}
			}

			Algorithm::Settings &settings = pData->bRightButtonHeld? pData->m_pFilter->LastSettings: pData->m_pFilter->CurrentSettings;
			Algorithm::Options &options = pData->m_pFilter->CurrentOptions;

			if(pData->g_iFocusedEditControl != -1)
			{
				const EditControlData *pControl = GetControlIndexFromID(pData->g_iFocusedEditControl);

				int iVal;
				float fVal;
				GetControlValue(pControl, settings, iVal, fVal);

				float fChange = pControl->fMouseWheelDelta;
				if(iDelta < 0)
					fChange *= -1.0f;
				if((GetKeyState(VK_LSHIFT) & 0x8000) || (GetKeyState(VK_RSHIFT) & 0x8000))
					fChange *= 10.0f;
				fVal += fChange;
				iVal += int(fChange);

				ClampControlValue(pControl, iVal, fVal);
				if(SaveControlValue(pControl, settings, iVal, fVal))
				{
					SaveControlValueToDialog(pControl, hDlg, iVal, fVal);
					pData->UpdateDisplayAfterSettingsChange(hDlg);
				}
			}

			return FALSE;
		}

		default:
			return FALSE;
		}
	}

	BOOL WINAPI UIProcExceptions(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam)
	{
		UIData *pData = (UIData *) GetWindowLongPtr(hDlg, GWLP_USERDATA);

		try
		{
			BOOL ret = UIProc(hDlg, wMsg, wParam, lParam);
			return ret;
		} catch(const exception &e) {
			// XXX: pData == NULL if too early
			string sError = e.what();
			strncpy(pData->szExceptionMessage, sError.c_str(), sizeof(pData->szExceptionMessage));
			pData->szExceptionMessage[sizeof(pData->szExceptionMessage)-1] = 0;
			EndDialog(hDlg, 0);
			pData->iUIResult = UI_CANCEL;
		}
		return FALSE;
	}
}

UIResult DoUI(PreviewRenderer *pFilter)
{
	UIData data;
	data.m_pFilter = pFilter;

	PlatformData *pPlatform = (PlatformData *)(gFilterRecord->platformData);

	AddWindowThreadLocks();
	DialogBoxParam(g_hInstance, (LPSTR) "UNSHARP2_DIALOG", (HWND) pPlatform->hwnd, (DLGPROC) UIProcExceptions, (LPARAM) &data);

	if(data.szExceptionMessage[0])
		MessageBox((HWND) pPlatform->hwnd, data.szExceptionMessage, plugInName, MB_OK);
	FreeWindowThreadLocks();

	return data.iUIResult;
}

/* Photoshop's progress dialog is ugly.  It has an arbitrary delay before displaying, and
 * we have no way of telling it that we're very likely to take a long time, so there's always
 * an unpleasant delay before displaying the progress dialog.  It makes us look unresponsive,
 * even though it's Photoshop's fault, so don't use it. */
namespace
{
	Algorithm *g_pAlgo;
	BOOL WINAPI ProgressWinProc(HWND hDlg, UINT wMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (wMsg)
		{
		case WM_INITDIALOG:
		{
			SetTimer(hDlg, 1, 50, NULL);
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 1000));

			HWND hwndOwner = GetParent(hDlg);
			if(hwndOwner == NULL)
				hwndOwner = GetDesktopWindow(); 

			RECT owner;
			GetWindowRect(hwndOwner, &owner); 
			RECT r;
			GetWindowRect(hDlg, &r);

/*			int iWidth = r.right - r.left;
			int iHeight = r.bottom - r.top;
			int x = GetSystemMetrics(SM_CXSCREEN)/2-iWidth/2;
			int y = GetSystemMetrics(SM_CYSCREEN)/2-iHeight/2;
			SetWindowPos(hDlg, 0, 
				(owner.left + owner.right) / 2 - (r.right - r.left) / 2,
				(owner.bottom + owner.top) / 2 - (r.bottom - r.top) / 2,
				0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW );
*/			return TRUE;
		}

		case WM_COMMAND:
		{
			int item = LOWORD (wParam);
			int cmd = HIWORD (wParam);
			if(cmd == BN_CLICKED)
			{
				if(item == IDCANCEL)
					EndDialog(hDlg, 0);
			}
			return TRUE;
		}

		case WM_TIMER:
		{
			if(!g_pAlgo->Running())
			{
				EndDialog(hDlg, 1);
				return TRUE;
			}

			float fPercentDone = g_pAlgo->Progress();
			int i = int(fPercentDone * 1000);
			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETPOS, i, 0);
			return TRUE;
		}
		default:

			return FALSE;
		}
	}
}

bool ShowProgressDialog(Algorithm *pAlgo)
{
	g_pAlgo = pAlgo;
	PlatformData *pPlatform = (PlatformData *)(gFilterRecord->platformData);

	AddWindowThreadLocks();
	INT_PTR iRet = DialogBoxParam(g_hInstance, (LPSTR) "PROGRESS", (HWND) pPlatform->hwnd, (DLGPROC) ProgressWinProc, NULL);
	FreeWindowThreadLocks();
	g_pAlgo = NULL;
	return iRet != 0;
}

void DoAbout()
{
	AboutRecord *pAboutRecord = (AboutRecord*) gFilterRecord;
	PlatformData *pPlatform = (PlatformData *) pAboutRecord->platformData;
	HWND hWnd = (HWND) pPlatform->hwnd;

	ShowAboutDialog(hWnd);
}
