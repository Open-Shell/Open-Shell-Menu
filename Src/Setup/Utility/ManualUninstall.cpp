// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#ifndef _WIN64

#define STRICT_TYPED_ITEMIDS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit

#include "resource.h"
#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>
#include <atlstr.h>
#include <atltypes.h>
#include <string>
#include "ResourceHelper.h"
#include "ComHelper.h"
#include "StringUtils.h"
#include "SettingsUIHelper.h"
#include <lm.h>
#include "..\StartMenu\StartMenuDLL\StartMenuDLL.h"

#define EXPLORER_CLSID L"{ECD4FC4D-521C-11D0-B792-00A0C90312E1}"
#define EMULATION_KEY L"TreatAs"


//#define SAFE_MODE

#ifdef SAFE_MODE
#define RegDeleteTree2(x,y) 5
#define RegDeleteValue2(x,y) 5
#define DeleteFile2(x) (SetLastError(3), FALSE)
#define MoveFileEx2(x,y,z) (SetLastError(5), FALSE)
#define SHFileOperation2(x) 0x78
#define RemoveDirectory2(x) FALSE
#else
#define RegDeleteTree2(x,y) RegDeleteTree(x,y)
#define RegDeleteValue2(x,y) RegDeleteValue(x,y)
#define DeleteFile2(x) DeleteFile(x)
#define MoveFileEx2(x,y,z) MoveFileEx(x,y,z)
#define SHFileOperation2(x) SHFileOperation(x)
#define RemoveDirectory2(x) RemoveDirectory(x)
#endif

// files to delete from the Open-Shell folder
static const wchar_t *g_InstalledFiles[]=
{
	L"ClassicExplorer32.dll",
	L"ClassicExplorer64.dll",
	L"ClassicExplorerSettings.exe",
	L"ClassicIEDLL_32.dll",
	L"ClassicIEDLL_64.dll",
	L"ClassicIE_32.exe",
	L"ClassicIE_64.exe",
	L"DesktopToasts.dll",
	L"OpenShell.chm",
	L"OpenShellReadme.rtf",
	L"Update.exe",
	L"StartMenu.exe",
	L"StartMenuDLL.dll",
	L"ExplorerL10N.ini",
	L"HISTORY.txt",
	L"IE Settings.lnk",
	L"PolicyDefinitions.zip",
	L"Start Menu Settings.lnk",
	L"Start Screen.lnk",
	L"StartMenuHelperL10N.ini",
	L"StartMenuL10N.ini",

	L"de-DE.dll",
	L"fr-FR.dll",
	L"it-IT.dll",
	L"pl-PL.dll",
	L"ru-RU.dll",
	L"zh-CN.dll",
	L"zh-TW.dll",
};

// skin files to delete from the Open-Shell\Skins folder
static const wchar_t *g_InstalledSkins[]=
{
	L"Classic Skin.skin",
	L"Classic Skin.skin7",
	L"Full Glass.skin",
	L"Immersive.skin",
	L"Immersive.skin7",
	L"Metallic.skin7",
	L"Metro.skin",
	L"Metro.skin7",
	L"Midnight.skin7",
	L"Smoked Glass.skin",
	L"Windows 8.skin",
	L"Windows 8.skin7",
	L"Windows Aero.skin",
	L"Windows Aero.skin7",
	L"Windows Basic.skin",
	L"Windows XP Luna.skin",
};

// shortcuts to delete from the Programs\Open-Shell folder
static const wchar_t *g_InstalledShortcuts[]=
{
	L"Classic Explorer Settings.lnk",
	L"Classic IE Settings.lnk",
	L"Open-Shell Help.lnk",
	L"Open-Shell Readme.lnk",
	L"Open-Shell Update.lnk",
	L"Open-Shell Menu Settings.lnk",
};

// files to delete from the LOCALAPPDATA folder
static const wchar_t *g_LocalFiles[]=
{
	L"StartupLog.txt",
	L"UpdateLog.txt",
	L"ClassicIELog.txt",
	L"StartMenuLog.txt",
	L"DataCache.db",
	L"ModernSettings.dat",
};

// files to delete from the ALLUSERSPROFILE folder
static const wchar_t *g_ProgramDataFiles[]=
{
	L"update.ver",
};

///////////////////////////////////////////////////////////////////////////////

static bool DirectoryExists( const wchar_t *directory )
{
	DWORD attrib=GetFileAttributes(directory);
	return (attrib!=INVALID_FILE_ATTRIBUTES && (attrib&FILE_ATTRIBUTE_DIRECTORY));
}

static bool FileExists( const wchar_t *fname )
{
	return GetFileAttributes(fname)!=INVALID_FILE_ATTRIBUTES;
}

///////////////////////////////////////////////////////////////////////////////

class CUninstallDlg: public CDialogImpl<CUninstallDlg>
{
public:
	BEGIN_MSG_MAP( CUninstallDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_CTLCOLORSTATIC, OnColorStatic )
		COMMAND_HANDLER( IDCANCEL, BN_CLICKED, OnCancel )
		COMMAND_HANDLER( IDOK, BN_CLICKED, OnOK )
		COMMAND_HANDLER( IDC_BUTTONBROWSEPATH, BN_CLICKED, OnBrowse )
		COMMAND_HANDLER( IDC_CHECKALLUSERS, BN_CLICKED, OnCheck )
		NOTIFY_HANDLER( IDC_SYSLINK1, NM_CLICK, OnLink )
		NOTIFY_HANDLER( IDC_SYSLINK1, NM_RETURN, OnLink )
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	enum { IDD=IDD_UNINSTALL };

	CString GetPath( void ) const { return m_Path; }
	bool GetRemoveUserSettings( void ) const { return m_bRemoveUserSettings; }
	bool GetRemoveAllUserSettings( void ) const { return m_bRemoveAllUserSettings; }
	bool GetRemoveAdminSettings( void ) const { return m_bRemoveAdminSettings; }

protected:
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnColorStatic( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCheck( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnLink( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

private:
	CString m_Path;
	bool m_bRemoveUserSettings;
	bool m_bRemoveAllUserSettings;
	bool m_bRemoveAdminSettings;
	bool m_bEmptyPath;
};

LRESULT CUninstallDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_bRemoveUserSettings=m_bRemoveAllUserSettings=m_bRemoveAdminSettings=false;
	m_bEmptyPath=false;

	wchar_t path[_MAX_PATH]={0};
	{
		HKEY hkey=NULL;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,L"SOFTWARE\\OpenShell\\OpenShell",0,KEY_READ|KEY_QUERY_VALUE|KEY_WOW64_64KEY,&hkey)==ERROR_SUCCESS)
		{
			DWORD type=0;
			DWORD size=sizeof(path);
			if (RegQueryValueEx(hkey,L"Path",0,&type,(BYTE*)path,&size)!=ERROR_SUCCESS || type!=REG_SZ)
				path[0]=0;
		}
	}

	if (path[0]==0)
	{
		BOOL bWow64=FALSE;
		if (!IsWow64Process(GetCurrentProcess(),&bWow64))
			bWow64=FALSE;
		wchar_t defaultPath[_MAX_PATH];
		Strcpy(defaultPath,_countof(defaultPath),bWow64?L"%ProgramW6432%\\Open-Shell":L"%ProgramFiles%\\Open-Shell");
		DoEnvironmentSubst(defaultPath,_countof(defaultPath));
		wchar_t fname1[_MAX_PATH], fname2[_MAX_PATH], fname3[_MAX_PATH];
		Sprintf(fname1,_countof(fname1),L"%s\\StartMenu.exe",defaultPath);
		Sprintf(fname2,_countof(fname2),L"%s\\ClassicExplorer32.dll",defaultPath);
		Sprintf(fname3,_countof(fname3),L"%s\\ClassicIEDLL_32.dll",defaultPath);
		if (FileExists(fname1) || FileExists(fname2) || FileExists(fname3))
			Strcpy(path,_countof(path),defaultPath);
	}

	m_bEmptyPath=path[0]==0;
	if (m_bEmptyPath)
		SetDlgItemText(IDC_STATICPATH,L"The tool failed to locate the Open-Shell installation folder. Use the Browse button to locate the correct folder if you want the files in it to be deleted.");
	else
	{
		PathRemoveBackslash(path);
		SetDlgItemText(IDC_EDITPATH,path);
	}

	return TRUE;
}

LRESULT CUninstallDlg::OnColorStatic( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if ((HWND)lParam==GetDlgItem(IDC_STATICWARNING) || (HWND)lParam==GetDlgItem(IDC_STATICALLUSERS) || (m_bEmptyPath && (HWND)lParam==GetDlgItem(IDC_STATICPATH)))
	{
		SetTextColor((HDC)wParam,0x0000CF);
		SetBkMode((HDC)wParam,TRANSPARENT);
		return (LRESULT)GetStockObject(NULL_BRUSH);
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CUninstallDlg::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	EndDialog(0);
	return 0;
}

LRESULT CUninstallDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	GetDlgItemText(IDC_EDITPATH,m_Path);
	m_bRemoveUserSettings=IsDlgButtonChecked(IDC_CHECKSETTINGS)==BST_CHECKED;
	m_bRemoveAllUserSettings=IsDlgButtonChecked(IDC_CHECKALLUSERS)==BST_CHECKED;
	m_bRemoveAdminSettings=IsDlgButtonChecked(IDC_CHECKADMIN)==BST_CHECKED;
	EndDialog(1);
	return 0;
}

LRESULT CUninstallDlg::OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CComPtr<IFileOpenDialog> pDialog;
	if (SUCCEEDED(pDialog.CoCreateInstance(CLSID_FileOpenDialog,NULL,CLSCTX_INPROC_SERVER)))
	{
		pDialog->SetTitle(L"Locate the Open-Shell install folder");
		pDialog->SetOptions(FOS_FILEMUSTEXIST|FOS_DONTADDTORECENT|FOS_DEFAULTNOMINIMODE|FOS_NOCHANGEDIR|FOS_PICKFOLDERS);
		HRESULT hr=pDialog->Show(m_hWnd);
		CComPtr<IShellItem> pResult;
		if (hr==S_OK)
			pDialog->GetResult(&pResult);

		if (pResult)
		{
			CComString pName;
			pResult->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
			SetDlgItemText(IDC_EDITPATH,pName);
			m_bEmptyPath=false;
			GetDlgItem(IDC_STATICPATH).Invalidate();
		}
	}
	return 0;
}

LRESULT CUninstallDlg::OnCheck( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	GetDlgItem(IDC_STATICALLUSERS).ShowWindow(IsDlgButtonChecked(IDC_CHECKALLUSERS)==BST_CHECKED?SW_SHOW:SW_HIDE);
	return 0;
}

LRESULT CUninstallDlg::OnLink( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	ShellExecute(m_hWnd,NULL,L"control",L"appwiz.cpl",NULL,SW_SHOWNORMAL);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

class CResultsDlg: public CResizeableDlg<CResultsDlg>
{
public:
	CResultsDlg( bool bErrors, bool bReboot, CString report ) { m_bErrors=bErrors; m_bReboot=bReboot; m_Report=report; }

	BEGIN_MSG_MAP( CResultsDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_CTLCOLORSTATIC, OnColorStatic )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_GETMINMAXINFO, OnGetMinMaxInfo )
		COMMAND_HANDLER( IDC_BUTTONCLOSE, BN_CLICKED, OnClose )
		COMMAND_HANDLER( IDC_BUTTONREBOOT, BN_CLICKED, OnReboot )
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_STATICRESULT,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITRESULT,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDC_BUTTONREBOOT,MOVE_MOVE_X|MOVE_MOVE_Y)
		RESIZE_CONTROL(IDC_BUTTONCLOSE,MOVE_MOVE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

	enum { IDD=IDD_UNINSTALL_RESULTS };

protected:
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnColorStatic( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnClose( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnReboot( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );

private:
	bool m_bErrors;
	bool m_bReboot;
	CString m_Report;
};

LRESULT CResultsDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	InitResize(MOVE_MODAL);
	if (m_bErrors)
	{
		SetDlgItemText(IDC_STATICRESULT,L"The Open-Shell removal tool encountered some errors. Please, restart your computer and try again. If the problem is not resolved"
			L" seek help in the Open-Shell forums: https://github.com/Open-Shell/Open-Shell-Menu/discussions. Copy the following report and post it in the forum. The report is also saved in a file OpenShellReport.txt on your desktop.");
	}
	else if (m_bReboot)
	{
		SetDlgItemText(IDC_STATICRESULT,L"The Open-Shell removal tool finished. A restart is required to complete the process."
			L"Copy the following report for your records. The report is also saved in a file OpenShellReport.txt on your desktop.");
	}
	else
	{
		SetDlgItemText(IDC_STATICRESULT,L"The Open-Shell removal tool finished."
			L"Copy the following report for your records. The report is also saved in a file OpenShellReport.txt on your desktop.");
	}

	SetDlgItemText(IDC_EDITRESULT,m_Report);

	GetDlgItem(IDC_BUTTONREBOOT).ShowWindow((m_bErrors || m_bReboot)?SW_SHOW:SW_HIDE);
	SetDlgItemText(IDC_BUTTONCLOSE,(m_bErrors || m_bReboot)?L"Restart Later":L"Close");
	GetDlgItem(IDC_EDITRESULT).SetFocus();
	return FALSE;
}

LRESULT CResultsDlg::OnColorStatic( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_bErrors && (HWND)lParam==GetDlgItem(IDC_STATICRESULT))
	{
		SetTextColor((HDC)wParam,0x0000CF);
		SetBkMode((HDC)wParam,TRANSPARENT);
		return (LRESULT)GetStockObject(NULL_BRUSH);
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CResultsDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CResultsDlg>::OnSize();
	return 0;
}

LRESULT CResultsDlg::OnClose( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	EndDialog(0);
	return 0;
}

LRESULT CResultsDlg::OnReboot( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	EndDialog(1);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

class CProgressDlg: public CDialogImpl<CProgressDlg>
{
public:
	BEGIN_MSG_MAP( CProgressDlg )
	END_MSG_MAP()

	enum { IDD=IDD_UNINSTALL_PROGRESS };
};

///////////////////////////////////////////////////////////////////////////////

static bool g_bHasErrors=false;
static bool g_bRebootRequired=false;
static std::vector<wchar_t> g_Report;

static void AdjustPrivileges( void )
{
	HANDLE hToken;
	if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken))
	{
		{
			TOKEN_PRIVILEGES tp={1};
			if (LookupPrivilegeValue(NULL,L"SeBackupPrivilege",&tp.Privileges[0].Luid))
				tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
		}
		{
			TOKEN_PRIVILEGES tp={1};
			if (LookupPrivilegeValue(NULL,L"SeRestorePrivilege",&tp.Privileges[0].Luid))
				tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
		}
		CloseHandle(hToken);
	}
}

static void LogMessage( int error, const wchar_t *format, ... )
{
	wchar_t message[1024];
	int len=0;
	if (error!=-1)
		len+=Strcpy(message,_countof(message),L"ERROR: ");
	va_list args;
	va_start(args,format);
	len+=Vsprintf(message+len,_countof(message)-len,format,args);
	va_end(args);
	if (error!=-1)
	{
		g_bHasErrors=true;
		len+=Sprintf(message+len,_countof(message)-len,L" ");
		len+=FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,error,0,message+len,_countof(message)-len,NULL);
	}
	len+=Sprintf(message+len,_countof(message)-len,L"\r\n");
	OutputDebugString(message);
	g_Report.insert(g_Report.end(),message,message+len);
}

static void SaveReportFile( void )
{
	wchar_t path[_MAX_PATH]=L"%USERPROFILE%\\Desktop\\OpenShellReport.txt";
	DoEnvironmentSubst(path,_countof(path));
	FILE *f;
	if (_wfopen_s(&f,path,L"wb")==0)
	{
		if (!g_Report.empty())
			fwrite(&g_Report[0],2,g_Report.size(),f);
		fclose(f);
	}
}

static void RemoveShellExtKey(const wchar_t* progID)
{
	static const auto ShellExtName = L"StartMenuExt";
	auto contextMenuHandlers = std::wstring(progID) + L"\\ShellEx\\ContextMenuHandlers";
	auto startMenuExt = contextMenuHandlers + L"\\" + ShellExtName;

	HKEY hkey = NULL;
	if (RegOpenKeyEx(HKEY_CLASSES_ROOT, startMenuExt.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hkey) == ERROR_SUCCESS)
	{
		RegCloseKey(hkey);
		LogMessage(-1, L"Deleting registry key HKEY_CLASSES_ROOT\\%s", startMenuExt.c_str());
		auto error = RegCreateKeyEx(HKEY_CLASSES_ROOT, contextMenuHandlers.c_str(), NULL, NULL, REG_OPTION_BACKUP_RESTORE, KEY_WRITE | DELETE | KEY_WOW64_64KEY, NULL, &hkey, NULL);
		if (error == ERROR_SUCCESS)
		{
			error = RegDeleteTree2(hkey, ShellExtName);
			if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
				LogMessage(error, L"Failed to delete registry key HKEY_CLASSES_ROOT\\%s.", startMenuExt.c_str());
			RegCloseKey(hkey);
		}
		else if (error != ERROR_FILE_NOT_FOUND)
		{
			LogMessage(error, L"Failed to open registry key HKEY_CLASSES_ROOT\\%s for writing.", contextMenuHandlers.c_str());
		}
	}
}

static bool RemoveRegistryKeys( bool bPin )
{
	HKEY hkey=NULL;
	int error;
	error=RegOpenKeyEx(HKEY_CLASSES_ROOT,L"CLSID\\" EXPLORER_CLSID L"\\" EMULATION_KEY,0,KEY_READ|KEY_WOW64_64KEY,&hkey);
	if (error==ERROR_SUCCESS)
	{
		RegCloseKey(hkey);
		LogMessage(-1,L"Deleting registry key HKEY_CLASSES_ROOT\\CLSID\\" EXPLORER_CLSID L"\\" EMULATION_KEY);
		error=RegCreateKeyEx(HKEY_CLASSES_ROOT,L"CLSID\\" EXPLORER_CLSID,NULL,NULL,REG_OPTION_BACKUP_RESTORE,KEY_WRITE|DELETE|KEY_WOW64_64KEY,NULL,&hkey,NULL);
		if (error==ERROR_SUCCESS)
		{
			error=RegDeleteTree2(hkey,EMULATION_KEY);
			if (error!=ERROR_SUCCESS)
				LogMessage(error,L"Failed to delete registry key HKEY_CLASSES_ROOT\\CLSID\\" EXPLORER_CLSID L"\\" EMULATION_KEY L".");
			RegCloseKey(hkey);
		}
		else
			LogMessage(error,L"Failed to open registry key HKEY_CLASSES_ROOT\\CLSID\\" EXPLORER_CLSID L" for writing.");

		if (RegOpenKeyEx(HKEY_CLASSES_ROOT,L"CLSID\\" EXPLORER_CLSID L"\\" EMULATION_KEY,0,KEY_READ|KEY_WOW64_64KEY,&hkey)==ERROR_SUCCESS)
		{
			RegCloseKey(hkey);
			SaveReportFile();
			wchar_t path[_MAX_PATH]=L"%USERPROFILE%\\Desktop\\OpenShellReport.txt";
			DoEnvironmentSubst(path,_countof(path));
			wchar_t message[1024];
			Sprintf(message,_countof(message),L"Failed to delete registry key HKEY_CLASSES_ROOT\\CLSID\\" EXPLORER_CLSID L"\\" EMULATION_KEY L".\r\n"
				L"You will have to delete it manually using regedit and then run this tool again.\r\n"
				L"The problem may be related to incorrect permissions or corrupted owner of the registry key or its parent.\r\n\r\n"
				L"Look at the file %s for more details about the failure.",path);
			MessageBox(NULL,message,L"Open-Shell Uninstaller",MB_OK|MB_ICONERROR);
			return false;
		}
	}

	if (bPin)
	{
		RemoveShellExtKey(L"Launcher.ImmersiveApplication");
		RemoveShellExtKey(L"Launcher.DesktopPackagedApplication");
		RemoveShellExtKey(L"Launcher.SystemSettings");
	}

	return true;
}

static void DeleteRegKeyHKCR( const wchar_t *keyName )
{
	int error=RegDeleteTree2(HKEY_CLASSES_ROOT,keyName);
	if (error!=ERROR_FILE_NOT_FOUND)
	{
		LogMessage(-1,L"Deleting registry key HKEY_CLASSES_ROOT\\%s",keyName);
		if (error!=ERROR_SUCCESS)
			LogMessage(error,L"Failed to delete registry key HKEY_CLASSES_ROOT\\%s.",keyName);
	}
}

static void DeleteRegKeyCLSID( const wchar_t *keyName, bool bIsWow64 )
{
	{
		// delete native key (32-bit on 32-bit OS and 64-bit on 64-bit OS)
		HKEY hkey=NULL;
		int error=RegOpenKeyEx(HKEY_CLASSES_ROOT,L"CLSID",0,KEY_WRITE|DELETE|KEY_WOW64_64KEY,&hkey);
		if (error==ERROR_SUCCESS)
		{
			int error=RegDeleteTree2(hkey,keyName);
			if (error!=ERROR_FILE_NOT_FOUND)
			{
				LogMessage(-1,L"Deleting registry key HKEY_CLASSES_ROOT\\CLSID\\%s",keyName);
				if (error!=ERROR_SUCCESS)
					LogMessage(error,L"Failed to delete registry key HKEY_CLASSES_ROOT\\CLSID\\%s.",keyName);
			}
			RegCloseKey(hkey);
		}
		else
		{
			LogMessage(-1,L"Deleting registry key HKEY_CLASSES_ROOT\\CLSID\\%s",keyName);
			LogMessage(error,L"Failed to open registry key HKEY_CLASSES_ROOT\\CLSID for writing.");
		}
	}

	if (bIsWow64)
	{
		// delete 32-bit on 64-bit OS
		HKEY hkey=NULL;
		int error=RegOpenKeyEx(HKEY_CLASSES_ROOT,L"CLSID",0,KEY_WRITE|DELETE|KEY_WOW64_32KEY,&hkey);
		if (error==ERROR_SUCCESS)
		{
			int error=RegDeleteTree2(hkey,keyName);
			if (error!=ERROR_FILE_NOT_FOUND)
			{
				LogMessage(-1,L"Deleting registry key HKEY_CLASSES_ROOT\\Wow6432Node\\CLSID\\%s",keyName);
				if (error!=ERROR_SUCCESS)
					LogMessage(error,L"Failed to delete registry key HKEY_CLASSES_ROOT\\Wow6432Node\\CLSID\\%s.",keyName);
			}
			RegCloseKey(hkey);
		}
		else
		{
			LogMessage(-1,L"Deleting registry key HKEY_CLASSES_ROOT\\Wow6432Node\\CLSID\\%s",keyName);
			LogMessage(error,L"Failed to open registry key HKEY_CLASSES_ROOT\\Wow6432Node\\CLSID for writing.");
		}
	}
}

static void DeleteRegKeySOFTWARE( const wchar_t *keyName, bool bIsWow64 )
{
	{
		// delete native key (32-bit on 32-bit OS and 64-bit on 64-bit OS)
		HKEY hkey=NULL;
		int error=RegOpenKeyEx(HKEY_LOCAL_MACHINE,L"SOFTWARE",0,KEY_WRITE|DELETE|KEY_WOW64_64KEY,&hkey);
		if (error==ERROR_SUCCESS)
		{
			int error=RegDeleteTree2(hkey,keyName);
			if (error!=ERROR_FILE_NOT_FOUND)
			{
				LogMessage(-1,L"Deleting registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\%s",keyName);
				if (error!=ERROR_SUCCESS)
					LogMessage(error,L"Failed to delete registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\%s.",keyName);
			}
			RegCloseKey(hkey);
		}
		else
		{
			LogMessage(-1,L"Deleting registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\%s",keyName);
			LogMessage(error,L"Failed to open registry key HKEY_LOCAL_MACHINE\\SOFTWARE for writing.");
		}
	}

	if (bIsWow64)
	{
		// delete 32-bit on 64-bit OS
		HKEY hkey=NULL;
		int error=RegOpenKeyEx(HKEY_LOCAL_MACHINE,L"SOFTWARE",0,KEY_WRITE|DELETE|KEY_WOW64_32KEY,&hkey);
		if (error==ERROR_SUCCESS)
		{
			int error=RegDeleteTree2(hkey,keyName);
			if (error!=ERROR_FILE_NOT_FOUND)
			{
				LogMessage(-1,L"Deleting registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s",keyName);
				if (error!=ERROR_SUCCESS)
					LogMessage(error,L"Failed to delete registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s.",keyName);
			}
			RegCloseKey(hkey);
		}
		else
		{
			LogMessage(-1,L"Deleting registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s",keyName);
			LogMessage(error,L"Failed to open registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node for writing.");
		}
	}
}

static void DeleteRegValueSOFTWARE( const wchar_t *keyName, const wchar_t *valueName, bool bIsWow64 )
{
	wchar_t keyName2[256];
	Sprintf(keyName2,_countof(keyName2),L"SOFTWARE\\%s",keyName);

	{
		// delete native value (32-bit on 32-bit OS and 64-bit on 64-bit OS)
		HKEY hkey=NULL;
		int error=RegOpenKeyEx(HKEY_LOCAL_MACHINE,keyName2,0,KEY_WRITE|DELETE|KEY_WOW64_64KEY,&hkey);
		if (error==ERROR_SUCCESS)
		{
			int error=RegDeleteValue2(hkey,valueName);
			if (error!=ERROR_FILE_NOT_FOUND)
			{
				LogMessage(-1,L"Deleting registry value HKEY_LOCAL_MACHINE\\SOFTWARE\\%s:%s",keyName,valueName);
				if (error!=ERROR_SUCCESS)
					LogMessage(error,L"Failed to delete registry value HKEY_LOCAL_MACHINE\\SOFTWARE\\%s:%s.",keyName,valueName);
			}
			RegCloseKey(hkey);
		}
		else if (error!=ERROR_FILE_NOT_FOUND)
		{
			LogMessage(-1,L"Deleting registry value HKEY_LOCAL_MACHINE\\SOFTWARE\\%s:%s",keyName,valueName);
			LogMessage(error,L"Failed to open registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\%s for writing.",keyName);
		}
	}

	if (bIsWow64)
	{
		// delete 32-bit on 64-bit OS
		HKEY hkey=NULL;
		int error=RegOpenKeyEx(HKEY_LOCAL_MACHINE,keyName2,0,KEY_WRITE|DELETE|KEY_WOW64_32KEY,&hkey);
		if (error==ERROR_SUCCESS)
		{
			int error=RegDeleteValue2(hkey,valueName);
			if (error!=ERROR_FILE_NOT_FOUND)
			{
				LogMessage(-1,L"Deleting registry value HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s:%s",keyName,valueName);
				if (error!=ERROR_SUCCESS)
					LogMessage(error,L"Failed to delete registry value HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s:%s.",keyName,valueName);
			}
			RegCloseKey(hkey);
		}
		else if (error!=ERROR_FILE_NOT_FOUND)
		{
			LogMessage(-1,L"Deleting registry value HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s:%s",keyName,valueName);
			LogMessage(error,L"Failed to open registry key HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%s for writing.",keyName);
		}
	}
}

static void DeleteInstallerKey( HKEY root, const wchar_t *rootName, const wchar_t *keyName, const wchar_t *valueName, const wchar_t *value )
{
	LogMessage(-1,L"Searching for installer registry key in %s\\%s",rootName,keyName);
	HKEY hkey=NULL;
	int error=RegOpenKeyEx(root,keyName,0,KEY_WRITE|DELETE|KEY_ENUMERATE_SUB_KEYS|KEY_WOW64_64KEY,&hkey);
	if (error==ERROR_SUCCESS)
	{
		std::vector<std::wstring> subKeyNames;
		for (int index=0;;index++)
		{
			wchar_t subKeyName[100];
			DWORD len=_countof(subKeyName);
			if (RegEnumKeyEx(hkey,index,subKeyName,&len,NULL,NULL,NULL,NULL)!=ERROR_SUCCESS)
				break;
			wchar_t buf[256];
			len=sizeof(buf);
			if (RegGetValue(hkey,subKeyName,valueName,RRF_RT_REG_SZ,NULL,(void*)buf,&len)==ERROR_SUCCESS)
			{
				if (len==wcslen(value)*2+2 && _wcsicmp(value,buf)==0)
					subKeyNames.push_back(subKeyName);
			}
		}
		for (std::vector<std::wstring>::const_iterator it=subKeyNames.begin();it!=subKeyNames.end();++it)
		{
			LogMessage(-1,L"Deleting registry key %s\\%s\\%s",rootName,keyName,it->c_str());
			error=RegDeleteTree2(hkey,it->c_str());
			if (error!=ERROR_SUCCESS)
				LogMessage(error,L"Failed to delete registry key %s\\%s\\%s.",rootName,keyName,it->c_str());
		}
		RegCloseKey(hkey);
	}
	else if (error!=ERROR_FILE_NOT_FOUND)
		LogMessage(error,L"Failed to open registry key %s\\%s for writing.",rootName,keyName);
}

static void DeleteSettings( HKEY root, const wchar_t *rootName, const wchar_t *keyName )
{
	HKEY hkey=NULL;
	int error=RegOpenKeyEx(root,keyName,0,KEY_WRITE|DELETE|KEY_WOW64_64KEY,&hkey);
	if (error==ERROR_SUCCESS)
	{
		error=RegDeleteTree2(hkey,L"ClassicExplorer");
		if (error==ERROR_SUCCESS)
			LogMessage(-1,L"Deleting registry key %s\\%s\\ClassicExplorer",rootName,keyName);
		else if (error!=ERROR_FILE_NOT_FOUND)
			LogMessage(error,L"Failed to delete registry key %s\\%s\\ClassicExplorer.",rootName,keyName);
		error=RegDeleteTree2(hkey,L"ClassicIE");
		if (error==ERROR_SUCCESS)
			LogMessage(-1,L"Deleting registry key %s\\%s\\ClassicIE",rootName,keyName);
		else if (error!=ERROR_FILE_NOT_FOUND)
			LogMessage(error,L"Failed to delete registry key %s\\%s\\ClassicIE.",rootName,keyName);
		error=RegDeleteTree2(hkey,L"OpenShell");
		if (error==ERROR_SUCCESS)
			LogMessage(-1,L"Deleting registry key %s\\%s\\OpenShell",rootName,keyName);
		else if (error!=ERROR_FILE_NOT_FOUND)
			LogMessage(error,L"Failed to delete registry key %s\\%s\\OpenShell.",rootName,keyName);
		error=RegDeleteTree2(hkey,L"StartMenu");
		if (error==ERROR_SUCCESS)
			LogMessage(-1,L"Deleting registry key %s\\%s\\StartMenu",rootName,keyName);
		else if (error!=ERROR_FILE_NOT_FOUND)
			LogMessage(error,L"Failed to delete registry key %s\\%s\\StartMenu.",rootName,keyName);
		RegCloseKey(hkey);

		HKEY root2;
		error=RegOpenKeyEx(root,NULL,0,KEY_WRITE|DELETE|KEY_WOW64_64KEY,&root2);
		if (error==ERROR_SUCCESS)
		{
			error=RegDeleteKey(root2,keyName);
			if (error==ERROR_SUCCESS)
				LogMessage(-1,L"Deleting registry key %s\\%s",rootName,keyName);
			else if (error!=ERROR_FILE_NOT_FOUND)
				LogMessage(error,L"Failed to delete registry key %s\\%s.",rootName,keyName);
			RegCloseKey(root2);
		}
		else
			LogMessage(error,L"Failed to open registry key %s for writing.",rootName);
	}
	else if (error!=ERROR_FILE_NOT_FOUND)
		LogMessage(error,L"Failed to open registry key %s\\%s for writing.",rootName,keyName);
}

// returns true if a file was delay-deleted
static bool DeleteFileEx( const wchar_t *fname, bool bAllowDelay )
{
	DWORD attribs=GetFileAttributes(fname);
	if (attribs!=INVALID_FILE_ATTRIBUTES)
	{
		if (attribs&FILE_ATTRIBUTE_READONLY)
			SetFileAttributes(fname,attribs&~FILE_ATTRIBUTE_READONLY);
		LogMessage(-1,L"Deleting file %s",fname);
		if (!DeleteFile2(fname))
		{
			int error=GetLastError();
			if (error==ERROR_ACCESS_DENIED && bAllowDelay)
			{
				LogMessage(-1,L"Delay deleting file %s",fname);
				wchar_t fname2[_MAX_PATH];
				Sprintf(fname2,_countof(fname2),L"%s_",fname);
				const wchar_t *delName=fname;
				if (MoveFileEx2(fname,fname2,0))
				{
					delName=fname2;
				}
				if (MoveFileEx2(delName,NULL,MOVEFILE_DELAY_UNTIL_REBOOT))
				{
					g_bRebootRequired=true;
					return true;
				}
				else
				{
					int error=GetLastError();
					LogMessage(error,L"Failed to delete file %s.",fname);
				}
			}
			else
				LogMessage(error,L"Failed to delete file %s.",fname);
		}
	}
	return false;
}

static void DeleteDirectoryEx( const wchar_t *path, bool bDelay )
{
	if (!DirectoryExists(path))
		return;
	if (bDelay)
	{
		LogMessage(-1,L"Delay deleting directory %s",path);
		if (MoveFileEx2(path,NULL,MOVEFILE_DELAY_UNTIL_REBOOT))
			g_bRebootRequired=true;
		else
		{
			int error=GetLastError();
			LogMessage(error,L"Failed to delete directory %s.",path);
		}
	}
	else
	{
		LogMessage(-1,L"Deleting directory %s",path);
		if (!RemoveDirectory2(path))
		{
			int error=GetLastError();
			bool bOldHasErrors=g_bHasErrors;
			LogMessage(error,L"Failed to delete directory %s.",path);
			if (error==ERROR_DIR_NOT_EMPTY)
			{
				// failing to delete non-empty directory is not a fatal error
				g_bHasErrors=bOldHasErrors;
			}
		}
	}
}

// returns true if any files were delay-deleted
static bool DeleteFiles( const wchar_t *directory, const wchar_t **files, int fileCount, bool bAllowDelay )
{
	bool res=false;
	if (DirectoryExists(directory))
	{
		for (int i=0;i<fileCount;i++)
		{
			wchar_t path[_MAX_PATH];
			Sprintf(path,_countof(path),L"%s\\%s",directory,files[i]);
			if (DeleteFileEx(path,bAllowDelay))
				res=true;
		}
	}
	return res;
}

// returns true if any files were delay-deleted
static bool DeleteFiles( const wchar_t *directory, const wchar_t *wildcard, bool bAllowDelay )
{
	bool res=false;
	wchar_t find[_MAX_PATH];
	Sprintf(find,_countof(find),L"%s\\%s",directory,wildcard);
	WIN32_FIND_DATA data;
	HANDLE h=FindFirstFile(find,&data);
	while (h!=INVALID_HANDLE_VALUE)
	{
		if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
		{
			wchar_t path[_MAX_PATH];
			Sprintf(path,_countof(path),L"%s\\%s",directory,data.cFileName);
			if (DeleteFileEx(path,bAllowDelay))
				res=true;
		}
		if (!FindNextFile(h,&data))
		{
			FindClose(h);
			break;
		}
	}
	return res;
}

static void DeleteUserFiles( const wchar_t *local, const wchar_t *roaming )
{
	if (DirectoryExists(local))
	{
		bool bDelay=DeleteFiles(local,g_LocalFiles,_countof(g_LocalFiles),true);
		DeleteDirectoryEx(local,bDelay);
	}

	if (DirectoryExists(roaming))
	{
		wchar_t pinned[_MAX_PATH];
		Sprintf(pinned,_countof(pinned),L"%s\\Pinned",roaming);
		DeleteFiles(pinned,L"*.*",false);
		DeleteDirectoryEx(pinned,false);
		DeleteDirectoryEx(roaming,false);
	}
}

static void ManualUninstallInternal( void )
{
	CUninstallDlg dlg;
	if (!dlg.DoModal(NULL))
		return;

	CProgressDlg progress;
	progress.Create(NULL);
	progress.GetDlgItem(IDC_PROGRESS1).SendMessage(PBM_SETRANGE,0,MAKELPARAM(0,100));
	progress.SetWindowPos(HWND_TOP,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
	{
		// stop start menu
		HWND hwnd=FindWindow(L"OpenShellMenu.CStartHookWindow",L"StartHookWindow");
		if (hwnd) PostMessage(hwnd,WM_USER+10,MSG_EXIT,0);

		int time=GetTickCount();

		wchar_t path[_MAX_PATH];
		Sprintf(path,_countof(path),L"%s\\StartMenu.exe",dlg.GetPath());
		int count=FileExists(path)?100:20;

		for (int i=0;i<count;i++)
		{
			int pos=i*120/count;
			if (pos>100) pos=100;
			progress.GetDlgItem(IDC_PROGRESS1).SendMessage(PBM_SETPOS,pos);
			MSG msg;
			while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			int dt=time+i*50-GetTickCount();
			if (dt>0) Sleep(dt);
		}
	}

	progress.GetDlgItem(IDC_PROGRESS1).SendMessage(PBM_SETPOS,100);

	AdjustPrivileges();

	LogMessage(-1,L"Open-Shell Utility - removing Open-Shell from the system\r\n\r\n");

	// remove TreatAs, app launcher keys
	WORD winVer=HIWORD(GetVersionEx(GetModuleHandle(L"user32.dll")));
	if (winVer>=WIN_VER_WIN8)
	{
		if (!RemoveRegistryKeys(winVer>=WIN_VER_WIN10))
		{
			progress.DestroyWindow();
			return;
		}
	}

	BOOL bWow64=FALSE;
	bool bIsWow64=IsWow64Process(GetCurrentProcess(),&bWow64) && bWow64;

	DeleteRegKeyHKCR(L"AppID\\ClassicExplorer.DLL");
	DeleteRegKeyHKCR(L"AppID\\ClassicIE.DLL");
	DeleteRegKeyHKCR(L"AppID\\StartMenuHelper.DLL");
	DeleteRegKeyHKCR(L"AppID\\{62D2FBE4-89F7-48A5-A35F-DA2B8A3C54B7}");
	DeleteRegKeyHKCR(L"AppID\\{65843E27-A491-429F-84A0-30A947E20F92}");
	DeleteRegKeyHKCR(L"AppID\\{DF3255F4-FF55-44FA-A728-E77B83E9E403}");

	DeleteRegKeyHKCR(L"Directory\\shellex\\DragDropHandlers\\ClassicCopyExt");
	DeleteRegKeyHKCR(L"Drive\\shellex\\DragDropHandlers\\ClassicCopyExt");
	DeleteRegKeyHKCR(L"exefile\\shellex\\ContextMenuHandlers\\StartMenuExt");
	DeleteRegKeyHKCR(L"Folder\\shellex\\ContextMenuHandlers\\StartMenuExt");
	DeleteRegKeyHKCR(L"Folder\\shellex\\DragDropHandlers\\ClassicCopyExt");
	DeleteRegKeyHKCR(L"lnkfile\\shellex\\ContextMenuHandlers\\StartMenuExt");
	DeleteRegKeyHKCR(L"ClassicExplorer.ClassicCopyExt");
	DeleteRegKeyHKCR(L"ClassicExplorer.ClassicCopyExt.1");
	DeleteRegKeyHKCR(L"ClassicExplorer.ExplorerBHO");
	DeleteRegKeyHKCR(L"ClassicExplorer.ExplorerBHO.1");
	DeleteRegKeyHKCR(L"ClassicExplorer.ExplorerBand");
	DeleteRegKeyHKCR(L"ClassicExplorer.ExplorerBand.1");
	DeleteRegKeyHKCR(L"ClassicExplorer.ShareOverlay");
	DeleteRegKeyHKCR(L"ClassicExplorer.ShareOverlay.1");
	DeleteRegKeyHKCR(L"ClassicIE.ClassicIEBHO");
	DeleteRegKeyHKCR(L"ClassicIE.ClassicIEBHO.1");
	DeleteRegKeyHKCR(L"StartMenuHelper.StartMenuExt");
	DeleteRegKeyHKCR(L"StartMenuHelper.StartMenuExt.1");
	DeleteRegKeyHKCR(L"TypeLib\\{BF8D124A-A4E0-402F-8152-4EF377E62586}");
	DeleteRegKeyHKCR(L"TypeLib\\{FDA50A1E-B8CE-49DE-8D17-B034A84AA280}");
	DeleteRegKeyHKCR(L"Interface\\{2576496C-B58A-4995-8878-8B68F9E8D1FC}");
	DeleteRegKeyHKCR(L"Interface\\{6E00B97F-A4D4-4062-98E4-4F66FC96F32F}");
	DeleteRegKeyHKCR(L"Interface\\{A1678625-A011-4B7C-A1FA-D691E4CDDB79}");
	DeleteRegKeyHKCR(L"Interface\\{BC4C1B8F-0BDE-4E42-9583-E072B2A28E0D}");
	DeleteRegKeyHKCR(L"Interface\\{C698A81E-5D02-42B1-9801-5381CA8BBC2F}");

	DeleteRegKeyCLSID(L"{449D0D6E-2412-4E61-B68F-1CB625CD9E52}",bIsWow64);
	DeleteRegKeyCLSID(L"{553891B7-A0D5-4526-BE18-D3CE461D6310}",bIsWow64);
	DeleteRegKeyCLSID(L"{594D4122-1F87-41E2-96C7-825FB4796516}",bIsWow64);
	DeleteRegKeyCLSID(L"{8C83ACB1-75C3-45D2-882C-EFA32333491C}",bIsWow64);
	DeleteRegKeyCLSID(L"{D3214FBB-3CA1-406A-B3E8-3EB7C393A15E}",bIsWow64);
	DeleteRegKeyCLSID(L"{E595F05F-903F-4318-8B0A-7F633B520D2B}",bIsWow64);
	DeleteRegKeyCLSID(L"{82E749ED-B971-4550-BAF7-06AA2BF7E836}",bIsWow64);
	DeleteRegKeyCLSID(L"{5AB14324-C087-42C1-B905-A0BFDB4E9532}",bIsWow64);
	DeleteRegKeyCLSID(L"{E407B70A-1FBD-4D5E-8822-231C69102472}",bIsWow64);
	DeleteRegKeyCLSID(L"{EA801577-E6AD-4BD5-8F71-4BE0154331A4}",bIsWow64);

	DeleteRegKeySOFTWARE(L"Microsoft\\Internet Explorer\\Extensions\\{56753E59-AF1D-4FBA-9E15-31557124ADA2}",bIsWow64);
	DeleteRegKeySOFTWARE(L"Microsoft\\Internet Explorer\\Low Rights\\ElevationPolicy\\{02E6771D-8375-42B9-9F83-B4730F697900}",bIsWow64);
	DeleteRegKeySOFTWARE(L"Microsoft\\Internet Explorer\\Low Rights\\ElevationPolicy\\{56753E59-AF1D-4FBA-9E15-31557124ADA2}",bIsWow64);
	DeleteRegKeySOFTWARE(L"Microsoft\\Internet Explorer\\Low Rights\\ElevationPolicy\\{C0393554-9B48-458A-B91B-3F684D003B2F}",bIsWow64);
	DeleteRegKeySOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects\\{449D0D6E-2412-4E61-B68F-1CB625CD9E52}",bIsWow64);
	DeleteRegKeySOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects\\{EA801577-E6AD-4BD5-8F71-4BE0154331A4}",bIsWow64);
	DeleteRegKeySOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\ShareOverlay",bIsWow64);
	DeleteRegKeySOFTWARE(L"OpenShell\\OpenShell",bIsWow64);

	DeleteRegValueSOFTWARE(L"Microsoft\\Internet Explorer\\Toolbar",L"{553891B7-A0D5-4526-BE18-D3CE461D6310}",bIsWow64);
	DeleteRegValueSOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Policies\\Ext\\CLSID",L"{449D0D6E-2412-4E61-B68F-1CB625CD9E52}",bIsWow64);
	DeleteRegValueSOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Policies\\Ext\\CLSID",L"{553891B7-A0D5-4526-BE18-D3CE461D6310}",bIsWow64);
	DeleteRegValueSOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Policies\\Ext\\CLSID",L"{EA801577-E6AD-4BD5-8F71-4BE0154331A4}",bIsWow64);
	DeleteRegValueSOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Run",L"Open-Shell Menu",bIsWow64);
	DeleteRegValueSOFTWARE(L"Microsoft\\Windows\\CurrentVersion\\Run",L"Open-Shell Start Menu",bIsWow64);

	DeleteInstallerKey(HKEY_CLASSES_ROOT,L"HKEY_CLASSES_ROOT",L"Installer\\Features",L"OpenShell",L"");
	DeleteInstallerKey(HKEY_CLASSES_ROOT,L"HKEY_CLASSES_ROOT",L"Installer\\Products",L"ProductName",L"Open-Shell");
	DeleteInstallerKey(HKEY_LOCAL_MACHINE,L"HKEY_LOCAL_MACHINE",L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",L"DisplayName",L"Open-Shell");

	if (dlg.GetRemoveUserSettings() || dlg.GetRemoveAllUserSettings())
	{
		// delete settings for the current user
		LogMessage(-1,L"Deleting user settings from HKEY_CURRENT_USER\\Software\\OpenShell");
		DeleteSettings(HKEY_CURRENT_USER,L"HKEY_CURRENT_USER",L"Software\\OpenShell");

		wchar_t local[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell";
		DoEnvironmentSubst(local,_countof(local));
		wchar_t roaming[_MAX_PATH]=L"%APPDATA%\\OpenShell";
		DoEnvironmentSubst(roaming,_countof(roaming));
		DeleteUserFiles(local,roaming);
	}

	if (dlg.GetRemoveAllUserSettings())
	{
		// delete settings for all users
		std::vector<CString> userNames;
		wchar_t usersFolder[_MAX_PATH];
		{
			CComString pUsersFolder;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_UserProfiles,0,NULL,&pUsersFolder)))
				Strcpy(usersFolder,_countof(usersFolder),pUsersFolder);
			else
			{
				Strcpy(usersFolder,_countof(usersFolder),L"%systemdrive%\\Users");
				DoEnvironmentSubst(usersFolder,_countof(usersFolder));
			}
		}
		{
			wchar_t userName[256]=L"%USERNAME%";
			DoEnvironmentSubst(userName,_countof(userName));
			USER_INFO_1 *users;
			DWORD userCount, userCount2;
			if (NetUserEnum(NULL,1,FILTER_NORMAL_ACCOUNT,(BYTE**)&users,MAX_PREFERRED_LENGTH,&userCount,&userCount2,NULL)==NERR_Success)
			{
				for (DWORD userIdx=0;userIdx<userCount2;userIdx++)
				{
					if ((users[userIdx].usri1_flags&UF_NORMAL_ACCOUNT) && _wcsicmp(userName,users[userIdx].usri1_name)!=0)
						userNames.push_back(users[userIdx].usri1_name);
				}
				NetApiBufferFree(users);
			}
		}

		for (std::vector<CString>::const_iterator it=userNames.begin();it!=userNames.end();++it)
		{
			LogMessage(-1,L"Deleting settings for user %s",*it);

			wchar_t local[_MAX_PATH];
			Sprintf(local,_countof(local),L"%s\\%s\\AppData\\Local\\OpenShell",usersFolder,*it);
			wchar_t roaming[_MAX_PATH];
			Sprintf(roaming,_countof(roaming),L"%s\\%s\\AppData\\Roaming\\OpenShell",usersFolder,*it);
			DeleteUserFiles(local,roaming);

			wchar_t regFile[_MAX_PATH];
			Sprintf(regFile,_countof(regFile),L"%s\\%s\\NTUSER.DAT",usersFolder,*it);
			if (GetFileAttributes(regFile)==INVALID_FILE_ATTRIBUTES)
			{
				int error=GetLastError();
				if (error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)
					LogMessage(error,L"Failed to load registry hive %s.",regFile);
				continue;
			}
			int error=RegLoadKey(HKEY_USERS,L"CSUTempUser",regFile);
			if (error==ERROR_SUCCESS)
			{
				DeleteSettings(HKEY_USERS,L"HKEY_USERS",L"CSUTempUser\\Software\\OpenShell");
				RegUnLoadKey(HKEY_USERS,L"CSUTempUser");
			}
			else if (error!=ERROR_FILE_NOT_FOUND)
				LogMessage(error,L"Failed to load registry hive %s.",regFile);
		}
	}

	if (dlg.GetRemoveAdminSettings())
	{
		// delete admin settings
		LogMessage(-1,L"Deleting settings from HKEY_LOCAL_MACHINE\\Software\\OpenShell");
		DeleteSettings(HKEY_LOCAL_MACHINE,L"HKEY_LOCAL_MACHINE",L"Software\\OpenShell");
	}

	// delete program data
	{
		wchar_t dataPath[_MAX_PATH]=L"%ALLUSERSPROFILE%\\OpenShell";
		DoEnvironmentSubst(dataPath,_countof(dataPath));
		wchar_t langPath[_MAX_PATH];
		Sprintf(langPath,_countof(langPath),L"%s\\Languages",dataPath);
		DeleteFiles(langPath,L"*.dll",false);
		DeleteDirectoryEx(langPath,false);
		DeleteFiles(dataPath,g_ProgramDataFiles,_countof(g_ProgramDataFiles),false);
		DeleteDirectoryEx(dataPath,false);
	}

	// delete installation folder
	CString installPath=dlg.GetPath();
	if (DirectoryExists(installPath) && !PathIsRelative(installPath))
	{
		wchar_t skinPath[_MAX_PATH];
		Sprintf(skinPath,_countof(skinPath),L"%s\\Skins",installPath);
		bool bDelay=DeleteFiles(skinPath,g_InstalledSkins,_countof(g_InstalledSkins),true);
		if (DirectoryExists(skinPath))
			DeleteDirectoryEx(skinPath,bDelay);
		if (DeleteFiles(installPath,g_InstalledFiles,_countof(g_InstalledFiles),true))
			bDelay=true;
		DeleteDirectoryEx(installPath,bDelay);
	}

	// delete start menu shortcuts
	{
		CComString pPath;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms,0,NULL,&pPath)))
		{
			wchar_t startMenuPath[_MAX_PATH];
			Sprintf(startMenuPath,_countof(startMenuPath),L"%s\\Open-Shell",(const wchar_t*)pPath);
			if (DirectoryExists(startMenuPath))
			{
				for (int i=0;i<_countof(g_InstalledShortcuts);i++)
				{
					wchar_t path[_MAX_PATH];
					Sprintf(path,_countof(path),L"%s\\%s",startMenuPath,g_InstalledShortcuts[i]);
					DeleteFileEx(path,false);
				}
				DeleteDirectoryEx(startMenuPath,false);
			}
		}
	}

	// delete start menu helpers
	wchar_t winDir[_MAX_PATH]=L"%WinDir%";
	DoEnvironmentSubst(winDir,_countof(winDir));
	if (bIsWow64)
	{
		// 64-bit OS
		wchar_t path[_MAX_PATH];
		Sprintf(path,_countof(path),L"%s\\System32\\StartMenuHelper64.dll",winDir);
		Wow64EnableWow64FsRedirection(FALSE);
		DeleteFileEx(path,true);
		Sprintf(path,_countof(path),L"%s\\SysWOW64\\StartMenuHelper32.dll",winDir);
		DeleteFileEx(path,true);
		Wow64EnableWow64FsRedirection(TRUE);
	}
	else
	{
		// 32-bit OS
		wchar_t path[_MAX_PATH];
		Sprintf(path,_countof(path),L"%s\\System32\\StartMenuHelper32.dll",winDir);
		DeleteFileEx(path,true);
	}

///////////////////////////////////////////////////////////////////////////////

	SaveReportFile();
	g_Report.push_back(0);

	progress.DestroyWindow();

	if (CResultsDlg(g_bHasErrors,g_bRebootRequired,&g_Report[0]).DoModal(NULL)==1)
	{
		// restart was requested
		HANDLE hToken;
		if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken))
		{
			TOKEN_PRIVILEGES tp={1};
			if (LookupPrivilegeValue(NULL,L"SeShutdownPrivilege",&tp.Privileges[0].Luid))
				tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
			CloseHandle(hToken);
		}
#ifndef SAFE_MODE
		ExitWindowsEx(EWX_REBOOT,SHTDN_REASON_FLAG_PLANNED);
#endif
	}
	else if (g_bHasErrors || g_bRebootRequired)
	{
		MessageBox(NULL,L"It is important that you restart as soon as possible. If you attempt to install another software before then it may get corrupted.",L"Open-Shell Uninstaller",MB_OK|MB_ICONWARNING);
	}
}

void ManualUninstall( void )
{
	CoInitialize(NULL);
	ManualUninstallInternal();
	CoUninitialize();
}

#endif
