// Open-Shell: directory notifications for an open menu; no file-operation worker.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <vector>

class CMenuFolderWatch
{
public:
	CMenuFolderWatch( void ) = default;
	CMenuFolderWatch( const CMenuFolderWatch& ) = delete;
	CMenuFolderWatch& operator=( const CMenuFolderWatch& ) = delete;
	~CMenuFolderWatch( void ) { Clear(); }
	bool Add( HWND window, UINT message, PCIDLIST_ABSOLUTE folder, bool recursive=false )
	{
		if (!folder) return false;
		SHChangeNotifyEntry entry={folder,recursive?TRUE:FALSE};
		ULONG cookie=SHChangeNotifyRegister(window,
			SHCNRF_ShellLevel|SHCNRF_InterruptLevel|SHCNRF_NewDelivery|
			(recursive?SHCNRF_RecursiveInterrupt:0),
			SHCNE_RENAMEITEM|SHCNE_CREATE|SHCNE_DELETE|SHCNE_MKDIR|SHCNE_RMDIR|
			SHCNE_UPDATEITEM|SHCNE_UPDATEDIR|SHCNE_RENAMEFOLDER|SHCNE_ATTRIBUTES,
			message,1,&entry);
		if (cookie) m_Cookies.push_back(cookie);
		return cookie!=0;
	}
	void Clear( void )
	{
		for (ULONG cookie : m_Cookies) SHChangeNotifyDeregister(cookie);
		m_Cookies.clear();
	}
	static bool Contains( PCIDLIST_ABSOLUTE folder, PCIDLIST_ABSOLUTE changed, bool recursive=false )
	{
		return folder && changed && (ILIsEqual(folder,changed) || ILIsParent(folder,changed,recursive?FALSE:TRUE));
	}
private:
	std::vector<ULONG> m_Cookies;
};
