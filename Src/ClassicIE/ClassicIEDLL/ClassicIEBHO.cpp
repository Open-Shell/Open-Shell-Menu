// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "ClassicIEDLL_h.h"
#include "ClassicIEBHO.h"
#include "ClassicIEDLL.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include "FNVHash.h"
#include "dllmain.h"
#include <shlguid.h>

static bool IsLowIntegrity( void )
{
	bool bLow=false;
	HANDLE hToken;
	if (OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_QUERY_SOURCE,&hToken)) 
	{
		DWORD dwLengthNeeded;
		if (!GetTokenInformation(hToken,TokenIntegrityLevel,NULL,0,&dwLengthNeeded))
		{
			TOKEN_MANDATORY_LABEL *pTIL=(TOKEN_MANDATORY_LABEL*)malloc(dwLengthNeeded);
			if (pTIL)
			{
				if (GetTokenInformation(hToken,TokenIntegrityLevel,pTIL,dwLengthNeeded,&dwLengthNeeded))
				{
					DWORD dwIntegrityLevel=*GetSidSubAuthority(pTIL->Label.Sid,(DWORD)(UCHAR)(*GetSidSubAuthorityCount(pTIL->Label.Sid)-1));
					bLow=(dwIntegrityLevel<SECURITY_MANDATORY_MEDIUM_RID);
				}
				free(pTIL);
			}
		}
		CloseHandle(hToken);
	}
	return bLow;
}

static DWORD StartBroker( bool bWait, const wchar_t *param )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	PathRemoveFileSpec(path);
#ifndef _WIN64
	BOOL bWow64;
	if (!IsWow64Process(GetCurrentProcess(),&bWow64) || !bWow64 || (GetVersionEx(GetModuleHandle(NULL))>>24)<10)
		PathAppend(path,L"ClassicIE_32.exe");
	else
#endif
	PathAppend(path,L"ClassicIE_64.exe");

	wchar_t cmdLine[1024];
	Sprintf(cmdLine,_countof(cmdLine),L"\"%s\" %s",path,param);
	STARTUPINFO startupInfo={sizeof(startupInfo)};
	PROCESS_INFORMATION processInfo;
	memset(&processInfo,0,sizeof(processInfo));
	DWORD res=GetIESettings();
	if (CreateProcess(path,cmdLine,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
	{
		CloseHandle(processInfo.hThread);
		if (bWait)
		{
			if (WaitForSingleObject(processInfo.hProcess,2000)==WAIT_OBJECT_0)
				GetExitCodeProcess(processInfo.hProcess,&res);
		}
		CloseHandle(processInfo.hProcess);
	}
	return res;
}

HRESULT STDMETHODCALLTYPE CClassicIEBHO::SetSite( IUnknown *pUnkSite )
{
	if (m_pWebBrowser && m_dwEventCookie!=0xFEFEFEFE)
		DispEventUnadvise(m_pWebBrowser,&DIID_DWebBrowserEvents2);
	m_pWebBrowser=NULL;

	IObjectWithSiteImpl<CClassicIEBHO>::SetSite(pUnkSite);
	if (pUnkSite)
	{
		HMODULE hFrame=GetModuleHandle(L"ieframe.dll");
		bool bLowIntegrity=IsLowIntegrity();
		m_ProtectedMode.LoadString(hFrame,bLowIntegrity?12939:12940);
		m_ProtectedMode=L" | "+m_ProtectedMode;
		// find the top window and run another process to subclass it (the top window can be in a higher-level process, so we can't subclass from here)
		LogToFile(CIE_LOG,L"SetSite");
		CComQIPtr<IServiceProvider> pProvider=pUnkSite;

		m_Settings=0;

		if (pProvider)
		{

			pProvider->QueryService(SID_SShellBrowser,IID_IShellBrowser,(void**)&m_pBrowser);

			HWND hwnd;
			HWND topWindow=NULL;
			if (m_pBrowser && SUCCEEDED(m_pBrowser->GetWindow(&hwnd)))
			{
				HWND topWindow=GetAncestor(hwnd,GA_ROOT);
				if (topWindow)
				{
					wchar_t param[100];
					Sprintf(param,_countof(param),L"%u",(DWORD)(uintptr_t)topWindow);
					m_Settings=StartBroker(bLowIntegrity,param);

					if (m_Settings&(IE_SETTING_PROGRESS|IE_SETTING_ZONE))
					{
						m_pZoneManager.CoCreateInstance(CLSID_InternetZoneManager,NULL,CLSCTX_INPROC_SERVER);
						m_pSecurityManager.CoCreateInstance(CLSID_InternetSecurityManager,NULL,CLSCTX_INPROC_SERVER);

						pProvider->QueryService(SID_SWebBrowserApp,IID_IWebBrowser2,(void**)&m_pWebBrowser);
						if (m_pWebBrowser)
						{
							if (m_dwEventCookie==0xFEFEFEFE) // ATL's event cookie is 0xFEFEFEFE when the sink is not advised
								DispEventAdvise(m_pWebBrowser,&DIID_DWebBrowserEvents2);
						}
					}
				}
			}
		}
	}
	else
	{
		m_pBrowser=NULL;
		m_pWebBrowser=NULL;
		m_pZoneManager=NULL;
		m_pSecurityManager=NULL;
	}
	return S_OK;
}

HRESULT WINAPI CClassicIEBHO::UpdateRegistry( BOOL bRegister )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	PathRemoveFileSpec(path);

	CString menu;
	menu.LoadString(IDS_SETTINGS_TITLE);

	_ATL_REGMAP_ENTRY mapEntries[]={
		{L"MODULEPATH",path},
		{L"MENUTEXT",menu},
		{NULL,NULL}
	};

	return _AtlModule.UpdateRegistryFromResource(IDR_CLASSICIEBHO,bRegister,mapEntries);
}

LRESULT CALLBACK CClassicIEBHO::SubclassStatusProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==SB_SETPARTS && wParam>0)
	{
		CClassicIEBHO *pThis=(CClassicIEBHO*)uIdSubclass;
		int w0=*(int*)lParam; // total width
		int w1=pThis->m_Progress<0?0:PROGRESS_WIDTH; // progress part
		int w2=pThis->m_TextWidth; // zone part
		int w=w1+w2;
		int parts[PART_COUNT];
		parts[PART_TEXT]=w0;
		if (parts[PART_TEXT]>=w+MIN_TEXT_WIDTH)
			parts[PART_TEXT]-=w;
		else if (parts[PART_TEXT]>=MIN_TEXT_WIDTH)
			parts[PART_TEXT]=MIN_TEXT_WIDTH;
		if (parts[PART_TEXT]>w0)
			parts[PART_TEXT]=w0;

		if (parts[PART_TEXT]+w1>w0)
			w1=0;
		parts[PART_PROGRESS]=parts[PART_TEXT]+w1;
		parts[PART_ZONE]=w0;
		parts[PART_ZOOM]=-1;

		DefSubclassProc(hWnd,SB_SETPARTS,_countof(parts),(LPARAM)parts);

		TOOLINFO tool={sizeof(tool),TTF_SUBCLASS,hWnd};
		tool.uId=1;
		DefSubclassProc(hWnd,SB_GETRECT,PART_ZONE,(LPARAM)&tool.rect);
		SendMessage(pThis->m_Tooltip,TTM_NEWTOOLRECT,0,(LPARAM)&tool);

		if (w1==0)
			ShowWindow(pThis->m_ProgressBar,SW_HIDE);
		else
		{
			RECT rc;
			DefSubclassProc(hWnd,SB_GETRECT,PART_PROGRESS,(LPARAM)&rc);
			rc.left+=2;
			rc.right-=2;
			rc.top+=1;
			rc.bottom-=1;
			SetWindowPos(pThis->m_ProgressBar,NULL,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,SWP_NOZORDER|SWP_SHOWWINDOW);
		}
		return TRUE;
	}

	if (uMsg==SB_GETPARTS)
	{
		int parts[10];
		int n=(int)DefSubclassProc(hWnd,SB_GETPARTS,_countof(parts),(LPARAM)parts);
		const int *p=parts;
		if (n>2)
		{
			p+=n-2;
			n=2;
		}
		if (lParam)
			memcpy((int*)lParam,p,4*((n<(int)wParam)?n:wParam));
		return n;
	}

	if (uMsg==SB_GETRECT)
	{
		if (wParam==1) wParam=PART_ZOOM;
		else if (wParam>PART_OFFSET) wParam-=PART_OFFSET;
	}

	if (uMsg==SB_SETTEXT)
	{
		if (!SendMessage(hWnd,SB_ISSIMPLE,0,0))
		{
			if ((wParam&255)==1) wParam=PART_ZOOM;
			else if ((wParam&255)>PART_OFFSET) wParam-=PART_OFFSET;
		}
	}

	if (uMsg==WM_LBUTTONDBLCLK)
	{
		POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
		RECT rc;
		DefSubclassProc(hWnd,SB_GETRECT,PART_ZONE,(LPARAM)&rc);
		if (PtInRect(&rc,pt))
		{
			CClassicIEBHO *pThis=(CClassicIEBHO*)uIdSubclass;
			CComBSTR url;
			if (pThis->m_pWebBrowser && SUCCEEDED(pThis->m_pWebBrowser->get_LocationURL(&url)))
			{
				wchar_t buf[1024];
				Sprintf(buf,_countof(buf),L"zone %u %s",(unsigned)(uintptr_t)GetAncestor(hWnd,GA_ROOT),(const wchar_t*)url);
				StartBroker(false,buf);
			}

			return 0;
		}
	}

	if (uMsg==SB_SIMPLE)
	{
		CClassicIEBHO *pThis=(CClassicIEBHO*)uIdSubclass;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		if (wParam)
		{
			ShowWindow(pThis->m_ProgressBar,SW_HIDE);
		}
		else
		{
			pThis->ResetParts();
		}
		return res;
	}

	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

void CClassicIEBHO::ResetParts( void )
{
	int parts[256];
	int n=(int)SendMessage(m_StatusBar,SB_GETPARTS,_countof(parts),(LPARAM)parts);
	SendMessage(m_StatusBar,SB_SETPARTS,n,(LPARAM)parts);
}

STDMETHODIMP CClassicIEBHO::OnNavigateComplete( IDispatch *pDisp, VARIANT *URL )
{
	HWND status;
	if (!m_pBrowser || FAILED(m_pBrowser->GetControlWindow(FCW_STATUS,&status)))
		return S_OK;

	if (m_StatusBar!=status)
	{
		if (m_StatusBar) RemoveWindowSubclass(m_StatusBar,SubclassStatusProc,(UINT_PTR)this);
		m_StatusBar=status;
		if (m_StatusBar)
		{
			m_TextWidth=0;
			SetWindowSubclass(m_StatusBar,SubclassStatusProc,(UINT_PTR)this,0);
			ResetParts();
		}
	}

	if (!m_StatusBar) return S_OK;

	if (!m_Tooltip)
	{
		m_Tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,m_StatusBar,NULL,g_Instance,NULL);
		TOOLINFO tool={sizeof(tool),TTF_SUBCLASS,m_StatusBar};
		tool.uId=1;
		tool.hinst=GetModuleHandle(L"ieframe.dll");
		tool.lpszText=MAKEINTRESOURCE(12941);
		SendMessage(m_Tooltip,TTM_ADDTOOL,0,(LPARAM)&tool);
	}

	if (!m_ProgressBar)
	{
		m_ProgressBar=CreateWindowEx(0,PROGRESS_CLASS,NULL,WS_CHILD|PBS_SMOOTH,0,0,0,0,m_StatusBar,NULL,g_Instance,NULL);
		SendMessage(m_ProgressBar,PBM_SETRANGE,0,MAKELPARAM(0,100));
	}

	m_TextWidth=0;
	if (!(m_Settings&IE_SETTING_ZONE))
		return S_OK;
	wchar_t text[256];
	text[0]=0;
	HICON hIcon=NULL;
	if (m_pZoneManager && m_pSecurityManager && URL && URL->vt==VT_BSTR)
	{
		DWORD zone;
		ZONEATTRIBUTES attributes={sizeof(attributes)};
		if (SUCCEEDED(m_pSecurityManager->MapUrlToZone(URL->bstrVal,&zone,0)) && SUCCEEDED(m_pZoneManager->GetZoneAttributes(zone,&attributes)))
		{
			Strcpy(text,_countof(text),attributes.szDisplayName);
			if (m_Settings&IE_SETTING_PROTECTED)
				Strcat(text,_countof(text),m_ProtectedMode);
			unsigned int key=CalcFNVHash(attributes.szIconPath);
			std::map<unsigned int,HICON>::const_iterator it=m_IconCache.find(key);
			if (it!=m_IconCache.end())
				hIcon=it->second;
			else
			{
				wchar_t path[_MAX_PATH];
				wchar_t *str=wcschr(attributes.szIconPath,'#');
				if (!str) str=wcschr(attributes.szIconPath,',');
				int index=0;
				if (str)
				{
					index=_wtol(str+1);
					*str=0;
				}
				Strcpy(path,_countof(path),attributes.szIconPath);
				if (PathIsRelative(path))
					PathFindOnPath(path,NULL);
				if (index==0)
					hIcon=(HICON)LoadImage(NULL,path,IMAGE_ICON,16,16,LR_LOADFROMFILE);
				else
				{
					HMODULE hModule=LoadLibraryEx(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
					if (hModule)
					{
						hIcon=(HICON)LoadImage(hModule,MAKEINTRESOURCE(index),IMAGE_ICON,16,16,0);
						FreeLibrary(hModule);
					}
				}
				m_IconCache[key]=hIcon;
			}
		}
	}
	if (text[0])
	{
		HDC hdc=GetDC(m_StatusBar);
		HGDIOBJ font0=SelectObject(hdc,(HFONT)SendMessage(m_StatusBar,WM_GETFONT,0,0));
		SIZE size;
		GetTextExtentPoint32(hdc,text,Strlen(text),&size);
		m_TextWidth=size.cx;
		SelectObject(hdc,font0);
		ReleaseDC(m_StatusBar,hdc);
	}

	// reset the parts to apply the new text width
	m_TextWidth+=32;
	ResetParts();

	// set text and icon
	SendMessage(m_StatusBar,SB_SETTEXT,PART_ZONE+PART_OFFSET,(LPARAM)text);
	SendMessage(m_StatusBar,SB_SETICON,PART_ZONE,(LPARAM)hIcon);

	return S_OK;
}

STDMETHODIMP CClassicIEBHO::OnProgressChange( long progress, long progressMax )
{
	if (!(m_Settings&IE_SETTING_PROGRESS))
		return S_OK;
	bool bVisible=(IsWindowVisible(m_ProgressBar)!=0);
	if (progress<0 || progressMax==0)
	{
		m_Progress=-1;
		if (!bVisible) return S_OK;
	}
	else
	{
		m_Progress=progress*100/progressMax;
		SendMessage(m_ProgressBar,PBM_SETPOS,m_Progress,0);
		if (bVisible) return S_OK;
	}
	ResetParts();
	RedrawWindow(m_StatusBar,NULL,NULL,RDW_UPDATENOW|RDW_ALLCHILDREN);
	return S_OK;
}

STDMETHODIMP CClassicIEBHO::OnQuit( void )
{
	if (m_pWebBrowser && m_dwEventCookie!=0xFEFEFEFE) // ATL's event cookie is 0xFEFEFEFE, when the sink is not advised
		return DispEventUnadvise(m_pWebBrowser,&DIID_DWebBrowserEvents2);
	return S_OK;
}
