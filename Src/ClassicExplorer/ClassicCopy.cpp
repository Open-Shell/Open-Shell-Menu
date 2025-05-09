// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include <commctrl.h>
#include <oleacc.h>
#include <atlcomcli.h>
#include <dwmapi.h>
#include <utility>
#include "Translations.h"
#include "Settings.h"
#include "ResourceHelper.h"
#include "ComHelper.h"
#include "dllmain.h"

static wchar_t g_TitleMove[256];
static wchar_t g_TitleCopy[256];
static wchar_t g_TitleFolder[256];
static wchar_t g_ButtonMove[256];
static wchar_t g_ButtonDontMove[256];
static wchar_t g_ButtonCopy[256];
static wchar_t g_ButtonDontCopy[256];
static wchar_t g_ButtonCancel[256];
static wchar_t g_ButtonMore[256];
static HMODULE g_hShell32;

static void LogPrint( CString *pLog, const wchar_t *format, ... )
{
	wchar_t buf[256];
	va_list args;
	va_start(args,format);
	int len=Vsprintf(buf,_countof(buf),format,args);
	va_end(args);
	*pLog+=buf;
}

// CClassicCopyFile - this is the implementation of the Copy UI dialog box for files

class CClassicCopyFile
{
public:
	CClassicCopyFile( void );
	~CClassicCopyFile( void );

	bool Run( HWND hWnd, IAccessible *pAcc, CString *pLog );

private:
	void EnumAccChildren( IAccessible *pAcc, CString *pLog );
	void AddAccChild( IAccessible *pAcc, const VARIANT &id, CString *pLog );
	void GetFileInfo( IAccessible *pAcc, bool bSrc );

	CString m_FileName;
	bool m_bSystem;
	bool m_bReadOnly;
	HICON m_Icon;

	CString m_SrcSize;
	CString m_SrcTime;
	HICON m_SrcIcon;

	CString m_DstSize;
	CString m_DstTime;
	HICON m_DstIcon;

	typedef std::pair<CComPtr<IAccessible>,int> CControl;
	CControl m_YesButton;
	CControl m_NoButton;
	CControl m_CheckBox;
	CControl m_Cancel;
	bool m_bCopyMultiLast; // the last of a series of multi-file boxes

	static INT_PTR CALLBACK DialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam );

	static void PumpMessages( void );
};

CClassicCopyFile::CClassicCopyFile( void )
{
	m_Icon=m_SrcIcon=m_DstIcon=NULL;
	m_bCopyMultiLast=false;
	m_bSystem=m_bReadOnly=false;
}

CClassicCopyFile::~CClassicCopyFile( void )
{
	if (m_Icon) DestroyIcon(m_Icon);
	if (m_SrcIcon) DestroyIcon(m_SrcIcon);
	if (m_DstIcon) DestroyIcon(m_DstIcon);
}

// Show the dialog box. Returns true to suppress the original task dialog box
bool CClassicCopyFile::Run( HWND hWnd, IAccessible *pAcc, CString *pLog )
{
	// find all interesting controls
	EnumAccChildren(pAcc,pLog);

	if (!m_YesButton.first || m_YesButton.second!=CHILDID_SELF || !m_NoButton.first || m_NoButton.second!=CHILDID_SELF || !m_Cancel.first)
		return false; // something is wrong, do nothing

	if (pLog) pLog->Empty(); // success - no need to log anything

	// get the info for the source and the destination file (file name, icon, properties)
	m_bReadOnly=false;
	m_bSystem=false;
	GetFileInfo(m_YesButton.first,true);
	GetFileInfo(m_NoButton.first,false);

	TlsData *pTlsData=GetTlsData();
	if (m_CheckBox.first)
		pTlsData->bCopyMultiFile=true;
	else if (pTlsData->bCopyMultiFile)
		m_bCopyMultiLast=true;

	// pick the correct dialog template (for single and multiple files, for LTR and RTL)
	int dlg=pTlsData->bCopyMultiFile?(IsLanguageRTL()?IDD_FILEMULTIR:IDD_FILEMULTI):(IsLanguageRTL()?IDD_FILER:IDD_FILE);

	HWND parent=GetWindow(GetAncestor(hWnd,GA_ROOT),GW_OWNER);

	int res=(int)DialogBoxParam(g_Instance,MAKEINTRESOURCE(dlg),parent,DialogProc,(LPARAM)this);

	if (res==IDOK || (res==IDYES && m_bCopyMultiLast))
	{
		// Yes was pressed, proceed with the operation
		m_YesButton.first->accDoDefaultAction(CComVariant(CHILDID_SELF));
	}
	else if (res==IDNO)
	{
		// No
		if (m_CheckBox.first && GetKeyState(VK_SHIFT)<0)
			m_CheckBox.first->accDoDefaultAction(CComVariant(CHILDID_SELF)); // Shift+No = No to All
		PumpMessages(); // messages need to be pumped after every accessibility action. otherwise the next action doesn't work
		m_NoButton.first->accDoDefaultAction(CComVariant(CHILDID_SELF));
	}
	else if (res==IDYES)
	{
		// Yes to All
		m_CheckBox.first->accDoDefaultAction(CComVariant(CHILDID_SELF));
		PumpMessages(); // messages need to be pumped after every accessibility action. otherwise the next action doesn't work
		m_YesButton.first->accDoDefaultAction(CComVariant(CHILDID_SELF));
	}
	if (res==IDCANCEL)
	{
		// Cancel
		if (GetKeyState(VK_SHIFT)<0 || GetKeyState(VK_CONTROL)<0)
			return false; // // Shift+Cancel or Ctrl+Cancel was clicked - show the original dialog box
		m_Cancel.first->accDoDefaultAction(CComVariant(CHILDID_SELF));
	}
	if (res==IDC_LINKMORE)
	{
		// More... was clicked - show the original dialog box
		return false;
	}
	return true;
}

void CClassicCopyFile::PumpMessages( void )
{
	MSG msg;
	while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void CClassicCopyFile::AddAccChild( IAccessible *pAcc, const VARIANT &id, CString *pLog )
{
	CComVariant state;
	pAcc->get_accState(id,&state);
	if (state.intVal&(STATE_SYSTEM_UNAVAILABLE|STATE_SYSTEM_INVISIBLE)) return;
	CComVariant role;
	pAcc->get_accRole(id,&role);
	if (role.intVal==ROLE_SYSTEM_PUSHBUTTON)
	{
		CComBSTR name;
		if (SUCCEEDED(pAcc->get_accName(id,&name)) && name)
		{
			if (pLog)
				LogPrint(pLog,L"button found: '%s', %d\r\n",name,id.intVal);
			if (_wcsicmp(name,g_ButtonCopy)==0 || _wcsicmp(name,g_ButtonMove)==0)
			{
				m_YesButton.first=pAcc;
				m_YesButton.second=id.intVal;
				if (pLog)
					LogPrint(pLog,L"YES button: %p, %d\r\n",pAcc,id.intVal);
			}
			else if (_wcsicmp(name,g_ButtonDontCopy)==0 || _wcsicmp(name,g_ButtonDontMove)==0)
			{
				m_NoButton.first=pAcc;
				m_NoButton.second=id.intVal;
				if (pLog)
					LogPrint(pLog,L"NO button: %p, %d\r\n",pAcc,id.intVal);
			}
			else if (_wcsicmp(name,g_ButtonCancel)==0)
			{
				m_Cancel.first=pAcc;
				m_Cancel.second=id.intVal;
				if (pLog)
					LogPrint(pLog,L"CANCEL button: %p, %d\r\n",pAcc,id.intVal);
			}
		}
	}
	if (role.intVal==ROLE_SYSTEM_CHECKBUTTON)
	{
		// hopefully there is only one checkbox
		m_CheckBox.first=pAcc;
		m_CheckBox.second=id.intVal;
	}
}

void CClassicCopyFile::EnumAccChildren( IAccessible *pAcc, CString *pLog )
{
	AddAccChild(pAcc,CComVariant(CHILDID_SELF),pLog);
	long count;
	pAcc->get_accChildCount(&count);
	CComVariant children[20];
	AccessibleChildren(pAcc,0,count,children,&count);
	for (int i=0;i<count;i++)
	{
		if (children[i].vt==VT_DISPATCH)
		{
			CComQIPtr<IAccessible> pChild=children[i].pdispVal;
			if (pChild)
				EnumAccChildren(pChild,pLog);
		}
		else
			AddAccChild(pAcc,children[i],pLog);
	}
}

void CClassicCopyFile::GetFileInfo( IAccessible *pAcc, bool bSrc )
{
	long count;
	pAcc->get_accChildCount(&count);
	CComVariant children[20];
	AccessibleChildren(pAcc,0,count,children,&count);

	wchar_t fname[_MAX_PATH]=L"";
	wchar_t dir[_MAX_PATH]=L"";
	CString size;
	CString date;

	// get the file name, directory, size and date
	for (int i=0;i<count;i++)
	{
		CComBSTR name;
		if (children[i].vt==VT_DISPATCH)
		{
			CComQIPtr<IAccessible> pChild=children[i].pdispVal;
			if (pChild)
				pChild->get_accName(CComVariant(CHILDID_SELF),&name);
		}
		else
		{
			pAcc->get_accName(children[i],&name);
		}
		switch (i)
		{
			case 2: if (wcslen(name)<_countof(fname)) wcscpy_s(fname,name); break;
			case 3: if (wcslen(name)<_countof(dir)) wcscpy_s(dir,name); break;
			case 4: size=name; break;
			case 5: date=name; break;
		}
	}

	if (bSrc)
	{
		m_FileName=fname;
		m_SrcSize=size;
		m_SrcTime=date;
	}
	else
	{
		m_DstSize=size;
		m_DstTime=date;
	}

	if (!fname[0] || !dir[0]) return;

	wchar_t fname2[_MAX_PATH];
	memcpy(fname2,fname,sizeof(fname2));
	*PathFindExtension(fname2)=0;

	int len1=Strlen(fname2);
	// the directory text is something like "filename (directory)". we need to parse out the real directory name
	int len2=Strlen(dir);
	if (dir[0]==0x202A) len1++; // for Arabic the first character is some RTL marker. needs to be skipped
	if (dir[len1]==0x1E) len1++; // for Hebrew there is an extra control character after the file name
	if (len1+1>=len2 || dir[len1]!=L' ' || dir[len1+1]!=L'(' || dir[len2-1]!=L')') return;
	if (dir[len1+2]==0x202A) len1++; // for Hebrew the first character is some RTL marker. needs to be skipped
	if (dir[len2-2]==0x202C) len2--; // for Hebrew the last character is some RTL marker. needs to be skipped
	dir[len2-1]=0;

	// construct the full file name
	wchar_t path[_MAX_PATH];
	_wmakepath_s(path,NULL,dir+len1+2,fname,NULL);

	if (!bSrc)
	{
		DWORD attrib=GetFileAttributes(path);
		if (attrib!=INVALID_FILE_ATTRIBUTES)
		{
			if (attrib&FILE_ATTRIBUTE_READONLY) m_bReadOnly=true;
			if (attrib&FILE_ATTRIBUTE_SYSTEM) m_bSystem=true;
		}
	}

	// get file icon
	HICON hIcon=NULL;
	CAbsolutePidl pidl;
	if (SUCCEEDED(SHParseDisplayName(path,NULL,&pidl,0,NULL)) && pidl)
	{
		int iconSize=GetSystemMetrics(SM_CXICON);
		HBITMAP hBitmap=NULL;
		CComPtr<IShellItemImageFactory> pFactory;
		if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItemImageFactory,(void**)&pFactory)) && pFactory)
		{
			SIZE size={iconSize,iconSize};
			if (FAILED(pFactory->GetImage(size,SIIGBF_ICONONLY,&hBitmap)))
				hBitmap=NULL;
		}

		if (hBitmap)
		{
			HBITMAP hMonoBitmap=CreateBitmap(iconSize,iconSize,1,1,NULL);
			ICONINFO info={TRUE,0,0,hMonoBitmap,hBitmap};
			hIcon=CreateIconIndirect(&info);
			DeleteObject(hMonoBitmap);
			DeleteObject(hBitmap);
		}
	}
	if (!hIcon) return;

	if (bSrc)
		m_SrcIcon=hIcon;
	else
		m_DstIcon=hIcon;
}

const int WM_BRINGFOREGROUND=WM_USER+11;

INT_PTR CALLBACK CClassicCopyFile::DialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_INITDIALOG)
	{
		SetWindowText(hwndDlg,FindTranslation(L"Copy.Title",L"Confirm File Replace"));
		CClassicCopyFile *pThis=(CClassicCopyFile*)lParam;
		wchar_t text[_MAX_PATH*2];
		if (pThis->m_bSystem)
		{
			Sprintf(text,_countof(text),FindTranslation(L"Copy.SubtitleSys",L"This folder already contains a system file named '%s'."),pThis->m_FileName);
			if (GetSettingInt(L"OverwriteAlertLevel")>=1)
				PlaySound(L".Default",NULL,SND_APPLICATION|SND_ALIAS|SND_ASYNC|SND_NODEFAULT|SND_SYSTEM);
		}
		else if (pThis->m_bReadOnly)
		{
			Sprintf(text,_countof(text),FindTranslation(L"Copy.SubtitleRO",L"This folder already contains a read-only file named '%s'."),pThis->m_FileName);
			if (GetSettingInt(L"OverwriteAlertLevel")>=2)
				PlaySound(L".Default",NULL,SND_APPLICATION|SND_ALIAS|SND_ASYNC|SND_NODEFAULT|SND_SYSTEM);
		}
		else
			Sprintf(text,_countof(text),FindTranslation(L"Copy.Subtitle",L"This folder already contains a file named '%s'."),pThis->m_FileName);
		SetDlgItemText(hwndDlg,IDC_STATICFNAME,text);

		// load icon for file conflict (146) from Shell32.dll
		HMODULE hShell32=GetModuleHandle(L"Shell32.dll");
		pThis->m_Icon=LoadIcon(hShell32,MAKEINTRESOURCE(146));
		if (pThis->m_Icon)
			SendDlgItemMessage(hwndDlg,IDC_STATICICON1,STM_SETICON,(LPARAM)pThis->m_Icon,0);

		// set the localized text
		SetDlgItemText(hwndDlg,IDC_STATICPROMPT1,FindTranslation(L"Copy.Prompt1",L"Do you want to replace the existing file:"));
		SetDlgItemText(hwndDlg,IDC_STATICDSTSIZE,pThis->m_DstSize);
		SetDlgItemText(hwndDlg,IDC_STATICDSTTIME,pThis->m_DstTime);
		SetDlgItemText(hwndDlg,IDC_STATICPROMPT2,FindTranslation(L"Copy.Prompt2",L"with this one?"));
		SendDlgItemMessage(hwndDlg,IDC_STATICDSTICON,STM_SETICON,(LPARAM)pThis->m_DstIcon,0);
		SetDlgItemText(hwndDlg,IDC_STATICSRCSIZE,pThis->m_SrcSize);
		SetDlgItemText(hwndDlg,IDC_STATICSRCTIME,pThis->m_SrcTime);
		SendDlgItemMessage(hwndDlg,IDC_STATICSRCICON,STM_SETICON,(LPARAM)pThis->m_SrcIcon,0);
		SetDlgItemText(hwndDlg,IDOK,FindTranslation(L"Copy.Yes",L"&Yes"));
		SetDlgItemText(hwndDlg,IDNO,FindTranslation(L"Copy.No",L"&No"));
		if (GetDlgItem(hwndDlg,IDYES))
			SetDlgItemText(hwndDlg,IDYES,FindTranslation(L"Copy.YesAll",L"Yes to &All"));
		if (GetDlgItem(hwndDlg,IDCANCEL))
			SetDlgItemText(hwndDlg,IDCANCEL,FindTranslation(L"Copy.Cancel",L"Cancel"));
		Sprintf(text,_countof(text),L"<a>%s</a>",FindTranslation(L"Copy.More",L"&More..."));
		SetDlgItemText(hwndDlg,IDC_LINKMORE,text);
		PostMessage(hwndDlg,WM_BRINGFOREGROUND,0,0);
		return TRUE;
	}
	if (uMsg==WM_BRINGFOREGROUND)
	{
		// bring window to front (sometimes on Windows7 it shows up behind Explorer)
		SetForegroundWindow(hwndDlg);
		return TRUE;
	}
	if (uMsg==WM_COMMAND && (wParam==IDOK || wParam==IDYES || wParam==IDNO || wParam==IDCANCEL))
	{
		EndDialog(hwndDlg,wParam);
		return TRUE;
	}
	if (uMsg==WM_NOTIFY)
	{
		NMHDR *pHdr=(NMHDR*)lParam;
		if (pHdr->idFrom==IDC_LINKMORE && (pHdr->code==NM_CLICK || pHdr->code==NM_RETURN))
		{
			EndDialog(hwndDlg,IDC_LINKMORE);
			return TRUE;
		}
	}
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////

// CClassicCopyFolder - this is the implementation of the Copy UI dialog box for folders

class CClassicCopyFolder
{
public:
	CClassicCopyFolder( void ) { m_Icon=NULL; m_Original=NULL; }
	~CClassicCopyFolder( void );

	bool Run( HWND hWnd );

private:
	HICON m_Icon;
	HWND m_Original;

	static INT_PTR CALLBACK DialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam );
};

CClassicCopyFolder::~CClassicCopyFolder( void )
{
	if (m_Icon) DestroyIcon(m_Icon);
}

// Show the dialog box. Returns true to suppress the original task dialog box
bool CClassicCopyFolder::Run( HWND hWnd )
{
	m_Original=hWnd;
	const int ID_ALLCHECK=16663;
	HWND check=GetDlgItem(hWnd,ID_ALLCHECK);
	bool bMulti=(check && (GetWindowLong(check,GWL_STYLE)&WS_VISIBLE));
	// pick the correct dialog template (for single and multiple files, for LTR and RTL)
	int dlg=bMulti?(IsLanguageRTL()?IDD_FOLDERMULTIR:IDD_FOLDERMULTI):(IsLanguageRTL()?IDD_FOLDERR:IDD_FOLDER);

	HWND parent=GetWindow(GetAncestor(hWnd,GA_ROOT),GW_OWNER);

	int res=(int)DialogBoxParam(g_Instance,MAKEINTRESOURCE(dlg),parent,DialogProc,(LPARAM)this);

	if (res==IDOK) // Yes button for single folder
	{
		// Yes was pressed, proceed with the operation
		PostMessage(hWnd,WM_COMMAND,IDYES,(LPARAM)GetDlgItem(hWnd,IDYES));
	}
	else if (res==IDNO)
	{
		// No
		if (bMulti)
		{
			if (GetKeyState(VK_SHIFT)<0)
			{
				CheckDlgButton(hWnd,ID_ALLCHECK,BST_CHECKED);
				SendMessage(hWnd,WM_COMMAND,ID_ALLCHECK,(LPARAM)check);
			}
			PostMessage(hWnd,WM_COMMAND,IDNO,(LPARAM)GetDlgItem(hWnd,IDNO)); // Skip
		}
		else
			PostMessage(hWnd,WM_COMMAND,IDCANCEL,(LPARAM)GetDlgItem(hWnd,IDCANCEL)); // No
	}
	else if (res==IDYES)
	{
		// Yes to All
		CheckDlgButton(hWnd,ID_ALLCHECK,BST_CHECKED);
		SendMessage(hWnd,WM_COMMAND,ID_ALLCHECK,(LPARAM)check);
		PostMessage(hWnd,WM_COMMAND,IDYES,(LPARAM)GetDlgItem(hWnd,IDYES));
	}
	if (res==IDCANCEL)
	{
		// Cancel
		if (GetKeyState(VK_SHIFT)<0 || GetKeyState(VK_CONTROL)<0)
			return false; // // Shift+Cancel or Ctrl+Cancel was clicked - show the original dialog box
		PostMessage(hWnd,WM_COMMAND,IDCANCEL,(LPARAM)GetDlgItem(hWnd,IDCANCEL)); // No
	}
	if (res==IDC_LINKMORE)
	{
		// More... was clicked - show the original dialog box
		return false;
	}
	return true;
}

INT_PTR CALLBACK CClassicCopyFolder::DialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_INITDIALOG)
	{
		SetWindowText(hwndDlg,FindTranslation(L"Folder.Title",L"Confirm Folder Replace"));
		CClassicCopyFolder *pThis=(CClassicCopyFolder*)lParam;
		wchar_t text[2048];
		// find the link control and get its text
		HWND link=FindWindowEx(pThis->m_Original,NULL,WC_LINK,NULL);
		if (link)
			GetWindowText(link,text,_countof(text));
		else
			text[0]=0;
		Strcat(text,_countof(text),L"\r\n\r\n");
		Strcat(text,_countof(text),FindTranslation(L"Folder.Prompt",L"Do you still want to move or copy the folder?"));
		SetDlgItemText(hwndDlg,IDC_STATICFNAME,text);

		// load icon for file conflict (146) from Shell32.dll
		HMODULE hShell32=GetModuleHandle(L"Shell32.dll");
		pThis->m_Icon=LoadIcon(hShell32,MAKEINTRESOURCE(146));
		if (pThis->m_Icon)
			SendDlgItemMessage(hwndDlg,IDC_STATICICON1,STM_SETICON,(LPARAM)pThis->m_Icon,0);

		// set the localized text
		SetDlgItemText(hwndDlg,IDOK,FindTranslation(L"Copy.Yes",L"&Yes"));
		SetDlgItemText(hwndDlg,IDNO,FindTranslation(L"Copy.No",L"&No"));
		if (GetDlgItem(hwndDlg,IDYES))
			SetDlgItemText(hwndDlg,IDYES,FindTranslation(L"Copy.YesAll",L"Yes to &All"));
		if (GetDlgItem(hwndDlg,IDCANCEL))
			SetDlgItemText(hwndDlg,IDCANCEL,FindTranslation(L"Copy.Cancel",L"Cancel"));
		Sprintf(text,_countof(text),L"<a>%s</a>",FindTranslation(L"Copy.More",L"&More..."));
		SetDlgItemText(hwndDlg,IDC_LINKMORE,text);
		PostMessage(hwndDlg,WM_BRINGFOREGROUND,0,0);
		return TRUE;
	}
	if (uMsg==WM_BRINGFOREGROUND)
	{
		// bring window to front (sometimes on Windows7 it shows up behind Explorer)
		SetForegroundWindow(hwndDlg);
		return TRUE;
	}
	if (uMsg==WM_COMMAND && (wParam==IDOK || wParam==IDYES || wParam==IDNO || wParam==IDCANCEL))
	{
		EndDialog(hwndDlg,wParam);
		return TRUE;
	}
	if (uMsg==WM_NOTIFY)
	{
		NMHDR *pHdr=(NMHDR*)lParam;
		if (pHdr->idFrom==IDC_LINKMORE && (pHdr->code==NM_CLICK || pHdr->code==NM_RETURN))
		{
			EndDialog(hwndDlg,IDC_LINKMORE);
			return TRUE;
		}
	}
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////

static CComPtr<IAccessible> FindMoreButton( IAccessible *pAcc )
{
	CComVariant state;
	CComVariant self(CHILDID_SELF);
	pAcc->get_accState(self,&state);
	if (!(state.intVal&(STATE_SYSTEM_UNAVAILABLE|STATE_SYSTEM_INVISIBLE)))
	{
		CComVariant role;
		pAcc->get_accRole(self,&role);
		if (role.intVal==ROLE_SYSTEM_PUSHBUTTON)
		{
			CComBSTR name;
			if (SUCCEEDED(pAcc->get_accName(self,&name)) && name && _wcsicmp(name,g_ButtonMore)==0)
				return pAcc;
		}
	}

	CComVariant children[30];
	long count;
	AccessibleChildren(pAcc,0,_countof(children),children,&count);
	for (int i=0;i<count;i++)
	{
		if (children[i].vt==VT_DISPATCH)
		{
			CComQIPtr<IAccessible> pChild=children[i].pdispVal;
			if (pChild)
			{
				CComPtr<IAccessible> pRes=FindMoreButton(pChild);
				if (pRes) return pRes;
			}
		}
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

static LRESULT CALLBACK WindowProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_WINDOWPOSCHANGING)
	{
		WINDOWPOS *pos=(WINDOWPOS*)lParam;
		if (pos->flags&SWP_SHOWWINDOW)
		{
			wchar_t title[256];
			GetWindowText(hWnd,title,_countof(title));

			if (_wcsicmp(title,g_TitleMove)==0 || _wcsicmp(title,g_TitleCopy)==0)
			{
				// file UI
				if (GetSettingBool(L"ReplaceFileUI"))
				{
					CString log;
					CComPtr<IAccessible> pAcc;
					HRESULT h=AccessibleObjectFromWindow(hWnd,OBJID_WINDOW,IID_IAccessible,(void**)&pAcc);
					if (SUCCEEDED(h) && pAcc)
					{
						CClassicCopyFile copy;
						if (copy.Run(hWnd,pAcc,g_bLogLevel?&log:NULL))
						{
							pos->x=pos->y=-20000;
							pos->flags&=~(SWP_SHOWWINDOW|SWP_NOMOVE);
						}
					}
					else if (g_bLogLevel)
						LogPrint(&log,L"AccessibleObjectFromWindow: error=0x%X, hWnd=0x%p",h,hWnd);

					if (g_bLogLevel)
					{
						wchar_t fname[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell";
						DoEnvironmentSubst(fname,_MAX_PATH);
						SHCreateDirectory(NULL,fname);
						Strcat(fname,_countof(fname),L"\\ExplorerLog.txt");
						if (!log.IsEmpty())
						{
							FILE *f;
							if (_wfopen_s(&f,fname,L"wb")==0)
							{
								fwprintf(f,L"\xFEFF");

								OSVERSIONINFOEX ver={sizeof(ver)};
								#pragma warning(suppress:4996)
								GetVersionEx((OSVERSIONINFO*)&ver);
								fwprintf(f,L"version = %d.%d.%d - %d.%d\r\n\r\n",ver.dwMajorVersion,ver.dwMinorVersion,ver.dwBuildNumber,ver.wServicePackMajor,ver.wServicePackMinor);

								wchar_t languages[100]={0};
								ULONG size=4; // up to 4 languages
								ULONG len=_countof(languages);
								GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&size,languages,&len);
								for (const wchar_t *lang=languages;*lang;lang+=wcslen(lang)+1)
									fwprintf(f,L"language = %s\r\n",lang);

								fwprintf(f,L"\r\n");
								fwprintf(f,L"g_ButtonMove = '%s'\r\n",g_ButtonMove);
								fwprintf(f,L"g_ButtonDontMove = '%s'\r\n",g_ButtonDontMove);
								fwprintf(f,L"g_ButtonCopy = '%s'\r\n",g_ButtonCopy);
								fwprintf(f,L"g_ButtonDontCopy = '%s'\r\n",g_ButtonDontCopy);
								fwprintf(f,L"g_ButtonCancel = '%s'\r\n",g_ButtonCancel);
								fwprintf(f,L"\r\n");

								fwrite((const wchar_t*)log,log.GetLength(),2,f);
								fclose(f);
							}
						}
						else
							DeleteFile(fname);
					}
				}
			}
			else if (_wcsicmp(title,g_TitleFolder)==0)
			{
				// folder UI
				if (GetSettingBool(L"ReplaceFolderUI"))
				{
					CClassicCopyFolder copy;
					if (copy.Run(hWnd))
					{
						pos->x=pos->y=-20000;
						pos->flags&=~(SWP_SHOWWINDOW|SWP_NOMOVE);
					}
				}
			}
			else
			{
				// look for progress bar
				if (GetSettingBool(L"EnableMore"))
				{
					HWND progress=FindChildWindow(hWnd,PROGRESS_CLASS);
					if (progress)
					{
						bool bDef;
						int delay=GetSettingInt(L"MoreProgressDelay",bDef);
						if (bDef)
						{
							delay=0;
#if _WIN32_WINNT < _WIN32_WINNT_WIN7
							if (GetWinVersion()>=_WIN32_WINNT_WIN7)
#endif
							{
								BOOL comp;
								if (SUCCEEDED(DwmIsCompositionEnabled(&comp)) && comp)
									delay=500;
							}
						}
						SetTimer(hWnd,'CLEX',delay,NULL);
						return DefSubclassProc(hWnd,uMsg,wParam,lParam);
					}
				}
			}
			LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
			RemoveWindowSubclass(hWnd,WindowProc,uIdSubclass);
			return res;
		}
	}
	if (uMsg==WM_TIMER && wParam=='CLEX')
	{
		KillTimer(hWnd,wParam);

		CComPtr<IAccessible> pAcc;
		HRESULT h=AccessibleObjectFromWindow(hWnd,OBJID_WINDOW,IID_IAccessible,(void**)&pAcc);
		if (SUCCEEDED(h) && pAcc)
		{
			CComPtr<IAccessible> pMore=FindMoreButton(pAcc);
			if (pMore) pMore->accDoDefaultAction(CComVariant(CHILDID_SELF));
		}

		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		RemoveWindowSubclass(hWnd,WindowProc,uIdSubclass);
		return res;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CALLBACK ClassicCopyHook( int nCode, WPARAM wParam, LPARAM lParam )
{
	if (nCode==HCBT_CREATEWND)
	{
		HWND hWnd=(HWND)wParam;
		CBT_CREATEWND *create=(CBT_CREATEWND*)lParam;
		HINSTANCE hInst=(HINSTANCE)GetWindowLongPtr(hWnd,GWLP_HINSTANCE);
		if (create->lpcs->lpszName && (intptr_t)create->lpcs->lpszClass==32770 && hInst==g_hShell32)
		{
			WaitDllInitThread();
			static LONG id;
			int i=InterlockedIncrement(&id);
			SetWindowSubclass(hWnd,WindowProc,i,0);
		}
	}
	TlsData *pTlsData=GetTlsData();
	return CallNextHookEx(pTlsData->copyHook,nCode,wParam,lParam);
}

void InitClassicCopyProcess( void )
{
	// load UI text from shell32.dll
	// the text is used to locate controls in the copy dialog by name
	g_hShell32=GetModuleHandle(L"shell32.dll");
	LoadString(g_hShell32,17027,g_TitleMove,256);
	LoadString(g_hShell32,17024,g_TitleCopy,256);
	LoadString(g_hShell32,16705,g_TitleFolder,256);
	LoadString(g_hShell32,13610,g_ButtonMove,256);
	LoadString(g_hShell32,13623,g_ButtonDontMove,256);
	LoadString(g_hShell32,13604,g_ButtonCopy,256);
	LoadString(g_hShell32,13606,g_ButtonDontCopy,256);
	LoadString(g_hShell32,13588,g_ButtonCancel,256);
	LoadString(g_hShell32,32992,g_ButtonMore,256);
}

void InitClassicCopyThread( void )
{
	TlsData *pTlsData=GetTlsData();
	if (!pTlsData->copyHook)
		pTlsData->copyHook=SetWindowsHookEx(WH_CBT,ClassicCopyHook,g_Instance,GetCurrentThreadId());
}

void FreeClassicCopyThread( void )
{
	TlsData *pTlsData=GetTlsData();
	if (pTlsData->copyHook)
	{
		UnhookWindowsHookEx(pTlsData->copyHook);
		pTlsData->copyHook=NULL;
	}
}
