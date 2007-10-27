#include "MenuDlg.h"
#include <cassert>

BOOL CALLBACK MenuDlgProc(HWND hDlg, UINT nMsg
							, WPARAM wParam, LPARAM lParam)
{
	SMenuDlgParam* pParam=(SMenuDlgParam*)::GetWindowLong(hDlg, GWL_USERDATA);
	switch (nMsg)
	{
	case WM_INITDIALOG:
		SetWindowLong(hDlg, GWL_USERDATA, lParam);
		return TRUE;

	case WM_DRAWITEM:
	case WM_INITMENUPOPUP:
	case WM_MEASUREITEM:
		if (pParam->pMenu3)
		{
			if (NOERROR!=pParam->pMenu3->HandleMenuMsg(nMsg, wParam, lParam))
			{
				//assert(0);
			}
		}
		else if (pParam->pMenu2)
		{
			if (NOERROR!=pParam->pMenu2->HandleMenuMsg(nMsg, wParam, lParam))
			{
				//assert(0);
			}
		}
		return (nMsg == WM_INITMENUPOPUP ? FALSE : TRUE);
	case WM_MENUCHAR:
		if (pParam->pMenu3)
		{
			LRESULT res;
			if (NOERROR!=pParam->pMenu3->HandleMenuMsg2(nMsg, wParam, lParam, &res))
			{
				//assert(0);
			}
			if (res) return res;
		}
		break;
	}
	return FALSE;
}