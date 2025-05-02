// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include <stdafx.h>
#include "resource.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "ResourceHelper.h"
#include "ComHelper.h"
#include "FNVHash.h"
#include "StringUtils.h"
#include "Translations.h"
#include "DownloadHelper.h"
#include <uxtheme.h>
#include <map>
#include <algorithm>

const KNOWNFOLDERID FOLDERID_DesktopRoot={'DESK', 'TO', 'P', {'D', 'E', 'S', 'K', 'T', 'O', 'P', 0x00}};

static bool IsVariantTrue( const CComVariant &var )
{
	return (var.vt==VT_I4 && var.intVal==1);
}

///////////////////////////////////////////////////////////////////////////////

void CTreeItem::SetCommand( CString command, const CStdCommand *pStdCommands, int style, int mask )
{
	this->command=command;
	pStdCommand=NULL;
	for (int i=0;pStdCommands[i].name;i++)
	{
		if (!pStdCommands[i].IsStyle(style,mask)) continue;
		if (_wcsicmp(pStdCommands[i].name,command)==0)
		{
			pStdCommand=&pStdCommands[i];
			return;
		}
		else if (pStdCommands[i].IsCustom())
		{
			pStdCommand=&pStdCommands[i];
		}
	}
}

unsigned int CTreeItem::GetIconKey( void ) const
{
	if (!icon.IsEmpty())
		return CalcFNVHash(icon);
	else if (pStdCommand && pStdCommand->knownFolder)
		return CalcFNVHash(pStdCommand->knownFolder,sizeof(GUID));
	else if (!link.IsEmpty())
		return CalcFNVHash(link);
	return 0;
}

HICON CTreeItem::LoadIcon( bool bSmall, std::vector<HMODULE> &modules ) const
{
	if (!icon.IsEmpty())
	{
		if (_wcsicmp(icon,L"none")==0)
			return bSmall?HICON_NONE:NULL;
		return ::LoadIcon(GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON),icon,modules);
	}
	else
	{
		CAbsolutePidl pidl;
		if (pStdCommand && pStdCommand->knownFolder)
		{
			SHGetKnownFolderIDList(*pStdCommand->knownFolder,0,NULL,&pidl);
		}
		else if (!link.IsEmpty())
		{
			const wchar_t *c=wcschr(link,'|');
			if (c)
			{
				for (c++;*c==' ';)
					c++;
			}
			else
				c=link;
			wchar_t buf[_MAX_PATH];
			Strcpy(buf,_countof(buf),c);
			DoEnvironmentSubst(buf,_countof(buf));

			wchar_t *second=wcschr(buf,';');
			if (second)
			{
				wchar_t *end= second;
				while (end>buf)
				{
					*end=0;
					end--;
					if (*end!=' ')
						break;
				}
			}

			ShParseDisplayName(buf,&pidl,0,NULL);
		}
		if (pidl)
		{
			HICON hIcon=::LoadIcon(GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON),pidl);
			return hIcon;
		}
	}
	return NULL;
}

unsigned int CTreeItem::GetIconDKey( unsigned int iconKey ) const
{
	if (iconD.IsEmpty())
		return ~iconKey;
	return CalcFNVHash(iconD);
}

HICON CTreeItem::LoadIconD( HICON hIcon, std::vector<HMODULE> &modules ) const
{
	if (!iconD.IsEmpty())
	{
		if (_wcsicmp(iconD,L"none")==0)
			return NULL;
		return ::LoadIcon(GetSystemMetrics(SM_CXICON),iconD,modules);
	}
	else if (hIcon)
		return CreateDisabledIcon(hIcon,GetSystemMetrics(SM_CXICON));
	else
		return NULL;
}

CString CTreeItem::GetDisplayName( bool bTitle ) const
{
	if (pStdCommand->IsCustom())
	{
		if (!label.IsEmpty())
		{
			if (label[0]!='$')
				return label;
			const wchar_t *pLabel=label;
			return FindTranslation(pLabel+1,pLabel);
		}
		if (!bTitle && !link.IsEmpty())
		{
			const wchar_t *c=wcschr(link,'|');
			if (c)
			{
				for (c++;*c==' ';)
					c++;
			}
			else
				c=link;
			wchar_t buf[_MAX_PATH];
			Strcpy(buf,_countof(buf),c);
			DoEnvironmentSubst(buf,_countof(buf));

			wchar_t *second=wcschr(buf,';');
			if (second)
			{
				wchar_t *end= second;
				while (end>buf)
				{
					*end=0;
					end--;
					if (*end!=' ')
						break;
				}
			}

			CAbsolutePidl pidl;
			ShParseDisplayName(buf,&pidl,0,NULL);
			if (pidl)
			{
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)))
				{
					CComString pName;
					if (SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
					{
						CString name;
						name.Format(L"%s (%s)",(const wchar_t*)LoadStringEx(pStdCommand->displayNameId),(const wchar_t*)pName);
						return name;
					}
				}
			}
		}
		if (!bTitle && !tip.IsEmpty())
		{
			const wchar_t *pTip=tip;
			CString name;
			if (pTip[0]=='$')
				name.Format(L"%s (%s)",LoadStringEx(pStdCommand->displayNameId),FindTranslation(pTip+1,pTip));
			else
				name.Format(L"%s (%s)",LoadStringEx(pStdCommand->displayNameId),pTip);
			return name;
		}
	}
	return LoadStringEx(pStdCommand->displayNameId);
}

// Subclass the renaming editbox to handle Esc and Enter
static LRESULT CALLBACK SubclassEditboxProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_GETDLGCODE)
		return DLGC_WANTALLKEYS;
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

class CSettingsTree: public CWindowImpl<CSettingsTree, CWindow>
{
public:
	BEGIN_MSG_MAP( CSettingsTree )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_MOUSEMOVE, OnMouseMove )
		MESSAGE_HANDLER( WM_LBUTTONUP, OnLButtonUp )
		MESSAGE_HANDLER( WM_RBUTTONDOWN, OnRButtonDown )
		MESSAGE_HANDLER( WM_KEYUP, OnKeyUp )
		MESSAGE_HANDLER( WM_GETDLGCODE, OnGetDlgCode )
		MESSAGE_HANDLER( WM_CHAR, OnChar )
		MESSAGE_HANDLER( WM_CAPTURECHANGED, OnCaptureChanged )
		MESSAGE_HANDLER( WM_TIMER, OnTimer )
		REFLECTED_NOTIFY_CODE_HANDLER( TVN_DELETEITEM, OnDeleteItem )
		REFLECTED_NOTIFY_CODE_HANDLER( TVN_BEGINDRAG, OnBeginDrag )
		REFLECTED_NOTIFY_CODE_HANDLER( TVN_BEGINLABELEDIT, OnBeginLabelEdit )
		REFLECTED_NOTIFY_CODE_HANDLER( TVN_ENDLABELEDIT, OnEndLabelEdit )
		REFLECTED_NOTIFY_CODE_HANDLER( TVN_KEYDOWN, OnKeyDown )
	END_MSG_MAP()

	CSettingsTree( int mask ) { m_ImageList=NULL; m_StyleMask=mask; }
	void SubclassWindow( HWND hWnd, CCustomTreeDlg *pOwner );
	int LoadIcon( const CTreeItem *pItem );
	int UpdateItem( CTreeItem *pItem, HTREEITEM hItem );
	void SetDisabled( bool bDisabled ) { m_bDisabled=bDisabled; }
	bool IsDragging( void ) const { return m_DragMode!=DRAG_NONE; }

	void DeleteItem( HTREEITEM hItem );

	void BeginDrag( const CStdCommand *pCommand );
	HTREEITEM CreateStdItem( const CStdCommand *pCommand, HTREEITEM hParent, HTREEITEM hAfter );

	std::vector<HMODULE> m_Modules;

protected:
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnKeyUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnGetDlgCode( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnCaptureChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDeleteItem( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnBeginDrag( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnBeginLabelEdit( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnEndLabelEdit( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnKeyDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

private:
	CCustomTreeDlg *m_pOwner;
	std::map<unsigned int,int> m_IconCache;
	HIMAGELIST m_ImageList;
	bool m_bDisabled;

	enum TDragMode
	{
		DRAG_NONE,
		DRAG_DELETE,
		DRAG_ERROR,
		DRAG_MOVE, // these must be the last two
		DRAG_COPY,
	};

	enum TDropLocation
	{
		DROP_NOWHERE,
		DROP_INSIDE,
		DROP_BEFORE,
		DROP_AFTER,
		DROP_LAST,
	};

	enum {
		TIMER_SCROLL=1,
		TIMER_HOVER=2,
	};

	int m_StyleMask;
	TDragMode m_DragMode;
	HCURSOR m_CursorError, m_CursorMove, m_CursorCopy, m_CursorDel;
	HTREEITEM m_DragItem;
	HTREEITEM m_DropTarget;
	HTREEITEM m_DropSubmenu;
	TDropLocation m_DropLocation;
	int m_Scroll;
	HTREEITEM m_HoverItem;
	const CStdCommand *m_pStdDragItem;
	bool m_bDragLink; // the item being dragged is a link

	bool FindItemByName( HTREEITEM hParent, const wchar_t *name, HTREEITEM hExclude );
	CString CreateUniqueName( CString oldName );
	HTREEITEM CreateDeepCopy( HTREEITEM hSource, HTREEITEM hParent, HTREEITEM hAfter, bool bRename );
};

void CSettingsTree::SubclassWindow( HWND hWnd, CCustomTreeDlg *pOwner )
{
	m_pOwner=pOwner;
	int size=GetSystemMetrics(SM_CXSMICON);
	if (!m_ImageList)
		m_ImageList=ImageList_Create(size,size,ILC_COLOR32|ILC_MASK|ILC_MIRROR,1,16);
	CWindowImpl<CSettingsTree, CWindow>::SubclassWindow(hWnd);
	TreeView_SetImageList(hWnd,m_ImageList,TVSIL_NORMAL);

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=size;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	RECT rc={0,0,size,size};

	HDC hdc=CreateCompatibleDC(NULL);
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,NULL,NULL,0);
	HGDIOBJ bmp0=SelectObject(hdc,bmp);
	FillRect(hdc,&rc,(HBRUSH)(COLOR_WINDOW+1));
	SelectObject(hdc,bmp0);
	DeleteDC(hdc);
	ImageList_AddMasked(m_ImageList,bmp,GetSysColor(COLOR_WINDOW));
	DeleteObject(bmp);

	SHFILEINFO info;
	if (SHGetFileInfo(L"file",FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_SMALLICON))
	{
		ImageList_AddIcon(m_ImageList,info.hIcon);
		DestroyIcon(info.hIcon);
	}
	m_DragMode=DRAG_NONE;
	HMODULE hOle32=LoadLibrary(L"ole32.dll");
	if (hOle32)
	{
		m_CursorError=(HCURSOR)LoadImage(hOle32,MAKEINTRESOURCE(1),IMAGE_CURSOR,0,0,LR_DEFAULTSIZE);
		m_CursorMove=(HCURSOR)LoadImage(hOle32,MAKEINTRESOURCE(2),IMAGE_CURSOR,0,0,LR_DEFAULTSIZE);
		m_CursorCopy=(HCURSOR)LoadImage(hOle32,MAKEINTRESOURCE(3),IMAGE_CURSOR,0,0,LR_DEFAULTSIZE);
		m_CursorDel=(HCURSOR)LoadImage(hOle32,MAKEINTRESOURCE(5),IMAGE_CURSOR,0,0,LR_DEFAULTSIZE);
		FreeLibrary(hOle32);
	}
}

int CSettingsTree::LoadIcon( const CTreeItem *pItem )
{
	unsigned int key=pItem->GetIconKey();
	std::map<unsigned int,int>::const_iterator it=m_IconCache.find(key);
	if (it!=m_IconCache.end())
		return it->second;

	// extract icon
	HICON hIcon=pItem->LoadIcon(true,m_Modules);
	if (hIcon==HICON_NONE)
	{
		m_IconCache[key]=0;
		return 0;
	}
	int index=1;
	if (hIcon)
	{
		index=ImageList_AddIcon(m_ImageList,hIcon);
		DestroyIcon(hIcon);
	}
	m_IconCache[key]=index;
	return index;
}

int CSettingsTree::UpdateItem( CTreeItem *pItem, HTREEITEM hItem )
{
	TVITEM item={TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_TEXT,hItem};

	item.iImage=item.iSelectedImage=LoadIcon(pItem);
	if (hItem)
	{
		CString dispName=pItem->GetDisplayName(false);
		item.pszText=(LPWSTR)(LPCWSTR)dispName;
		TreeView_SetItem(m_hWnd,&item);
	}
	return item.iImage;
}

LRESULT CSettingsTree::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	for (std::vector<HMODULE>::const_iterator it=m_Modules.begin();it!=m_Modules.end();++it)
		FreeLibrary(*it);
	m_Modules.clear();
	if (m_CursorError) DestroyCursor(m_CursorError);
	if (m_CursorMove) DestroyCursor(m_CursorMove);
	if (m_CursorCopy) DestroyCursor(m_CursorCopy);
	if (m_CursorDel) DestroyCursor(m_CursorDel);
	return 0;
}

LRESULT CSettingsTree::OnDeleteItem( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	// free data
	NMTREEVIEW *pItem=(NMTREEVIEW*)pnmh;
	delete (CTreeItem*)pItem->itemOld.lParam;
	return 0;
}

void CSettingsTree::BeginDrag( const CStdCommand *pCommand )
{
	if (m_bDisabled) return;
	m_pStdDragItem=pCommand;
	m_bDragLink=pCommand->knownFolder!=NULL;
	m_DragItem=TreeView_GetSelection(m_hWnd);
	SetFocus();
	SetCapture();
	m_DragMode=DRAG_COPY;
	m_DropTarget=NULL;
	m_DropLocation=DROP_NOWHERE;
	m_Scroll=0;
	m_HoverItem=NULL;
	m_DropSubmenu=NULL;
	SetCursor(m_CursorCopy);
	TreeView_SelectItem(m_hWnd,NULL);
}

LRESULT CSettingsTree::OnBeginDrag( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	if (m_bDisabled) return 0;
	NMTREEVIEW *pDrag=(NMTREEVIEW*)pnmh;
	CTreeItem *pItem=(CTreeItem*)pDrag->itemNew.lParam;
	if (!pItem) return 0;
	m_pStdDragItem=NULL;
	m_DragItem=pDrag->itemNew.hItem;
	m_bDragLink=(pItem->pStdCommand && pItem->pStdCommand->knownFolder) || !pItem->link.IsEmpty();
	TreeView_SelectItem(m_hWnd,m_DragItem);
	SetCapture();
	m_DragMode=GetKeyState(VK_CONTROL)<0?DRAG_COPY:DRAG_MOVE;
	m_DropTarget=NULL;
	m_DropLocation=DROP_NOWHERE;
	m_Scroll=0;
	m_HoverItem=NULL;
	m_DropSubmenu=NULL;
	SetCursor(m_DragMode==DRAG_COPY?m_CursorCopy:m_CursorMove);
	SetFocus();
	TreeView_SelectItem(m_hWnd,NULL);
	return 0;
}

LRESULT CSettingsTree::OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_DragMode==DRAG_NONE)
	{
		bHandled=FALSE;
		return 0;
	}
	// find the drop location
	m_DropTarget=NULL;
	m_DropLocation=DROP_NOWHERE;
	TVHITTESTINFO test={{(short)LOWORD(lParam),(short)HIWORD(lParam)}};
	RECT rc;
	GetClientRect(&rc);
	int h=TreeView_GetItemHeight(m_hWnd);
	int scroll=0;
	if (test.pt.y<h)
		scroll=-1;
	if (test.pt.y>rc.bottom-h)
		scroll=1;
	if (m_Scroll!=scroll)
	{
		m_Scroll=scroll;
		if (m_Scroll)
			SetTimer(TIMER_SCROLL,100);
		else
			KillTimer(TIMER_SCROLL);
	}
	bool bDelete=!PtInRect(&rc,test.pt);
	if (!bDelete)
	{
		HTREEITEM hLast=TreeView_GetLastVisible(m_hWnd);
		if (hLast)
		{
			TreeView_GetItemRect(m_hWnd,hLast,&rc,FALSE);
			if (TreeView_HitTest(m_hWnd,&test))
			{
				m_DropTarget=test.hItem;
			}
			else
			{
				if (test.pt.y>=rc.bottom)
				{
					// after the last visible item
					m_DropTarget=hLast;
				}
			}
		}
		else if (m_pStdDragItem)
			m_DropLocation=DROP_LAST;

		if (m_DropTarget && !m_pStdDragItem)
		{
			// can't drop inside itself
			for (HTREEITEM hItem=TreeView_GetParent(m_hWnd,m_DropTarget);hItem;hItem=TreeView_GetParent(m_hWnd,hItem))
			{
				if (hItem==m_DragItem)
				{
					m_DropTarget=NULL;
					break;
				}
			}
		}
		if (m_DropTarget)
		{
			TreeView_GetItemRect(m_hWnd,m_DropTarget,&rc,FALSE);
			TVITEM item={TVIF_PARAM,m_DropTarget};
			TreeView_GetItem(m_hWnd,&item);
			int dy=test.pt.y-rc.top;
			int h1=(rc.bottom-rc.top)/4;
			int h2=(rc.bottom-rc.top)-h1;
			if (!item.lParam || ((CTreeItem*)item.lParam)->pStdCommand->IsSeparator())
			{
				h1=(rc.bottom-rc.top)/2;
				h2=(rc.bottom-rc.top)-h1;
			}
			if (dy<h1)
				m_DropLocation=DROP_BEFORE;
			else if (dy>=h2)
				m_DropLocation=DROP_AFTER;
			else
			{
				// can't drop in a separator or itself
				if (!item.lParam || ((CTreeItem*)item.lParam)->pStdCommand->IsSeparator() || (m_DropTarget==m_DragItem && !m_pStdDragItem))
					m_DropTarget=NULL;
				else
					m_DropLocation=DROP_INSIDE;
			}
		}
	}
	// display drop location
	if (!m_DropTarget)
	{
		TreeView_SelectDropTarget(m_hWnd,NULL);
		TreeView_SetInsertMark(m_hWnd,NULL,FALSE);
		if (m_DropLocation==DROP_LAST)
		{
			m_DragMode=DRAG_COPY;
			SetCursor(m_CursorCopy);
		}
		else
		{
			if (m_pStdDragItem)
				bDelete=false;
			m_DragMode=bDelete?DRAG_DELETE:DRAG_ERROR;
			SetCursor(bDelete?m_CursorDel:m_CursorError);
		}
	}
	else
	{
		m_DragMode=(m_pStdDragItem || GetKeyState(VK_CONTROL)<0)?DRAG_COPY:DRAG_MOVE;
		SetCursor(m_DragMode==DRAG_COPY?m_CursorCopy:m_CursorMove);
		if (m_DropLocation==DROP_INSIDE)
		{
			TreeView_SelectDropTarget(m_hWnd,m_DropTarget);
			TreeView_SetInsertMark(m_hWnd,NULL,FALSE);
		}
		else
		{
			TreeView_SelectDropTarget(m_hWnd,NULL);
			TreeView_SetInsertMark(m_hWnd,m_DropTarget,m_DropLocation==DROP_AFTER);
		}
	}

	if (m_DropLocation==DROP_INSIDE)
	{
		if (m_HoverItem!=m_DropTarget)
		{
			SetTimer(TIMER_HOVER,400);
			m_HoverItem=m_DropTarget;
		}
	}
	else if (m_HoverItem)
	{
		m_HoverItem=NULL;
		KillTimer(TIMER_HOVER);
	}
	return 0;
}

bool CSettingsTree::FindItemByName( HTREEITEM hParent, const wchar_t *name, HTREEITEM hExclude )
{
	for (HTREEITEM hItem=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hItem;hItem=TreeView_GetNextSibling(m_hWnd,hItem))
	{
		if (FindItemByName(hItem,name,hExclude))
			return true;
		if (hItem!=hExclude)
		{
			TVITEM item={TVIF_PARAM,hItem};
			TreeView_GetItem(m_hWnd,&item);
			if (item.lParam && _wcsicmp(((CTreeItem*)item.lParam)->name,name)==0)
				return true;
		}
	}
	return false;
}

CString CSettingsTree::CreateUniqueName( CString oldName )
{
	if (!FindItemByName(NULL,oldName,NULL))
		return oldName;
	wchar_t name[256];
	Strcpy(name,_countof(name)-3,oldName);
	// find numeric part
	int pos=Strlen(name);
	while (pos>0 && name[pos-1]>='0' && name[pos-1]<='9')
		pos--;
	for (int i=2;;i++)
	{
		Sprintf(name+pos,_countof(name)-pos,L"%d",i);
		if (!FindItemByName(NULL,name,NULL))
			break;
	}
	return name;
}

HTREEITEM CSettingsTree::CreateDeepCopy( HTREEITEM hSource, HTREEITEM hParent, HTREEITEM hAfter, bool bRename )
{
	TVINSERTSTRUCT insert={hParent,hAfter?hAfter:TVI_FIRST,{TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_STATE,hSource,0,TVIS_EXPANDED}};
	TreeView_GetItem(m_hWnd,&insert.item);
	CTreeItem *pSource=(CTreeItem*)insert.item.lParam;
	if (!pSource) return NULL;
	CTreeItem *pNewItem=new CTreeItem(*pSource);
	if (bRename && !pNewItem->pStdCommand->IsSeparator())
	{
		pNewItem->name=CreateUniqueName(pNewItem->name);
	}
	insert.item.lParam=(LPARAM)pNewItem;
	insert.item.mask|=TVIF_TEXT;
	CString dispName=pNewItem->GetDisplayName(false);
	insert.item.pszText=(LPWSTR)(LPCWSTR)dispName;
	HTREEITEM hResult=TreeView_InsertItem(m_hWnd,&insert);
	for (HTREEITEM hChild=TreeView_GetChild(m_hWnd,hSource);hChild;hChild=TreeView_GetNextSibling(m_hWnd,hChild))
		CreateDeepCopy(hChild,hResult,TVI_LAST,bRename);
	return hResult;
}

HTREEITEM CSettingsTree::CreateStdItem( const CStdCommand *pCommand, HTREEITEM hParent, HTREEITEM hAfter )
{
	TVINSERTSTRUCT insert={hParent,hAfter?hAfter:TVI_FIRST,{TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_STATE|TVIF_TEXT,NULL,TVIS_EXPANDED,TVIS_EXPANDED}};
	CTreeItem *pNewItem=new CTreeItem;
	pNewItem->label=pCommand->label;
	pNewItem->tip=pCommand->tip;
	pNewItem->icon=pCommand->icon;
	pNewItem->iconD=pCommand->iconD;
	pNewItem->pStdCommand=pCommand;
	pNewItem->settings=pCommand->settings&~m_StyleMask;

	if (pCommand->IsSeparator())
	{
		pNewItem->name=pCommand->name;
	}
	else
	{
		pNewItem->command=pCommand->name;
		pNewItem->name=CreateUniqueName(pCommand->itemName);
		insert.item.iImage=insert.item.iSelectedImage=UpdateItem(pNewItem,NULL);
	}

	insert.item.lParam=(LPARAM)pNewItem;
	CString dispName=pNewItem->GetDisplayName(false);
	insert.item.pszText=(LPWSTR)(LPCWSTR)dispName;
	return TreeView_InsertItem(m_hWnd,&insert);
}

LRESULT CSettingsTree::OnRButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	// ignore right-click during dragging
	if (m_DragMode==DRAG_NONE)
		bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_DragMode!=DRAG_NONE)
	{
		TDragMode mode=m_DragMode;
		HTREEITEM dragItem=m_DragItem;
		if (m_DropSubmenu==m_DropTarget && mode>=DRAG_MOVE)
			m_DropSubmenu=NULL;
		m_DragItem=NULL;
		ReleaseCapture();
		// perform drop operation
		if (mode==DRAG_DELETE)
		{
			TreeView_EnsureVisible(m_hWnd,dragItem);
			if (!m_pStdDragItem)
			{
				TreeView_DeleteItem(m_hWnd,dragItem);
				m_pOwner->SerializeData();
			}
		}
		else if (mode>=DRAG_MOVE)
		{
			HTREEITEM hParent=m_DropTarget;
			HTREEITEM hAfter=TVI_LAST;
			if (m_DropLocation==DROP_AFTER)
			{
				hAfter=hParent;
				hParent=TreeView_GetParent(m_hWnd,hParent);
			}
			else if (m_DropLocation==DROP_BEFORE)
			{
				hAfter=TreeView_GetPrevSibling(m_hWnd,hParent);
				if (!hAfter) hAfter=TVI_FIRST;
				hParent=TreeView_GetParent(m_hWnd,hParent);
			}
			if (hParent)
			{
				HTREEITEM hChild=TreeView_GetChild(m_hWnd,hParent);
				if (hChild)
				{
					TVITEM item={TVIF_PARAM,hChild};
					TreeView_GetItem(m_hWnd,&item);
					if (!item.lParam)
					{
						TreeView_DeleteItem(m_hWnd,hChild);
						hAfter=TVI_LAST;
					}
				}
			}

			HTREEITEM hNewItem;
			if (m_pStdDragItem)
			{
				hNewItem=CreateStdItem(m_pStdDragItem,hParent,hAfter);
			}
			else
			{
				hNewItem=CreateDeepCopy(dragItem,hParent,hAfter,mode==DRAG_COPY);
				if (mode==DRAG_MOVE)
					TreeView_DeleteItem(m_hWnd,dragItem);
			}

			TreeView_SelectItem(m_hWnd,hNewItem);
			TreeView_EnsureVisible(m_hWnd,hNewItem);
			m_pOwner->SerializeData();
		}
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnKeyUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_DragMode>=DRAG_MOVE)
	{
		m_DragMode=(m_pStdDragItem || GetKeyState(VK_CONTROL)<0)?DRAG_COPY:DRAG_MOVE;
		SetCursor(m_DragMode==DRAG_COPY?m_CursorCopy:m_CursorMove);
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnGetDlgCode( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_DragMode!=DRAG_NONE || (lParam && ((MSG*)lParam)->message==WM_KEYDOWN && ((MSG*)lParam)->wParam==VK_RETURN))
		return DLGC_WANTALLKEYS;
	bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN)
		return 0;
	bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnCaptureChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_DragMode!=DRAG_NONE)
	{
		m_DragMode=DRAG_NONE;
		TreeView_SelectDropTarget(m_hWnd,NULL);
		TreeView_SetInsertMark(m_hWnd,NULL,FALSE);
		SetCursor(LoadCursor(NULL,IDC_ARROW));
		if (m_DragItem) TreeView_SelectItem(m_hWnd,m_DragItem);
		KillTimer(TIMER_SCROLL);
		KillTimer(TIMER_HOVER);
		if (m_DropSubmenu)
			TreeView_DeleteItem(m_hWnd,m_DropSubmenu);
		m_DropSubmenu=NULL;
		m_HoverItem=NULL;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==TIMER_SCROLL)
	{
		if (m_Scroll>0)
			SendMessage(WM_VSCROLL,SB_LINEDOWN);
		else
			SendMessage(WM_VSCROLL,SB_LINEUP);
	}
	else if (wParam==TIMER_HOVER)
	{
		if (m_HoverItem)
		{
			TreeView_Expand(m_hWnd,m_HoverItem,TVE_EXPAND);
			if (!TreeView_GetChild(m_hWnd,m_HoverItem))
			{
				if (m_DropSubmenu)
					TreeView_DeleteItem(m_hWnd,m_DropSubmenu);
				TVINSERTSTRUCT insert={m_HoverItem,TVI_FIRST,{TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_STATE|TVIF_TEXT,NULL,TVIS_EXPANDED,TVIS_EXPANDED}};
				CString text=LoadStringEx(IDS_EMPTY_MENU);
				insert.item.pszText=(LPWSTR)(LPCWSTR)text;
				m_DropSubmenu=TreeView_InsertItem(m_hWnd,&insert);
				TreeView_SelectItem(m_hWnd,m_DropSubmenu);
				TreeView_EnsureVisible(m_hWnd,m_DropSubmenu);
			}
		}
		KillTimer(TIMER_HOVER);
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CSettingsTree::OnBeginLabelEdit( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	if (m_bDisabled) return TRUE;
	CWindow edit=TreeView_GetEditControl(m_hWnd);
	SetWindowSubclass(edit,SubclassEditboxProc,'CLSH',0);
	NMTVDISPINFO *pInfo=(NMTVDISPINFO*)pnmh;
	if (!pInfo->item.lParam)
		return TRUE;
	CTreeItem *pItem=(CTreeItem*)pInfo->item.lParam;
	if (!pItem->pStdCommand->IsCustom())
		return TRUE;
	edit.SetWindowText(pItem->label);
	return FALSE;
}

LRESULT CSettingsTree::OnEndLabelEdit( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVDISPINFO *pInfo=(NMTVDISPINFO*)pnmh;
	if (pInfo->item.pszText)
	{
		CTreeItem *pItem=(CTreeItem*)pInfo->item.lParam;
		pItem->label=pInfo->item.pszText;
		m_pOwner->SerializeData();
		UpdateItem(pItem,pInfo->item.hItem);
	}
	return FALSE;
}

LRESULT CSettingsTree::OnKeyDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVKEYDOWN *pKey=(NMTVKEYDOWN*)pnmh;
	if (m_DragMode!=DRAG_NONE)
	{
		if (pKey->wVKey==VK_ESCAPE)
		{
			ReleaseCapture();
			return TRUE;
		}
		if (m_DragMode>=DRAG_MOVE)
		{
			m_DragMode=(m_pStdDragItem || GetKeyState(VK_CONTROL)<0)?DRAG_COPY:DRAG_MOVE;
			SetCursor(m_DragMode==DRAG_COPY?m_CursorCopy:m_CursorMove);
		}
	}
	// F2 to rename
	if (pKey->wVKey==VK_F2)
	{
		HTREEITEM hSelected=TreeView_GetSelection(m_hWnd);
		if (hSelected)
			TreeView_EditLabel(m_hWnd,hSelected);
	}
	// Del to delete
	if (pKey->wVKey==VK_DELETE)
	{
		HTREEITEM hSelected=TreeView_GetSelection(m_hWnd);
		if (hSelected)
			DeleteItem(hSelected);
	}
	return 0;
}

void CSettingsTree::DeleteItem( HTREEITEM hItem )
{
	if (!m_bDisabled)
	{
		TreeView_DeleteItem(m_hWnd,hItem);
		m_pOwner->SerializeData();
	}
}

///////////////////////////////////////////////////////////////////////////////

class CCommandsTree: public CWindowImpl<CCommandsTree, CWindow>
{
public:
	BEGIN_MSG_MAP( CCommandsTree )
		MESSAGE_HANDLER( WM_GETDLGCODE, OnGetDlgCode )
		MESSAGE_HANDLER( WM_CHAR, OnChar )
	END_MSG_MAP()

protected:
	LRESULT OnGetDlgCode( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
};

LRESULT CCommandsTree::OnGetDlgCode( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (lParam && ((MSG*)lParam)->message==WM_KEYDOWN && ((MSG*)lParam)->wParam==VK_RETURN)
		return DLGC_WANTALLKEYS;
	bHandled=FALSE;
	return 0;
}

LRESULT CCommandsTree::OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN)
		return 0;
	bHandled=FALSE;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

CEditCustomItemDlg::~CEditCustomItemDlg( void )
{
	if (m_hIcon) DestroyIcon(m_hIcon);
	if (m_hIconD) DestroyIcon(m_hIconD);
}

void CEditCustomItemDlg::InitDialog( CWindow commandCombo, const CStdCommand *pStdcommands, int style, int mask, CWindow linkCombo, const KNOWNFOLDERID *const *pCommonLinks )
{
	InitResize(MOVE_HORIZONTAL|MOVE_GRIPPER);
	CString str;
	GetWindowText(str);
	CString title;
	title.Format(str,m_pItem->GetDisplayName(true));
	SetWindowText(title);

	for (int i=0;pStdcommands[i].name;i++)
		if (pStdcommands[i].IsStyle(style,mask) && !pStdcommands[i].IsSeparator() && !pStdcommands[i].IsCustom())
			commandCombo.SendMessage(CB_ADDSTRING,0,(LPARAM)pStdcommands[i].name);

	commandCombo.SetWindowText(m_pItem->command);
	int idx=(int)commandCombo.SendMessage(CB_FINDSTRINGEXACT,-1,(LPARAM)(const wchar_t*)m_pItem->command);
	if (idx>=0)
		commandCombo.SendMessage(CB_SETCURSEL,idx);

	for (int i=0;pCommonLinks[i];i++)
	{
		if (*pCommonLinks[i]==FOLDERID_DesktopRoot)
		{
			linkCombo.SendMessage(CB_ADDSTRING,0,(LPARAM)L"Main Desktop | ::{Desktop}");
		}
		else
		{
			CAbsolutePidl path;
			if (FAILED(SHGetKnownFolderIDList(*pCommonLinks[i],0,NULL,&path)) || !path) continue;
			CComString pName;
			if (SUCCEEDED(SHGetNameFromIDList(path,SIGDN_NORMALDISPLAY,&pName)))
			{
				CComString pPath;
				if (SUCCEEDED(SHGetNameFromIDList(path,SIGDN_DESKTOPABSOLUTEPARSING,&pPath)))
				{
					wchar_t text[_MAX_PATH*2];
					int len=Sprintf(text,_countof(text),L"%s | ",(const wchar_t*)pName);
					if (!PathUnExpandEnvStrings(pPath,text+len,_countof(text)-len))
						Strcpy(text+len,_countof(text)-len,pPath);
					linkCombo.SendMessage(CB_ADDSTRING,0,(LPARAM)text);
				}
			}
		}
	}

	linkCombo.SetWindowText(m_pItem->link);
	idx=(int)linkCombo.SendMessage(CB_FINDSTRINGEXACT,-1,(LPARAM)(const wchar_t*)m_pItem->link);
	if (idx>=0)
		linkCombo.SendMessage(CB_SETCURSEL,idx);

	m_hIcon=NULL;
	m_IconKey=0;
	m_hIconD=NULL;
	m_IconDKey=0;
	m_StoredItem=*m_pItem;

	RECT rc;
	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())==ERROR_SUCCESS)
	{
		ULONG size=sizeof(rc);
		if (regSettings.QueryBinaryValue(L"CEditCustomItemDlg",&rc,&size)==ERROR_SUCCESS && size==sizeof(rc))
			SetStoreRect(rc);
	}

	COMBOBOXINFO info={sizeof(info)};
	commandCombo.SendMessage(CB_GETCOMBOBOXINFO,0,(LPARAM)&info);
	SHAutoComplete(info.hwndItem,SHACF_FILESYSTEM);
	linkCombo.SendMessage(CB_GETCOMBOBOXINFO,0,(LPARAM)&info);
	SHAutoComplete(info.hwndItem,SHACF_FILESYSTEM);
}

LRESULT CEditCustomItemDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CEditCustomItemDlg>::OnSize();
	return 0;
}

void CEditCustomItemDlg::StorePlacement( void )
{
	RECT rc;
	GetStoreRect(rc);

	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())!=ERROR_SUCCESS)
		regSettings.Create(HKEY_CURRENT_USER,GetSettingsRegPath());
	regSettings.SetBinaryValue(L"CEditCustomItemDlg",&rc,sizeof(rc));
}

LRESULT CEditCustomItemDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	::EnableWindow(m_EnableParent,TRUE);
	m_bResult=true;
	StorePlacement();
	DestroyWindow();
	return 0;
}

LRESULT CEditCustomItemDlg::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	::EnableWindow(m_EnableParent,TRUE);
	m_bResult=false;
	StorePlacement();
	DestroyWindow();
	(*m_pItem)=m_StoredItem;
	return 0;
}

void CEditCustomItemDlg::UpdateIcons( int iconID, int iconDID )
{
	unsigned int key=m_pItem->GetIconKey();

	if (key!=m_IconKey)
	{
		m_IconKey=key;
		if (m_hIcon) DestroyIcon(m_hIcon);
		m_hIcon=m_pItem->LoadIcon(false,m_Modules);
		SendDlgItemMessage(iconID,STM_SETICON,(WPARAM)m_hIcon);
	}

	if (iconDID)
	{
		key=m_pItem->GetIconDKey(key);
		if (key!=m_IconDKey)
		{
			m_IconDKey=key;
			if (m_hIconD) DestroyIcon(m_hIconD);
			m_hIconD=m_pItem->LoadIconD(m_hIcon,m_Modules);
			SendDlgItemMessage(iconDID,STM_SETICON,(WPARAM)m_hIconD);
		}
	}
}

CString CEditCustomItemDlg::GetComboText( WORD wNotifyCode, WORD wID )
{
	CString text;
	if (wNotifyCode==CBN_SELENDOK)
	{
		int idx=(int)SendDlgItemMessage(wID,CB_GETCURSEL);
		text.Empty();
		if (idx>=0)
		{
			int len=(int)SendDlgItemMessage(wID,CB_GETLBTEXTLEN,idx);
			SendDlgItemMessage(wID,CB_GETLBTEXT,idx,(LPARAM)text.GetBuffer(len+1));
			text.ReleaseBuffer(len);
		}
	}
	else
		GetDlgItemText(wID,text);
	text.TrimLeft();
	text.TrimRight();
	return text;
}

///////////////////////////////////////////////////////////////////////////////

class CBrowseLinkEvents: public IFileDialogEvents, public IFileDialogControlEvents
{
public:
	CBrowseLinkEvents( void ) { m_pResult=NULL; }

	// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **ppvObject );
	virtual ULONG STDMETHODCALLTYPE AddRef( void ) { return 1; }
	virtual ULONG STDMETHODCALLTYPE Release( void ) { return 1; }

	// IFileDialogEvents
	STDMETHOD(OnFileOk)( IFileDialog *pfd ) { return S_OK; }
	STDMETHOD(OnFolderChanging)( IFileDialog *pfd, IShellItem *psiFolder ) { return S_OK; }
	STDMETHOD(OnFolderChange)( IFileDialog *pfd ) { return S_OK; }
	STDMETHOD(OnSelectionChange)( IFileDialog *pfd ) { return S_OK; }
	STDMETHOD(OnShareViolation)( IFileDialog *pfd, IShellItem *psi, FDE_SHAREVIOLATION_RESPONSE *pResponse ) { return S_OK; }
	STDMETHOD(OnTypeChange)( IFileDialog *pfd ) { return S_OK; }
	STDMETHOD(OnOverwrite)( IFileDialog *pfd, IShellItem *psi, FDE_OVERWRITE_RESPONSE *pResponse ) { return S_OK; }

	// IFileDialogControlEvents
	STDMETHOD(OnItemSelected)( IFileDialogCustomize *pfdc, DWORD dwIDCtl, DWORD dwIDItem ) { return S_OK; }
	STDMETHOD(OnButtonClicked)( IFileDialogCustomize *pfdc, DWORD dwIDCtl );
	STDMETHOD(OnCheckButtonToggled)( IFileDialogCustomize *pfdc, DWORD dwIDCtl, BOOL bChecked ) { return S_OK; }
	STDMETHOD(OnControlActivating)( IFileDialogCustomize *pfdc, DWORD dwIDCtl ) { return S_OK; }

	CComPtr<IShellItem> m_pResult;
};

HRESULT STDMETHODCALLTYPE CBrowseLinkEvents::QueryInterface( REFIID riid, void **ppvObject )
{
	if (riid==IID_IUnknown)
	{
		*ppvObject=(IUnknown*)(IFileDialogEvents*)this;
		return S_OK;
	}
	if (riid==IID_IFileDialogEvents)
	{
		*ppvObject=(IFileDialogEvents*)this;
		return S_OK;
	}
	if (riid==IID_IFileDialogControlEvents)
	{
		*ppvObject=(IFileDialogControlEvents*)this;
		return S_OK;
	}
	*ppvObject=NULL;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE CBrowseLinkEvents::OnButtonClicked( IFileDialogCustomize *pfdc, DWORD dwIDCtl )
{
	CComQIPtr<IFileDialog> pfd=pfdc;
	if (!pfd) return E_INVALIDARG;

	CComPtr<IShellItem> pItem;
	if (SUCCEEDED(pfd->GetCurrentSelection(&pItem)))
	{
		SFGAOF attr;
		if (SUCCEEDED(pItem->GetAttributes(SFGAO_FOLDER|SFGAO_STREAM,&attr)) && (attr&(SFGAO_FOLDER|SFGAO_STREAM))!=SFGAO_FOLDER)
			pItem=NULL;
	}
	if (!pItem)
	{
		pfd->GetFolder(&pItem);
	}
	m_pResult=std::move(pItem);
	pfd->Close(S_FALSE);
	return S_OK;
}

///////////////////////////////////////////////////////////////////////////////

bool BrowseCommandHelper( HWND parent, wchar_t *text )
{
	if (text[0]=='"')
	{
		// remove quotes
		int len=Strlen(text);
		if (len>1 && text[len-1]=='"')
		{
			len-=2;
			memmove(text,text+1,len*2);
			text[len]=0;
		}
	}
	OPENFILENAME ofn={sizeof(ofn)};
	ofn.hwndOwner=parent;
	wchar_t filters[256];
	Strcpy(filters,_countof(filters),LoadStringEx(IDS_COMMAND_FILTERS));
	for (wchar_t *c=filters;*c;c++)
		if (*c=='|') *c=0;
	ofn.lpstrFilter=filters;
	ofn.nFilterIndex=1;
	DoEnvironmentSubst(text,_MAX_PATH);
	wchar_t dir[_MAX_PATH];
	Strcpy(dir,_countof(dir),text);
	PathRemoveFileSpec(dir);
	ofn.lpstrInitialDir=dir;
	text[0]=0;
	ofn.lpstrFile=text;
	ofn.nMaxFile=_MAX_PATH;
	ofn.Flags=OFN_DONTADDTORECENT|OFN_ENABLESIZING|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR|OFN_NODEREFERENCELINKS;
	if (GetOpenFileName(&ofn))
	{
		wchar_t buf[_MAX_PATH];
		UnExpandEnvStrings(text,buf,_countof(buf));
		// add quotes if needed
		if (wcschr(buf,' '))
		{
			int len=Strlen(buf);
			if (len<_MAX_PATH-3)
			{
				memcpy(text+1,buf,len*2);
				text[0]='"';
				text[len+1]='"';
				text[len+2]=0;
			}
		}
		else
			Strcpy(text,_MAX_PATH,buf);
		return true;
	}
	return false;
}

bool BrowseLinkHelper( HWND parent, wchar_t *text, bool bFoldersOnly )
{
	DoEnvironmentSubst(text,_MAX_PATH);

	CComPtr<IFileOpenDialog> pDialog;
	if (FAILED(pDialog.CoCreateInstance(CLSID_FileOpenDialog,NULL,CLSCTX_INPROC_SERVER)))
		return false;
	CComQIPtr<IFileDialogCustomize> pCustomize(pDialog);
	if (!pCustomize)
		return false;

	pDialog->SetTitle(LoadStringEx(bFoldersOnly?IDS_PICK_LINK_FOLDER:IDS_PICK_LINK_TITLE));
	if (!bFoldersOnly) // add separate buttons for selecting files/folders to the dialog
	{
		pDialog->SetOkButtonLabel(LoadStringEx(IDS_PICK_LINK_FILE));
		wchar_t button[256];
		Sprintf(button,_countof(button),L"  %s  ",LoadStringEx(IDS_PICK_LINK_FOLDER));
		pCustomize->AddPushButton(101,button);
	}

	CBrowseLinkEvents events;
	DWORD cookie;
	pDialog->Advise(&events,&cookie);
	if (bFoldersOnly) // set FOS_PICKFOLDERS option to use dialog in folder-only mode
		pDialog->SetOptions(FOS_PICKFOLDERS|FOS_ALLNONSTORAGEITEMS|FOS_DONTADDTORECENT|FOS_DEFAULTNOMINIMODE);
	else
		pDialog->SetOptions(FOS_ALLNONSTORAGEITEMS|FOS_FILEMUSTEXIST|FOS_DONTADDTORECENT|FOS_DEFAULTNOMINIMODE|FOS_NODEREFERENCELINKS);
	{
		const wchar_t *c=wcschr(text,'|');
		if (c)
		{
			for (c++;*c==' ';)
				c++;
		}
		else
			c=text;
		CComPtr<IShellItem> pItem;
		if (SUCCEEDED(SHCreateItemFromParsingName(c,NULL,IID_IShellItem,(void**)&pItem)))
			pDialog->SetFolder(pItem);
	}

	HRESULT hr=pDialog->Show(parent);
	CComPtr<IShellItem> pResult;
	if (hr==S_OK)
		pDialog->GetResult(&pResult);
	else if (hr==S_FALSE)
		pResult=events.m_pResult;

	pDialog->Unadvise(cookie);

	if (pResult)
	{
		CComString pName;
		pResult->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
		UnExpandEnvStrings(pName,text,_MAX_PATH);
	}

	return pResult!=NULL;
}

bool BrowseIconHelper( HWND parent, wchar_t *text )
{
	int id=0;
	if (*text)
	{
		wchar_t *c=wcsrchr(text,',');
		if (c)
		{
			*c=0;
			id=_wtol(c+1);
		}
	}
	else
		Strcpy(text,_MAX_PATH,L"shell32.dll");
	if (BrowseForIcon(parent,text,id))
	{
		if (_wcsicmp(PathFindExtension(text),L".ico")==0)
			return true;
		if (id>0)
		{
			wchar_t buf[20];
			Sprintf(buf,_countof(buf),L", %d",id);
			Strcat(text,_MAX_PATH,buf);
			return true;
		}
	}
	return false;
}

bool CEditCustomItemDlg::Run( HWND parent, int dlgID )
{
	parent=GetAncestor(parent,GA_ROOT);
	DLGTEMPLATE *pTemplate=LoadDialogEx(dlgID);
	Create(parent,pTemplate);
	ShowWindow(SW_SHOWNORMAL);
	::EnableWindow(parent,FALSE);
	SetEnableParent(parent);
	MSG msg;
	while (m_hWnd && GetMessage(&msg,0,0,0))
	{
		if (IsDialogMessage(&msg)) continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return GetResult();
}

///////////////////////////////////////////////////////////////////////////////

// Subclass the tooltip to delay the tip when the mouse moves from one tree item to the next
static LRESULT CALLBACK SubclassInfoTipProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==TTM_UPDATE)
	{
		int time=(int)SendMessage(hWnd,TTM_GETDELAYTIME,TTDT_RESHOW,0);
		SetTimer(hWnd,'CLSH',time,NULL);
		return 0;
	}
	if (uMsg==WM_TIMER && wParam=='CLSH')
	{
		KillTimer(hWnd,wParam);
		DefSubclassProc(hWnd,TTM_UPDATE,0,0);
		return 0;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CCustomTreeDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_pSetting=NULL;

	InitResize();
	EnableThemeDialogTexture(m_hWnd,ETDT_ENABLETAB);
	m_Tree.SubclassWindow(GetDlgItem(IDC_TREEITEMS),this);
	m_CommandsTree.SubclassWindow(GetDlgItem(IDC_TREECOMMANDS));
	TreeView_SetImageList(m_CommandsTree,TreeView_GetImageList(m_Tree,TVSIL_NORMAL),TVSIL_NORMAL);
	if (IsAppThemed())
	{
		m_Tree.SetWindowLong(GWL_STYLE,m_Tree.GetWindowLong(GWL_STYLE)|TVS_TRACKSELECT);
		SetWindowTheme(m_Tree,L"Explorer",NULL);
		m_CommandsTree.SetWindowLong(GWL_STYLE,m_CommandsTree.GetWindowLong(GWL_STYLE)|TVS_TRACKSELECT);
		SetWindowTheme(m_CommandsTree,L"Explorer",NULL);
	}

	int height=TreeView_GetItemHeight(m_Tree);
	int minHeight=GetSystemMetrics(SM_CXSMICON)+2;
	if (height<minHeight)
	{
		TreeView_SetItemHeight(m_Tree,minHeight);
		TreeView_SetItemHeight(m_CommandsTree,minHeight);
	}

	HWND tooltip=TreeView_GetToolTips(m_CommandsTree);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,10000);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,1000);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_RESHOW,1000);
	SetWindowSubclass(tooltip,SubclassInfoTipProc,'CLSH',0);

	tooltip=TreeView_GetToolTips(m_Tree);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,10000);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,1000);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_RESHOW,1000);
	SetWindowSubclass(tooltip,SubclassInfoTipProc,'CLSH',0);

	for (int i=0;m_pStdCommands[i].name;i++)
	{
		const CStdCommand &command=m_pStdCommands[i];
		if (!command.IsStyle(m_Style,m_StyleMask)) continue;
		CString displayName=LoadStringEx(command.displayNameId);
		TVINSERTSTRUCT insert={NULL,TVI_LAST,{TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE,NULL,0,0,(LPWSTR)(LPCWSTR)displayName}};
		insert.item.iImage=insert.item.iSelectedImage=1;
		if (!command.icon)
			insert.item.iImage=insert.item.iSelectedImage=0;
		else
		{
			CTreeItem tItem;
			tItem.icon=command.icon;
			tItem.pStdCommand=&command;
			insert.item.iImage=insert.item.iSelectedImage=m_Tree.LoadIcon(&tItem);
		}

		insert.item.lParam=(LPARAM)&command;
		TreeView_InsertItem(m_CommandsTree,&insert);
	}

	CString str=LoadStringEx(m_bMenu?IDS_TREE_MENU:IDS_TREE_TOOLBAR);
	SetDlgItemText(IDC_STATICLEFT,str);
	str=LoadStringEx(m_bMenu?IDS_DRAG_DROP_SM_TIP:IDS_DRAG_DROP_EXP_TIP);
	SetDlgItemText(IDC_STATICHINT,str);

	return TRUE;
}

LRESULT CCustomTreeDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CCustomTreeDlg>::OnSize();
	return 0;
}

LRESULT CCustomTreeDlg::OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CWindow tree=(HWND)wParam;
	if (tree.m_hWnd!=m_Tree.m_hWnd && tree.m_hWnd!=m_CommandsTree.m_hWnd)
		return 0;
	if (m_Tree.IsDragging())
	{
		ReleaseCapture();
		return 0;
	}
	HTREEITEM hItem=NULL;
	POINT pt;
	if ((DWORD)lParam==0xFFFFFFFF)
	{
		// keyboard
		hItem=TreeView_GetSelection(tree);
		if (hItem)
		{
			RECT rc;
			TreeView_GetItemRect(tree,hItem,&rc,TRUE);
			pt.x=(rc.left+rc.right)/2;
			pt.y=(rc.top+rc.bottom)/2;
		}
		else
			pt.x=pt.y=0;
		tree.ClientToScreen(&pt);
	}
	else
	{
		DWORD pos=GetMessagePos();
		pt.x=(short)LOWORD(pos);
		pt.y=(short)HIWORD(pos);
		TVHITTESTINFO test={pt};
		tree.ScreenToClient(&test.pt);
		if (TreeView_HitTest(tree,&test) && (test.flags&(TVHT_ONITEMICON|TVHT_ONITEMLABEL)))
		{
			TreeView_SelectItem(tree,test.hItem);
			hItem=test.hItem;
		}
	}

	CTreeItem *pItem=NULL;
	if (tree.m_hWnd==m_Tree.m_hWnd)
	{
		if (hItem)
		{
			TVITEM item={TVIF_PARAM,hItem};
			TreeView_GetItem(m_Tree,&item);
			pItem=(CTreeItem*)item.lParam;
		}
	}
	else if (!hItem)
		return 0;

	enum {
		CMD_EDIT=1,
		CMD_RENAME,
		CMD_DELETE,
		CMD_RESETALL,
		CMD_ADD,
		CMD_ADD_MENU,
	};

	HMENU menu=CreatePopupMenu();
	wchar_t text[256];
	if (tree.m_hWnd==m_Tree.m_hWnd)
	{
		if (pItem)
		{
			if (!pItem->pStdCommand->IsSeparator())
			{
				Sprintf(text,_countof(text),L"%s\tEnter",LoadStringEx(IDS_MENU_EDIT));
				AppendMenu(menu,MF_STRING,CMD_EDIT,text);
				SetMenuDefaultItem(menu,CMD_EDIT,FALSE);
				Sprintf(text,_countof(text),L"%s\tF2",LoadStringEx(IDS_MENU_RENAME));
				AppendMenu(menu,MF_STRING,CMD_RENAME,text);
				if (m_pSetting->IsLocked())
				{
					EnableMenuItem(menu,CMD_EDIT,MF_BYCOMMAND|MF_GRAYED);
					EnableMenuItem(menu,CMD_RENAME,MF_BYCOMMAND|MF_GRAYED);
				}
				else if (!pItem->pStdCommand->IsCustom())
					EnableMenuItem(menu,CMD_RENAME,MF_BYCOMMAND|MF_GRAYED);
				if (!TreeView_GetChild(m_Tree,hItem))
					AppendMenu(menu,MF_STRING,CMD_ADD_MENU,LoadStringEx(IDS_INSERT_MENU));
				if (m_pSetting->IsLocked())
					EnableMenuItem(menu,CMD_ADD_MENU,MF_BYCOMMAND|MF_GRAYED);
			}
			Sprintf(text,_countof(text),L"%s\tDel",LoadStringEx(IDS_MENU_DELETE));
			AppendMenu(menu,MF_STRING,CMD_DELETE,text);
			if (m_pSetting->IsLocked())
				EnableMenuItem(menu,CMD_DELETE,MF_BYCOMMAND|MF_GRAYED);
		}
		if (!m_pSetting->IsLocked())
			AppendMenu(menu,MF_STRING,CMD_RESETALL,LoadStringEx(m_bMenu?IDS_RESET_MENU:IDS_RESET_TOOLBAR));
	}
	else
	{
		Sprintf(text,_countof(text),L"%s\tEnter",LoadStringEx(m_bMenu?IDS_ADD_MENU:IDS_ADD_TOOLBAR));
		AppendMenu(menu,MF_STRING,CMD_ADD,text);
		SetMenuDefaultItem(menu,CMD_ADD,FALSE);
		if (m_pSetting->IsLocked())
			EnableMenuItem(menu,CMD_ADD,MF_BYCOMMAND|MF_GRAYED);
	}

	int res=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,0,m_hWnd,NULL);
	DestroyMenu(menu);
	if (res==CMD_EDIT)
		EditItemInternal(pItem,hItem);
	if (res==CMD_DELETE)
		m_Tree.DeleteItem(hItem);
	if (res==CMD_RENAME)
		TreeView_EditLabel(m_Tree,hItem);
	if (res==CMD_RESETALL)
	{
		if (::MessageBox(m_hWnd,LoadStringEx(m_bMenu?IDS_RESET_MENU_WARN:IDS_RESET_TOOLBAR_WARN),LoadStringEx(m_bMenu?IDS_RESET_MENU:IDS_RESET_TOOLBAR),MB_YESNO|MB_ICONWARNING)==IDYES)
		{
			{
				CSettingsLockWrite lock;
				m_pSetting->value=m_pSetting->defValue;
				m_pSetting->flags|=CSetting::FLAG_DEFAULT;
				ItemsChanged();
				SetSettingsDirty();
			}
			SetGroup(m_pSetting-1,true);
		}
	}
	if (res==CMD_ADD)
	{
		AddItem(hItem);
	}
	if (res==CMD_ADD_MENU)
	{
		TVINSERTSTRUCT insert={hItem,TVI_FIRST,{TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_STATE|TVIF_TEXT,NULL,TVIS_EXPANDED,TVIS_EXPANDED}};
		CString text=LoadStringEx(IDS_EMPTY_MENU);
		insert.item.pszText=(LPWSTR)(LPCWSTR)text;
		HTREEITEM hNew=TreeView_InsertItem(m_Tree,&insert);
		TreeView_SelectItem(m_Tree,hNew);
		TreeView_EnsureVisible(m_Tree,hNew);
	}
	return 0;
}

const CStdCommand *CCustomTreeDlg::FindStdCommand( const wchar_t *name )
{
	for (int i=0;m_pStdCommands[i].name;i++)
		if (m_pStdCommands[i].IsStyle(m_Style,m_StyleMask) && _wcsicmp(m_pStdCommands[i].name,name)==0)
			return &m_pStdCommands[i];
	return NULL;
}

LRESULT CCustomTreeDlg::OnGetInfoTip( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVGETINFOTIP *pTip=(NMTVGETINFOTIP*)pnmh;
	const CStdCommand *pCommand=NULL;
	if (idCtrl==IDC_TREECOMMANDS)
		pCommand=(CStdCommand*)pTip->lParam;
	else if (!m_pSetting->IsLockedReg())
	{
		CTreeItem *pItem=(CTreeItem*)pTip->lParam;
		if (pItem)
			pCommand=pItem->pStdCommand;
	}
	if (pCommand)
	{
		if (idCtrl==IDC_TREECOMMANDS)
			Sprintf(pTip->pszText,pTip->cchTextMax,L"%s\n%s",LoadStringEx(pCommand->tipID),LoadStringEx(m_bMenu?IDS_COMMAND_SM_TIP:IDS_COMMAND_EXP_TIP));
		else
			Strcpy(pTip->pszText,pTip->cchTextMax,LoadStringEx(pCommand->tipID));
	}
	return 0;
}

LRESULT CCustomTreeDlg::OnBeginDrag( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTREEVIEW *pDrag=(NMTREEVIEW*)pnmh;
	m_Tree.BeginDrag((CStdCommand*)pDrag->itemNew.lParam);
	return 0;
}

LRESULT CCustomTreeDlg::OnAddItem( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	if (pnmh->code==TVN_KEYDOWN)
	{
		NMTVKEYDOWN *pKey=(NMTVKEYDOWN*)pnmh;
		if (pKey->wVKey!=VK_RETURN)
		{
			bHandled=FALSE;
			return 0;
		}
	}

	HTREEITEM hItem=TreeView_GetSelection(m_CommandsTree);
	if (hItem)
		AddItem(hItem);
	return TRUE;
}

LRESULT CCustomTreeDlg::OnEditItem( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	if (pnmh->code==TVN_KEYDOWN)
	{
		NMTVKEYDOWN *pKey=(NMTVKEYDOWN*)pnmh;
		if (pKey->wVKey!=VK_RETURN)
		{
			bHandled=FALSE;
			return 0;
		}
	}

	TVITEM item={TVIF_PARAM,TreeView_GetSelection(m_Tree)};
	if (!item.hItem) return 0;
	TreeView_GetItem(m_Tree,&item);
	CTreeItem *pItem=(CTreeItem*)item.lParam;
	if (pItem)
		EditItemInternal(pItem,item.hItem);
	return TRUE;
}

void CCustomTreeDlg::AddItem( HTREEITEM hCommand )
{
	if (m_pSetting->IsLocked()) return;
	TVITEM item={TVIF_PARAM,hCommand};
	TreeView_GetItem(m_CommandsTree,&item);
	const CStdCommand *pCommand=(CStdCommand*)item.lParam;
	HTREEITEM hAfter=TreeView_GetSelection(m_Tree);
	HTREEITEM hParent=NULL;
	if (hAfter)
	{
		hParent=TreeView_GetParent(m_Tree,hAfter);
		TVITEM item={TVIF_PARAM,hAfter};
		TreeView_GetItem(m_Tree,&item);
		if (!item.lParam)
		{
			TreeView_DeleteItem(m_Tree,hAfter);
			hAfter=TVI_LAST;
		}
	}
	else
		hAfter=TVI_LAST;
	HTREEITEM hItem=m_Tree.CreateStdItem(pCommand,hParent,hAfter);
	TreeView_SelectItem(m_Tree,hItem);
	TreeView_EnsureVisible(m_Tree,hItem);
	SerializeData();
}

HTREEITEM CCustomTreeDlg::GetRoot( void )
{
	return TreeView_GetRoot(m_Tree);
}

HTREEITEM CCustomTreeDlg::GetChild( HTREEITEM hParent )
{
	return TreeView_GetChild(m_Tree,hParent);
}

HTREEITEM CCustomTreeDlg::GetNext( HTREEITEM hItem )
{
	return TreeView_GetNextSibling(m_Tree,hItem);
}

CTreeItem *CCustomTreeDlg::GetItem( HTREEITEM hItem )
{
	TVITEM item={TVIF_PARAM,hItem};
	TreeView_GetItem(m_Tree,&item);
	return (CTreeItem*)item.lParam;
}

void CCustomTreeDlg::EditItemInternal( CTreeItem *pItem, HTREEITEM hItem )
{
	if (!m_pSetting->IsLocked() && !pItem->pStdCommand->IsSeparator() && EditItem(pItem,m_Tree,hItem,m_Tree.m_Modules))
	{
		SerializeData();
		m_Tree.UpdateItem(pItem,hItem);
	}
}

bool CCustomTreeDlg::EditItem( CTreeItem *pItem, HWND tree, HTREEITEM hItem, std::vector<HMODULE> &modules )
{
	MessageBox(L"Edit Item",L"",MB_OK);
	return false;
}

LRESULT CCustomTreeDlg::OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVCUSTOMDRAW *pDraw=(NMTVCUSTOMDRAW*)pnmh;
	if (pDraw->nmcd.dwDrawStage==CDDS_PREPAINT)
	{
		return CDRF_NOTIFYITEMDRAW;
	}
	else if (pDraw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT && !pDraw->nmcd.lItemlParam)
	{
		if (!IsAppThemed())
		{
			TVITEM item={TVIF_STATE,(HTREEITEM)pDraw->nmcd.dwItemSpec,0,TVIS_SELECTED};
			TreeView_GetItem(m_Tree,&item);
			if (item.state&TVIS_SELECTED)
				return CDRF_DODEFAULT;
		}
		pDraw->clrText=GetSysColor(COLOR_GRAYTEXT);
	}
	return CDRF_DODEFAULT;
}

int CCustomTreeDlg::ParseTreeItem( CTreeItem *pItem, CSettingsParser &parser )
{
	const CStdCommand *pStdCommand=FindStdCommand(pItem->name);
	if (pStdCommand && pStdCommand->IsSeparator())
	{
		pItem->pStdCommand=pStdCommand;
		return 0;
	}
	wchar_t text[256];
	Sprintf(text,_countof(text),L"%s.Command",pItem->name);
	pItem->SetCommand(parser.FindSetting(text,L""),m_pStdCommands,m_Style,m_StyleMask);
	Sprintf(text,_countof(text),L"%s.Link",pItem->name);
	pItem->link=parser.FindSetting(text,L"");
	Sprintf(text,_countof(text),L"%s.Label",pItem->name);
	pItem->label=parser.FindSetting(text,L"");
	Sprintf(text,_countof(text),L"%s.Tip",pItem->name);
	pItem->tip=parser.FindSetting(text,L"");
	Sprintf(text,_countof(text),L"%s.Icon",pItem->name);
	pItem->icon=parser.FindSetting(text,L"");
	ParseTreeItemExtra(pItem,parser);
	return m_Tree.UpdateItem(pItem,NULL);
}

void CCustomTreeDlg::AppendString( std::vector<wchar_t> &stringBuilder, const wchar_t *text )
{
	stringBuilder.insert(stringBuilder.end(),text,text+Strlen(text));
}

void CCustomTreeDlg::SerializeItem( HTREEITEM hItem, std::vector<wchar_t> &stringBuilder )
{
	TVITEM item={TVIF_PARAM,hItem};
	CTreeItem *pItem=NULL;
	if (hItem)
	{
		TreeView_GetItem(m_Tree,&item);
		pItem=(CTreeItem*)item.lParam;
	}
	wchar_t text[2048];
	HTREEITEM hChild=hItem?TreeView_GetChild(m_Tree,hItem):TreeView_GetRoot(m_Tree);
	if (hChild)
	{
		// save .Items
		if (pItem)
			Sprintf(text,_countof(text),L"%s.Items=",pItem->name);
		else
			Strcpy(text,_countof(text),L"Items=");
		AppendString(stringBuilder,text);

		bool bFirst=true;
		for (;hChild;hChild=TreeView_GetNextSibling(m_Tree,hChild), bFirst=false)
		{
			item.hItem=hChild;
			TreeView_GetItem(m_Tree,&item);
			CTreeItem *pChild=(CTreeItem*)item.lParam;
			if (pChild)
			{
				Sprintf(text,_countof(text),bFirst?L"%s":L",%s",pChild->name);
				AppendString(stringBuilder,text);
			}
		}
		AppendString(stringBuilder,L"\n");
	}

	if (pItem && !pItem->pStdCommand->IsSeparator())
	{
		// save other properties
		if (!pItem->command.IsEmpty())
		{
			Sprintf(text,_countof(text),L"%s.Command=%s\n",pItem->name,pItem->command);
			AppendString(stringBuilder,text);
		}
		if (!pItem->link.IsEmpty())
		{
			Sprintf(text,_countof(text),L"%s.Link=%s\n",pItem->name,pItem->link);
			AppendString(stringBuilder,text);
		}
		if (!pItem->label.IsEmpty())
		{
			Sprintf(text,_countof(text),L"%s.Label=%s\n",pItem->name,pItem->label);
			AppendString(stringBuilder,text);
		}
		if (!pItem->tip.IsEmpty())
		{
			Sprintf(text,_countof(text),L"%s.Tip=%s\n",pItem->name,pItem->tip);
			AppendString(stringBuilder,text);
		}
		if (!pItem->icon.IsEmpty())
		{
			Sprintf(text,_countof(text),L"%s.Icon=%s\n",pItem->name,pItem->icon);
			AppendString(stringBuilder,text);
		}
		SerializeItemExtra(pItem,stringBuilder);
	}
	hChild=hItem?TreeView_GetChild(m_Tree,hItem):TreeView_GetRoot(m_Tree);
	for (;hChild;hChild=TreeView_GetNextSibling(m_Tree,hChild))
		SerializeItem(hChild,stringBuilder);
}

void CCustomTreeDlg::SerializeData( void )
{
	std::vector<wchar_t> stringBuilder;
	SerializeItem(NULL,stringBuilder);
	stringBuilder.push_back(0);
	CSettingsLockWrite lock;
	const wchar_t *strNew=&stringBuilder[0];
	const wchar_t *strOld=m_pSetting->value.vt==VT_BSTR?m_pSetting->value.bstrVal:L"???";
	if ((m_pSetting->flags&CSetting::FLAG_DEFAULT) || wcscmp(strNew,strOld)!=0)
		SetSettingsDirty();
	m_pSetting->value=CComVariant(strNew);
	m_pSetting->flags&=~CSetting::FLAG_DEFAULT;
	ItemsChanged();
}

void CCustomTreeDlg::CreateTreeItems( CSettingsParser &parser, HTREEITEM hParent, const CSettingsParser::TreeItem *pItems, int index )
{
	for (;!pItems[index].name.IsEmpty();index++)
	{
		CTreeItem *pNewItem=new CTreeItem;
		pNewItem->name=pItems[index].name;
		TVINSERTSTRUCT insert={hParent,TVI_LAST,{TVIF_STATE|TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE,NULL,TVIS_EXPANDED,TVIS_EXPANDED}};
		insert.item.iImage=insert.item.iSelectedImage=ParseTreeItem(pNewItem,parser);
		insert.item.lParam=(LPARAM)pNewItem;
		CString dispName=pNewItem->GetDisplayName(false);
		insert.item.pszText=(LPWSTR)(LPCWSTR)dispName;
		HTREEITEM hItem=TreeView_InsertItem(m_Tree,&insert);

		if (pItems[index].children>=0)
			CreateTreeItems(parser,hItem,pItems,pItems[index].children);
	}
}

void CCustomTreeDlg::SetGroup( CSetting *pGroup, bool bReset )
{
	if (!m_pSetting || bReset) // optimization - initialize the UI only the first time
	{
		TreeView_DeleteAllItems(m_Tree);
		m_pSetting=pGroup+1;
		Assert(!m_pSetting->pLinkTo);
		m_Tree.SetDisabled(m_pSetting->IsLocked());
		const wchar_t *pText=L"";
		if (m_pSetting->value.vt==VT_BSTR)
			pText=m_pSetting->value.bstrVal;

		CSettingsParser parser;
		parser.LoadText(pText,Strlen(pText));
		parser.ParseText();
		std::vector<CSettingsParser::TreeItem> items;
		parser.ParseTree(L"Items",items);
		if (!items.empty())
			CreateTreeItems(parser,NULL,&items[0],0);
		InitItems();
	}
	// use the tooltip from m_CommandsTree to show the "locked" tooltip for m_Tree. otherwise it will behave like an infotip and move from item to item
	CWindow tooltip=(HWND)m_CommandsTree.SendMessage(TVM_GETTOOLTIPS);
	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,(UINT_PTR)(HWND)m_Tree};
	CString str=LoadStringEx(m_bMenu?IDS_MENU_LOCKED:IDS_TOOLBAR_LOCKED);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	if (m_pSetting->IsLockedReg())
		tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	else
		tooltip.SendMessage(TTM_DELTOOL,0,(LPARAM)&tool);

	TreeView_SetBkColor(m_Tree,m_pSetting->IsLocked()?GetSysColor(COLOR_BTNFACE):-1);
	TreeView_SetBkColor(m_CommandsTree,m_pSetting->IsLocked()?GetSysColor(COLOR_BTNFACE):-1);
}

CCustomTreeDlg::CCustomTreeDlg( bool bMenu, const CStdCommand *pStdCommands, int style, int mask ):
m_Tree(*new CSettingsTree(mask)), m_CommandsTree(*new CCommandsTree)
{
	m_bMenu=bMenu;
	m_pStdCommands=pStdCommands;
	m_Style=style;
	m_StyleMask=mask;
}

CCustomTreeDlg::~CCustomTreeDlg( void )
{
	delete &m_Tree;
	delete &m_CommandsTree;
}

///////////////////////////////////////////////////////////////////////////////

class CBrowseForIconDlg: public CResizeableDlg<CBrowseForIconDlg>
{
public:
	CBrowseForIconDlg( wchar_t *path, int &id ): m_pPath(path), m_ID(id) { m_bResult=false; }
	void SetEnableParent( HWND parent ) { m_EnableParent=parent; }
	bool GetResult( void ) { return m_bResult; }

	BEGIN_MSG_MAP( CBrowseForIconDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_GETMINMAXINFO, OnGetMinMaxInfo )
		COMMAND_ID_HANDLER( IDC_BUTTONBROWSE, OnBrowse )
		COMMAND_ID_HANDLER( IDOK, OnOK )
		COMMAND_ID_HANDLER( IDCANCEL, OnCancel )
		COMMAND_HANDLER( IDC_EDITFILE, EN_KILLFOCUS, OnKillFocus )
		NOTIFY_HANDLER( IDC_LISTICONS, LVN_ITEMACTIVATE, OnItemActivate )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_EDITFILE,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_BUTTONBROWSE,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_LISTICONS,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDOK,MOVE_MOVE_X|MOVE_MOVE_Y)
		RESIZE_CONTROL(IDCANCEL,MOVE_MOVE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnKillFocus( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnItemActivate( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

private:
	HWND m_EnableParent;
	bool m_bResult;
	wchar_t *m_pPath;
	int &m_ID;
	CString m_LastName;

	void UpdateIcons( int id );
	void StorePlacement( void );
	static BOOL CALLBACK EnumIconsProc( HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam );
};

LRESULT CBrowseForIconDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	SetDlgItemText(IDC_EDITFILE,m_pPath);
	CWindow list=GetDlgItem(IDC_LISTICONS);
	list.SendMessage(LVM_SETEXTENDEDLISTVIEWSTYLE,LVS_EX_DOUBLEBUFFER,LVS_EX_DOUBLEBUFFER);
	HDC hdc=CreateCompatibleDC(NULL);
	HFONT font0=(HFONT)SelectObject(hdc,list.GetFont());
	TEXTMETRIC metrics;
	GetTextMetrics(hdc,&metrics);
	SelectObject(hdc,font0);
	DeleteDC(hdc);
	ListView_SetIconSpacing(list,GetSystemMetrics(SM_CXICON)+metrics.tmHeight+7,GetSystemMetrics(SM_CYICON)+metrics.tmHeight+7);
	if (IsAppThemed())
		SetWindowTheme(list,L"Explorer",NULL);
	UpdateIcons(m_ID);
	InitResize(MOVE_MODAL);
	SHAutoComplete(GetDlgItem(IDC_EDITFILE),SHACF_FILESYSTEM);

	RECT rc;
	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())==ERROR_SUCCESS)
	{
		ULONG size=sizeof(rc);
		if (regSettings.QueryBinaryValue(L"CBrowseForIconDlg",&rc,&size)==ERROR_SUCCESS && size==sizeof(rc))
			SetStoreRect(rc);
	}
	return TRUE;
}

LRESULT CBrowseForIconDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CBrowseForIconDlg>::OnSize();
	return 0;
}

LRESULT CBrowseForIconDlg::OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	OPENFILENAME ofn={sizeof(ofn)};
	ofn.hwndOwner=m_hWnd;
	wchar_t filters[256];
	Strcpy(filters,_countof(filters),LoadStringEx(IDS_ICON_FILTERS));
	for (wchar_t *c=filters;*c;c++)
		if (*c=='|') *c=0;
	ofn.lpstrFilter=filters;
	ofn.nFilterIndex=1;
	wchar_t path[_MAX_PATH];
	GetDlgItemText(IDC_EDITFILE,path,_countof(path));
	DoEnvironmentSubst(path,_countof(path));
	wchar_t dir[_MAX_PATH];
	Strcpy(dir,_countof(dir),path);
	PathRemoveFileSpec(dir);
	ofn.lpstrInitialDir=dir;
	path[0]=0;
	ofn.lpstrFile=path;
	ofn.nMaxFile=_countof(path);
	CString title=LoadStringEx(IDS_ICON_TITLE);
	ofn.lpstrTitle=title;
	ofn.Flags=OFN_DONTADDTORECENT|OFN_ENABLESIZING|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
	if (GetOpenFileName(&ofn))
	{
		wchar_t buf[_MAX_PATH];
		UnExpandEnvStrings(path,buf,_countof(buf));
		SetDlgItemText(IDC_EDITFILE,buf);
		UpdateIcons(-1);
	}
	return 0;
}

void CBrowseForIconDlg::StorePlacement( void )
{
	RECT rc;
	GetStoreRect(rc);

	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())!=ERROR_SUCCESS)
		regSettings.Create(HKEY_CURRENT_USER,GetSettingsRegPath());
	regSettings.SetBinaryValue(L"CBrowseForIconDlg",&rc,sizeof(rc));
}

LRESULT CBrowseForIconDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (GetFocus()==GetDlgItem(IDC_EDITFILE))
	{
		SendMessage(WM_NEXTDLGCTL,(WPARAM)GetDlgItem(IDOK).m_hWnd,TRUE);
		return 0;
	}
	CString text;
	GetDlgItemText(IDC_EDITFILE,text);
	text.TrimLeft();
	text.TrimRight();
	Strcpy(m_pPath,_MAX_PATH,text);
	CWindow list=GetDlgItem(IDC_LISTICONS);
	int sel=ListView_GetNextItem(list,-1,LVNI_SELECTED);
	if (sel>=0)
	{
		LVITEM item={LVIF_PARAM,sel};
		ListView_GetItem(list,&item);
		m_ID=(int)item.lParam;
	}
	else
		m_ID=0;
	::EnableWindow(m_EnableParent,TRUE);
	m_bResult=true;
	StorePlacement();
	DestroyWindow();
	return 0;
}

LRESULT CBrowseForIconDlg::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	::EnableWindow(m_EnableParent,TRUE);
	m_bResult=false;
	StorePlacement();
	DestroyWindow();
	return 0;
}

LRESULT CBrowseForIconDlg::OnKillFocus( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	UpdateIcons(-1);
	return 0;
}

LRESULT CBrowseForIconDlg::OnItemActivate( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	PostMessage(WM_COMMAND,IDOK,0);
	return 0;
}

BOOL CALLBACK CBrowseForIconDlg::EnumIconsProc( HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam )
{
	if (IS_INTRESOURCE(lpszName))
	{
		HWND list=(HWND)lParam;
		HICON hIcon=(HICON)LoadImage(hModule,lpszName,IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_DEFAULTCOLOR);
		if (hIcon)
		{
			HIMAGELIST images=ListView_GetImageList(list,LVSIL_NORMAL);
			int idx=ImageList_AddIcon(images,hIcon);
			DestroyIcon(hIcon);
			LVITEM item={LVIF_IMAGE|LVIF_PARAM|LVIF_TEXT};
			item.iItem=idx;
			item.iImage=idx;
			item.lParam=(WORD)(DWORD_PTR)lpszName;
			wchar_t text[20];
			Sprintf(text,_countof(text),L"%d",(int)item.lParam);
			item.pszText=text;
			ListView_InsertItem(list,&item);
		}
	}
	return TRUE;
}

void CBrowseForIconDlg::UpdateIcons( int id )
{
	wchar_t path[1024];
	GetDlgItemText(IDC_EDITFILE,path,_countof(path));
	DoEnvironmentSubst(path,_countof(path));
	if (m_LastName==path && id==-1) return;
	m_LastName=path;

	CWindow list=GetDlgItem(IDC_LISTICONS);
	ListView_DeleteAllItems(list);
	HIMAGELIST images=ImageList_Create(GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),ILC_COLOR32|ILC_MASK,10,10);
	ListView_SetImageList(list,images,LVSIL_NORMAL);
	ImageList_RemoveAll(images);

	if (_wcsicmp(PathFindExtension(path),L".ico")==0)
	{
		// load icon file
		HICON hIcon=(HICON)LoadImage(NULL,path,IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_DEFAULTCOLOR|LR_LOADFROMFILE);
		if (hIcon)
		{
			ImageList_AddIcon(images,hIcon);
			DestroyIcon(hIcon);
			LVITEM item={LVIF_IMAGE|LVIF_PARAM|LVIF_STATE,0,0,LVIS_SELECTED,LVIS_SELECTED};
			item.iImage=0;
			item.lParam=0;
			ListView_InsertItem(list,&item);
		}
	}
	else
	{
		// load module file
		HINSTANCE hInstance=_AtlBaseModule.GetResourceInstance();
		HMODULE hModule;
		if (*path)
			hModule=LoadLibraryEx(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		else
			hModule=hInstance;
		if (hModule)
		{
			// enumerate icons, extract icons
			list.SendMessage(WM_SETREDRAW,FALSE);
			EnumResourceNames(hModule,RT_GROUP_ICON,EnumIconsProc,(LONG_PTR)list.m_hWnd);
			list.SendMessage(WM_SETREDRAW,TRUE);
			// free module
			if (hModule!=hInstance) FreeLibrary(hModule);
		}
		if (ListView_GetItemCount(list)>0)
		{
			int idx=0;
			if (id>0)
			{
				LVFINDINFO find={LVFI_PARAM,0,id};
				idx=ListView_FindItem(list,-1,&find);
				if (idx<0) idx=0;
			}
			ListView_SetItemState(list,idx,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
			ListView_EnsureVisible(list,idx,FALSE);
			list.InvalidateRect(NULL);
		}
	}
}

bool BrowseForIcon( HWND hWndParent, wchar_t *path, int &id )
{
	CBrowseForIconDlg dlg(path,id);

	hWndParent=GetAncestor(hWndParent,GA_ROOT);
	DLGTEMPLATE *pTemplate=LoadDialogEx(IDD_BROWSEFORICON);
	dlg.Create(hWndParent,pTemplate);
	dlg.ShowWindow(SW_SHOWNORMAL);
	EnableWindow(hWndParent,FALSE);
	dlg.SetEnableParent(hWndParent);
	MSG msg;
	while (dlg.m_hWnd && GetMessage(&msg,0,0,0))
	{
		if (dlg.IsDialogMessage(&msg)) continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return dlg.GetResult();
}

///////////////////////////////////////////////////////////////////////////////

bool BrowseForBitmap( HWND hWndParent, wchar_t *path, bool bAllowJpeg )
{
	OPENFILENAME ofn={sizeof(ofn)};
	ofn.hwndOwner=hWndParent;
	wchar_t filters[256];
	Strcpy(filters,_countof(filters),LoadStringEx(bAllowJpeg?IDS_BMP_FILTERS2:IDS_BMP_FILTERS));
	for (wchar_t *c=filters;*c;c++)
		if (*c=='|') *c=0;
	ofn.lpstrFilter=filters;
	ofn.nFilterIndex=1;
	wchar_t dir[_MAX_PATH];
	Strcpy(dir,_countof(dir),path);
	PathRemoveFileSpec(dir);
	ofn.lpstrInitialDir=dir;
	path[0]=0;
	ofn.lpstrFile=path;
	ofn.nMaxFile=_MAX_PATH;
	CString title=LoadStringEx(IDS_BMP_TITLE);
	ofn.lpstrTitle=title;
	ofn.Flags=OFN_DONTADDTORECENT|OFN_ENABLESIZING|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
	if (GetOpenFileName(&ofn))
	{
		wchar_t buf[_MAX_PATH];
		UnExpandEnvStrings(path,buf,_countof(buf));
		Strcpy(path,_MAX_PATH,buf);
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

bool BrowseForSound( HWND hWndParent, wchar_t *path )
{
	OPENFILENAME ofn={sizeof(ofn)};
	ofn.hwndOwner=hWndParent;
	wchar_t filters[256];
	Strcpy(filters,_countof(filters),LoadStringEx(IDS_WAV_FILTERS));
	for (wchar_t *c=filters;*c;c++)
		if (*c=='|') *c=0;
	ofn.lpstrFilter=filters;
	ofn.nFilterIndex=1;
	wchar_t dir[_MAX_PATH];
	Strcpy(dir,_countof(dir),path);
	PathRemoveFileSpec(dir);
	ofn.lpstrInitialDir=dir;
	path[0]=0;
	ofn.lpstrFile=path;
	ofn.nMaxFile=_MAX_PATH;
	CString title=LoadStringEx(IDS_WAV_TITLE);
	ofn.lpstrTitle=title;
	ofn.Flags=OFN_DONTADDTORECENT|OFN_ENABLESIZING|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
	if (GetOpenFileName(&ofn))
	{
		wchar_t buf[_MAX_PATH];
		UnExpandEnvStrings(path,buf,_countof(buf));
		Strcpy(path,_MAX_PATH,buf);
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

class CTreeSettingsDlg: public CResizeableDlg<CTreeSettingsDlg>
{
public:
	enum {
		IDC_EDITBOX=101,
		IDC_BROWSEBTN=102,
		IDC_PLAYBTN=103,
		WM_EDITKEY=WM_APP+11,
	};

	enum TEditMode
	{
		EDIT_NONE,
		EDIT_INT,
		EDIT_STRING,
		EDIT_ICON,
		EDIT_BITMAP,
		EDIT_BITMAP_JPG,
		EDIT_SOUND,
		EDIT_HOTKEY,
		EDIT_HOTKEY_ANY,
		EDIT_COLOR,
		EDIT_FONT,
		EDIT_DIRECTORY,
	};

	BEGIN_MSG_MAP( CTreeSettingsDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_EDITKEY, OnEditKey )
		MESSAGE_HANDLER( WM_CONTEXTMENU, OnContextMenu )
		COMMAND_HANDLER( IDC_EDITBOX, EN_CHANGE, OnEditChange )
		COMMAND_HANDLER( IDC_EDITBOX, EN_KILLFOCUS, OnKillFocus )
		COMMAND_HANDLER( IDC_BROWSEBTN, BN_CLICKED, OnBrowse )
		COMMAND_HANDLER( IDC_PLAYBTN, BN_CLICKED, OnPlay )
		COMMAND_HANDLER( IDC_BROWSEBTN, BN_KILLFOCUS, OnKillFocus )
		NOTIFY_HANDLER( IDC_SETTINGS, NM_CUSTOMDRAW, OnCustomDraw )
		NOTIFY_HANDLER( IDC_SETTINGS, NM_CLICK, OnClick )
		NOTIFY_HANDLER( IDC_SETTINGS, NM_DBLCLK, OnClick )
		NOTIFY_HANDLER( IDC_SETTINGS, TVN_KEYDOWN, OnKeyDown )
		NOTIFY_HANDLER( IDC_SETTINGS, TVN_GETINFOTIP, OnGetInfoTip )
		NOTIFY_HANDLER( IDC_SETTINGS, TVN_SELCHANGED, OnSelChanged )
		NOTIFY_HANDLER( IDC_SETTINGS, TVN_ITEMEXPANDING, OnExpanding )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_SETTINGS,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDC_STATICTIP,MOVE_SIZE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

	void SetGroup( CSetting *pGroup, const CString &filter, const CSetting *pSelect );
	void Validate( void ) { ApplyEditBox(); }

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEditKey( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEditChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnKillFocus( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnPlay( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnKeyDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnClick( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnGetInfoTip( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnSelChanged( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnExpanding( int idCtrl, LPNMHDR pnmh, BOOL& bHandled ) { return TRUE; } // prevent collapsing

private:
	CWindow m_Tree;
	const CSetting *m_pGroup;
	CWindow m_EditBox;
	CWindow m_BrowseButton;
	CWindow m_PlayButton;
	HICON m_PlayIcon;
	TEditMode m_EditMode;
	HTREEITEM m_EditItem;
	CSetting *m_pEditSetting;
	bool m_bBasic;
	bool m_bIgnoreFocus;
	bool m_bSearch;

	void UpdateGroup( const CSetting *pModified );
	void ToggleItem( HTREEITEM hItem, bool bDefault );
	void PlayItem( HTREEITEM hItem );
	void LocateSetting( const CSetting *pSetting );
	void ItemSelected( HTREEITEM hItem, CSetting *pSetting, bool bEnabled );
	void ApplyEditBox( void );
	void UpdateEditPosition( void );
	const CSetting *GetNextSetting( const CSetting *pSetting, const wchar_t *filter );
	bool IsVisible( const CSetting *pSetting, const wchar_t *filter ) const;
	HTREEITEM GetNextItem( HTREEITEM hItem, bool bChildren=true );
	HTREEITEM FindSettingsItem( const wchar_t *name );
};

// Subclass the tree control to forward the command messages to the parent
static LRESULT CALLBACK SubclassTreeProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_COMMAND)
		return SendMessage(GetParent(hWnd),uMsg,wParam,lParam);
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static int g_HotKey;

static bool GetKeyName( int key, wchar_t *text, int size, bool bCtrl, bool bAlt, bool bShift )
{
	wchar_t name[100];
	text[0]=0;
	int len=0;
	if (bCtrl || (key&(HOTKEYF_CONTROL<<8)))
	{
		GetKeyNameText(MapVirtualKey(VK_CONTROL,MAPVK_VK_TO_VSC)<<16,name,_countof(name));
		len+=Sprintf(text+len,size-len,L"%s+",name);
	}
	if (bAlt || (key&(HOTKEYF_ALT<<8)))
	{
		GetKeyNameText(MapVirtualKey(VK_MENU,MAPVK_VK_TO_VSC)<<16,name,_countof(name));
		len+=Sprintf(text+len,size-len,L"%s+",name);
	}
	if (bShift || (key&(HOTKEYF_SHIFT<<8)))
	{
		GetKeyNameText(MapVirtualKey(VK_SHIFT,MAPVK_VK_TO_VSC)<<16,name,_countof(name));
		len+=Sprintf(text+len,size-len,L"%s+",name);
	}
	if (key)
	{
		int scan=MapVirtualKey(key&255,MAPVK_VK_TO_VSC);
		if (scan==0)
		{
			text[0]=0;
			return false;
		}
		if (!GetKeyNameText(scan<<16,name,_countof(name)))
		{
			text[0]=0;
			return false;
		}
		Strcpy(text+len,size-len,name);
	}

	return true;
}

// Subclass the edit control to forward some keyboard messages to the parent and to act as a hotkey control
static LRESULT CALLBACK SubclassEditProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (dwRefData)
	{
		static bool s_bCtrl, s_bAlt, s_bShift;

		if (uMsg==WM_GETDLGCODE)
			return DLGC_WANTALLKEYS;

		if (uMsg==WM_CHAR || uMsg==WM_SYSCHAR)
			return 0;

		if (uMsg==WM_KEYDOWN || uMsg==WM_SYSKEYDOWN)
		{
			if ((wParam==VK_UP || wParam==VK_DOWN || wParam==VK_PRIOR || wParam==VK_NEXT) && GetKeyState(VK_CONTROL)>=0 && GetKeyState(VK_MENU)>=0 && GetKeyState(VK_SHIFT)>=0)
				return SendMessage(GetParent(hWnd),uMsg,wParam,lParam);
			if (wParam==VK_ESCAPE)
			{ // Esc - clears the key
				g_HotKey=0;
				SetWindowText(hWnd,L"");
				return 0;
			}
			wchar_t text[100];
			if (wParam==VK_CONTROL) s_bCtrl=true, g_HotKey=0;
			else if (wParam==VK_SHIFT) s_bShift=true, g_HotKey=0;
			else if (wParam==VK_MENU) s_bAlt=true, g_HotKey=0;
			else
			{
				g_HotKey=wParam&255;
				if (s_bCtrl) g_HotKey|=(HOTKEYF_CONTROL<<8);
				if (s_bAlt) g_HotKey|=(HOTKEYF_ALT<<8);
				if (s_bShift) g_HotKey|=(HOTKEYF_SHIFT<<8);
				if (dwRefData==1)
				{
					if (!s_bAlt && !(s_bCtrl && s_bShift))
						g_HotKey=0;
				}
			}
			GetKeyName(g_HotKey,text,_countof(text),s_bCtrl,s_bAlt,s_bShift);
			SetWindowText(hWnd,text);
			return 0;
		}
		if (uMsg==WM_KEYUP || uMsg==WM_SYSKEYUP)
		{
			wchar_t text[100];
			if (wParam==VK_CONTROL) s_bCtrl=false;
			if (wParam==VK_MENU) s_bAlt=false;
			if (wParam==VK_SHIFT) s_bShift=false;
			GetKeyName(g_HotKey,text,_countof(text),s_bCtrl,s_bAlt,s_bShift);
			SetWindowText(hWnd,text);
			return 0;
		}
		if (uMsg==WM_RBUTTONUP || uMsg==WM_CONTEXTMENU)
			return 0;
	}
	if (uMsg==WM_CHAR && wParam==VK_SPACE && GetKeyState(VK_CONTROL)<0)
		return SendMessage(GetParent(hWnd),WM_KEYDOWN,wParam,lParam);
	if (uMsg==WM_CHAR && wParam==VK_RETURN)
		return 0;
	if (uMsg==WM_GETDLGCODE && wParam==VK_RETURN)
		return DLGC_WANTALLKEYS;
	if (uMsg==WM_KEYDOWN)
	{
		if (wParam==VK_UP || wParam==VK_DOWN || wParam==VK_PRIOR || wParam==VK_NEXT || (wParam=='P' && GetKeyState(VK_CONTROL)<0))
			return SendMessage(GetParent(hWnd),uMsg,wParam,lParam);
		if (wParam==VK_RETURN || wParam==VK_ESCAPE)
			return SendMessage(GetParent(GetParent(hWnd)),CTreeSettingsDlg::WM_EDITKEY,wParam,0);
	}
	if (uMsg==WM_KILLFOCUS)
	{
		if (wParam && (HWND)wParam==GetDlgItem(GetParent(hWnd),CTreeSettingsDlg::IDC_BROWSEBTN))
			return 0;
	}
	if (uMsg==WM_CHAR && wParam==16)
		return 0; // prevent Ctrl+P from playing the default sound
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CTreeSettingsDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	InitResize();
	EnableThemeDialogTexture(m_hWnd,ETDT_ENABLETAB);

	m_Tree=GetDlgItem(IDC_SETTINGS);
	m_Tree.SetWindowLong(GWL_STYLE,m_Tree.GetWindowLong(GWL_STYLE)|WS_CLIPCHILDREN);
	if (IsAppThemed())
	{
		m_Tree.SetWindowLong(GWL_STYLE,m_Tree.GetWindowLong(GWL_STYLE)|TVS_TRACKSELECT);
		SetWindowTheme(m_Tree,L"Explorer",NULL);
	}
	m_Tree.SendMessage(TVM_SETEXTENDEDSTYLE,TVS_EX_DOUBLEBUFFER,TVS_EX_DOUBLEBUFFER);
	SetWindowSubclass(m_Tree,SubclassTreeProc,'CLSH',0);
	HINSTANCE hInstance=_AtlBaseModule.GetResourceInstance();

	HIMAGELIST images=GetSettingsImageList(m_Tree);
	TreeView_SetImageList(m_Tree,images,TVSIL_NORMAL);
	int height=TreeView_GetItemHeight(m_Tree);
	if (height<18) TreeView_SetItemHeight(m_Tree,18);

	HWND tooltip=TreeView_GetToolTips(m_Tree);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,10000);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,1000);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_RESHOW,1000);
	SetWindowSubclass(tooltip,SubclassInfoTipProc,'CLSH',0);

	HFONT font=m_Tree.GetFont();
	m_EditBox=CreateWindow(L"EDIT",NULL,WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,m_Tree,(HMENU)IDC_EDITBOX,hInstance,NULL);
	m_EditBox.SetFont(font);
	SetWindowSubclass(m_EditBox,SubclassEditProc,'CLSH',0);
	m_BrowseButton=CreateWindow(L"BUTTON",L"...",WS_CHILD|BS_PUSHBUTTON,0,0,0,0,m_Tree,(HMENU)IDC_BROWSEBTN,hInstance,NULL);
	m_BrowseButton.SetFont(font);
	m_PlayIcon=(HICON)LoadImage(hInstance,MAKEINTRESOURCE(IDI_ICONPLAY),IMAGE_ICON,0,0,0);
	m_PlayButton=CreateWindow(L"BUTTON",L">",WS_CHILD|BS_PUSHBUTTON|BS_ICON,0,0,0,0,m_Tree,(HMENU)IDC_PLAYBTN,hInstance,NULL);
	m_PlayButton.SendMessage(BM_SETIMAGE,IMAGE_ICON,(LPARAM)m_PlayIcon);
	m_EditMode=EDIT_NONE;
	m_EditItem=NULL;
	m_pEditSetting=NULL;
	m_bIgnoreFocus=false;

	return TRUE;
}

LRESULT CTreeSettingsDlg::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	DestroyIcon(m_PlayIcon);
	bHandled=FALSE;
	m_EditMode=EDIT_NONE;
	return 0;
}

LRESULT CTreeSettingsDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CTreeSettingsDlg>::OnSize();
	UpdateEditPosition();
	return 0;
}

LRESULT CTreeSettingsDlg::OnEditKey( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN && !(m_EditBox.GetWindowLong(GWL_STYLE)&ES_READONLY))
		ItemSelected(m_EditItem,m_pEditSetting,true);
	return 0;
}

LRESULT CTreeSettingsDlg::OnEditChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	UpdateEditPosition();
	return 0;
}

LRESULT CTreeSettingsDlg::OnKillFocus( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (!m_bIgnoreFocus && m_pEditSetting)
	{
		ApplyEditBox();
		UpdateEditPosition();
	}
	return 0;
}

LRESULT CTreeSettingsDlg::OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (m_EditMode==EDIT_ICON)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		wchar_t text[_MAX_PATH*2];
		Strcpy(text,_countof(text),str);
		int id=0;
		if (*text)
		{
			wchar_t *c=wcsrchr(text,',');
			if (c)
			{
				*c=0;
				id=_wtol(c+1);
			}
		}
		else
			Strcpy(text,_countof(text),L"shell32.dll");
		if (BrowseForIcon(m_hWnd,text,id))
		{
			if (id>0)
			{
				wchar_t buf[20];
				Sprintf(buf,_countof(buf),L", %d",id);
				Strcat(text,_countof(text),buf);
			}
			m_EditBox.SetWindowText(text);
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_bIgnoreFocus=false;
	}
	else if (m_EditMode==EDIT_BITMAP || m_EditMode==EDIT_BITMAP_JPG)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		wchar_t text[1024];
		if (_wcsicmp(PathFindExtension(str),L".bmp")==0 || _wcsicmp(PathFindExtension(str),L".png")==0 || (m_EditMode==EDIT_BITMAP_JPG && _wcsicmp(PathFindExtension(str),L".jpg")==0))
		{
			Strcpy(text,_countof(text),str);
			DoEnvironmentSubst(text,_countof(text));
		}
		else
			text[0]=0;
		if (BrowseForBitmap(m_hWnd,text,m_EditMode==EDIT_BITMAP_JPG))
		{
			m_EditBox.SetWindowText(text);
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	else if (m_EditMode==EDIT_SOUND)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		wchar_t text[1024];
		if (_wcsicmp(PathFindExtension(str),L".wav")==0)
		{
			Strcpy(text,_countof(text),str);
			DoEnvironmentSubst(text,_countof(text));
		}
		else
			text[0]=0;
		if (BrowseForSound(m_hWnd,text))
		{
			m_EditBox.SetWindowText(text);
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	else if (m_EditMode==EDIT_COLOR)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		COLORREF val=RgbToBgr(ParseColor(str));
		static COLORREF customColors[16];
		CHOOSECOLOR choose={sizeof(choose),m_hWnd,NULL,val,customColors};
		choose.Flags=CC_ANYCOLOR|CC_FULLOPEN|CC_RGBINIT;
		if (ChooseColor(&choose))
		{
			wchar_t text[100];
			Sprintf(text,_countof(text),L"%06X",BgrToRgb(choose.rgbResult));
			m_EditBox.SetWindowText(text);
			ApplyEditBox();
			UpdateGroup(m_pEditSetting);
			m_Tree.Invalidate();
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	else if (m_EditMode==EDIT_FONT)
	{
		m_bIgnoreFocus=true;
		CString text;
		m_EditBox.GetWindowText(text);

		HDC hdc=::GetDC(NULL);
		int dpi=GetDeviceCaps(hdc,LOGPIXELSY);
		::ReleaseDC(NULL,hdc);

		LOGFONT font={0};
		const wchar_t *str=text;
		while (*str==' ')
			str++;
		str=GetToken(str,font.lfFaceName,_countof(font.lfFaceName),L",");
		int len=Strlen(font.lfFaceName);
		while (len>0 && font.lfFaceName[len-1]==' ')
			font.lfFaceName[--len]=0;
		while (*str==' ')
			str++;
		wchar_t token[256];
		str=GetToken(str,token,_countof(token),L",");
		len=Strlen(token);
		while (len>0 && token[len-1]==' ')
			token[--len]=0;
		font.lfWeight=FW_NORMAL;
		if (_wcsicmp(token,L"bold")==0)
			font.lfWeight=FW_BOLD;
		else if (_wcsicmp(token,L"italic")==0)
			font.lfItalic=1;
		else if (_wcsicmp(token,L"bold_italic")==0)
			font.lfWeight=FW_BOLD, font.lfItalic=1;
		str=GetToken(str,token,_countof(token),L", \t");
		font.lfHeight=-MulDiv(_wtol(token),dpi,72);

		CHOOSEFONT choose={sizeof(choose),m_hWnd,NULL,&font};
		choose.Flags=CF_NOSCRIPTSEL;
		if (*font.lfFaceName)
			choose.Flags|=CF_INITTOLOGFONTSTRUCT;
		if (ChooseFont(&choose))
		{
			wchar_t text[256];
			const wchar_t *type=font.lfItalic?L"italic":L"normal";
			if (font.lfWeight>=FW_BOLD)
				type=font.lfItalic?L"bold_italic":L"bold";
			Sprintf(text,_countof(text),L"%s, %s, %d",font.lfFaceName,type,(-font.lfHeight*72+dpi/2)/dpi);
			m_EditBox.SetWindowText(text);
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	else if (m_EditMode==EDIT_DIRECTORY)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		wchar_t text[1024];
		DWORD dwAttrs=GetFileAttributes(str); // ensure directory exists before passing it to dialog
		if (dwAttrs!=INVALID_FILE_ATTRIBUTES && dwAttrs&FILE_ATTRIBUTE_DIRECTORY)
		{
			Strcpy(text,_countof(text),str);
			DoEnvironmentSubst(text,_countof(text));
		}
		else
			text[0]=0;
		Strcpy(text,_countof(text),str);
		DoEnvironmentSubst(text,_countof(text));
		if (BrowseLinkHelper(m_hWnd,text,true))
			m_EditBox.SetWindowText(text);
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	return 0;
}

LRESULT CTreeSettingsDlg::OnPlay( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	PlayItem(TreeView_GetSelection(m_Tree));
	return 0;
}

LRESULT CTreeSettingsDlg::OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVCUSTOMDRAW *pDraw=(NMTVCUSTOMDRAW*)pnmh;
	if (pDraw->nmcd.dwDrawStage==CDDS_PREPAINT)
	{
		UpdateEditPosition();
		return CDRF_NOTIFYITEMDRAW;
	}
	else if (pDraw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)
	{
		TVITEM item={TVIF_IMAGE|TVIF_STATE,(HTREEITEM)pDraw->nmcd.dwItemSpec,0,TVIS_SELECTED};
		TreeView_GetItem(m_Tree,&item);
		if ((item.state&TVIS_CUT) && (!(item.state&TVIS_SELECTED) || IsAppThemed()))
			pDraw->clrText=GetSysColor(COLOR_GRAYTEXT);
	}
	return CDRF_DODEFAULT;
}

void CTreeSettingsDlg::ToggleItem( HTREEITEM hItem, bool bDefault )
{
	if (!hItem) return;
	TVITEM item={TVIF_PARAM|TVIF_IMAGE|TVIF_STATE,hItem,0,TVIS_CUT};
	TreeView_GetItem(m_Tree,&item);
	if (item.state&TVIS_CUT)
		return;
	CSetting *pSetting=(CSetting*)item.lParam;
	if (pSetting->pLinkTo) pSetting=pSetting->pLinkTo;
	if (bDefault)
	{
		if (pSetting->flags&CSetting::FLAG_DEFAULT)
			return;
		{
			CSettingsLockWrite lock;
			pSetting->flags|=CSetting::FLAG_DEFAULT;
			pSetting->value=pSetting->defValue;
			SetSettingsDirty();
		}
		if (m_pEditSetting && (m_pEditSetting==pSetting || m_pEditSetting->pLinkTo==pSetting))
		{
			m_pEditSetting=NULL;
			ItemSelected(item.hItem,pSetting,true);
		}
		if (pSetting->flags&CSetting::FLAG_CALLBACK)
		{
			SettingChangedCallback(pSetting);
		}
		UpdateGroup(pSetting);
	}
	else if (pSetting->type==CSetting::TYPE_BOOL)
	{
		// toggle
		bool state=IsVariantTrue(pSetting->value);
		{
			CSettingsLockWrite lock;
			pSetting->value=CComVariant(state?0:1);
			pSetting->flags&=~CSetting::FLAG_DEFAULT;
			SetSettingsDirty();
		}
		if (pSetting->flags&CSetting::FLAG_CALLBACK)
		{
			SettingChangedCallback(pSetting);
		}
		UpdateGroup(pSetting);
	}
	else if (pSetting->type==CSetting::TYPE_RADIO)
	{
		// set parent's value
		for (CSetting *pParent=pSetting-1;;pParent--)
		{
			if (pParent->type!=CSetting::TYPE_RADIO)
			{
				CSetting *pTarget=pParent->pLinkTo?pParent->pLinkTo:pParent;
				Assert(pTarget->type==CSetting::TYPE_INT);
				int val=(int)(pSetting-pParent-1);
				if (pTarget->value.vt==VT_I4 && pTarget->value.intVal==val)
					break;
				{
					CSettingsLockWrite lock;
					pTarget->value=CComVariant(val);
					pTarget->flags&=~CSetting::FLAG_DEFAULT;
					SetSettingsDirty();
				}
				if (pParent->flags&CSetting::FLAG_CALLBACK)
				{
					SettingChangedCallback(pParent);
				}
				UpdateGroup(pParent);
				break;
			}
		}
	}
}

void CTreeSettingsDlg::PlayItem( HTREEITEM hItem )
{
	if (!hItem) return;
	TVITEM item={TVIF_PARAM,hItem};
	TreeView_GetItem(m_Tree,&item);
	CSetting *pSetting=(CSetting*)item.lParam;
	if (pSetting->type==CSetting::TYPE_SOUND && pSetting->GetValue().vt==VT_BSTR)
	{
		if (!*pSetting->GetValue().bstrVal)
			return;
		wchar_t path[_MAX_PATH];
		Strcpy(path,_countof(path),pSetting->GetValue().bstrVal);
		DoEnvironmentSubst(path,_countof(path));
		if (_wcsicmp(PathFindExtension(path),L"wav")==0)
			PlaySound(path,NULL,SND_FILENAME|SND_ASYNC|SND_NODEFAULT|SND_SYSTEM);
		else
			PlaySound(path,NULL,SND_APPLICATION|SND_ALIAS|SND_ASYNC|SND_NODEFAULT|SND_SYSTEM);
	}
}

void CTreeSettingsDlg::LocateSetting( const CSetting *pSetting )
{
	for (const CSetting *pGroup=pSetting;;pGroup--)
	{
		if (pGroup->type==CSetting::TYPE_GROUP)
		{
			SelectSettingsTab(pGroup->nameID,true,pSetting);
			return;
		}
	}
}

LRESULT CTreeSettingsDlg::OnClick( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	DWORD pos=GetMessagePos();
	TVHITTESTINFO test={{(short)LOWORD(pos),(short)HIWORD(pos)}};
	m_Tree.ScreenToClient(&test.pt);
	if (TreeView_HitTest(m_Tree,&test) && (test.flags&(TVHT_ONITEMICON|TVHT_ONITEMLABEL)))
		ToggleItem(test.hItem,GetKeyState(VK_CONTROL)<0);

	return 0;
}

LRESULT CTreeSettingsDlg::OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if ((HWND)wParam!=m_Tree.m_hWnd)
		return 0;
	HTREEITEM hItem=NULL;
	POINT pt;
	if ((DWORD)lParam==0xFFFFFFFF)
	{
		// keyboard
		hItem=TreeView_GetSelection(m_Tree);
		if (hItem)
		{
			RECT rc;
			TreeView_GetItemRect(m_Tree,hItem,&rc,TRUE);
			pt.x=(rc.left+rc.right)/2;
			pt.y=(rc.top+rc.bottom)/2;
		}
		else
			pt.x=pt.y=0;
		m_Tree.ClientToScreen(&pt);
	}
	else
	{
		DWORD pos=GetMessagePos();
		pt.x=(short)LOWORD(pos);
		pt.y=(short)HIWORD(pos);
		TVHITTESTINFO test={pt};
		m_Tree.ScreenToClient(&test.pt);
		if (TreeView_HitTest(m_Tree,&test) && (test.flags&(TVHT_ONITEMICON|TVHT_ONITEMLABEL)))
		{
			TreeView_SelectItem(m_Tree,test.hItem);
			hItem=test.hItem;
		}
	}

	if (!hItem) return 0;

	TVITEM item={TVIF_PARAM|TVIF_IMAGE|TVIF_STATE,hItem,0,TVIS_CUT};
	TreeView_GetItem(m_Tree,&item);
	CSetting *pSetting=(CSetting*)item.lParam;

	enum
	{
		CMD_TOGGLE=1,
		CMD_DEFAULT,
		CMD_PLAY,
		CMD_LOCATE,
	};

	HMENU menu=CreatePopupMenu();
	if (pSetting->type!=CSetting::TYPE_RADIO)
	{
		if (pSetting->type==CSetting::TYPE_BOOL)
		{
			AppendMenu(menu,MF_STRING,CMD_TOGGLE,LoadStringEx(IDS_TOGGLE_SETTING));
			if (item.state&TVIS_CUT)
				EnableMenuItem(menu,CMD_TOGGLE,MF_BYCOMMAND|MF_GRAYED);
		}
		if (!(pSetting->flags&CSetting::FLAG_NODEFAULT))
		{
			AppendMenu(menu,MF_STRING,CMD_DEFAULT,LoadStringEx(IDS_DEFAULT_SETTING));
			if ((item.state&TVIS_CUT) || pSetting->IsDefault())
				EnableMenuItem(menu,CMD_DEFAULT,MF_BYCOMMAND|MF_GRAYED);
		}
	}
	else
	{
		AppendMenu(menu,MF_STRING,1,LoadStringEx(IDS_SELECT_SETTING));
		if (item.state&TVIS_CUT)
			EnableMenuItem(menu,1,MF_BYCOMMAND|MF_GRAYED);
	}
	if (pSetting->type==CSetting::TYPE_SOUND)
	{
		AppendMenu(menu,MF_STRING,CMD_PLAY,LoadStringEx(IDS_PLAY_SETTING));
		if (pSetting->GetValue().vt!=VT_BSTR || *pSetting->GetValue().bstrVal==0)
			EnableMenuItem(menu,CMD_PLAY,MF_BYCOMMAND|MF_GRAYED);
	}
	if (m_bSearch)
		AppendMenu(menu,MF_STRING,CMD_LOCATE,LoadStringEx(IDS_LOCATE_SETTING));

	int res=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,0,m_hWnd,NULL);
	DestroyMenu(menu);
	if (res==CMD_TOGGLE)
		ToggleItem(hItem,false);
	if (res==CMD_DEFAULT)
		ToggleItem(hItem,true);
	if (res==CMD_PLAY)
		PlayItem(hItem);
	if (res==CMD_LOCATE)
		LocateSetting(pSetting);

	return 0;
}

LRESULT CTreeSettingsDlg::OnKeyDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVKEYDOWN *pKey=(NMTVKEYDOWN*)pnmh;
	if (pKey->wVKey==VK_SPACE && GetKeyState(VK_MENU)>=0)
	{
		ToggleItem(TreeView_GetSelection(m_Tree),(GetKeyState(VK_CONTROL)<0));
		return 1;
	}
	if (pKey->wVKey=='P' && GetKeyState(VK_CONTROL)<0)
	{
		PlayItem(TreeView_GetSelection(m_Tree));
		return 1;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CTreeSettingsDlg::OnGetInfoTip( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVGETINFOTIP *pTip=(NMTVGETINFOTIP*)pnmh;
	const CSetting *pSetting=(CSetting*)pTip->lParam;
	if (pSetting->IsLocked())
		Sprintf(pTip->pszText,pTip->cchTextMax,L"%s\n%s",LoadStringEx(pSetting->tipID),LoadStringEx(pSetting->IsLockedReg()?IDS_SETTING_LOCKED:IDS_SETTING_LOCKED_GP));
	else
		Strcpy(pTip->pszText,pTip->cchTextMax,LoadStringEx(pSetting->tipID));
	return 0;
}

LRESULT CTreeSettingsDlg::OnSelChanged( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	HTREEITEM hItem=TreeView_GetSelection(m_Tree);
	if (hItem)
	{
		TVITEM item={TVIF_PARAM|TVIF_STATE,hItem,0,TVIS_CUT};
		TreeView_GetItem(m_Tree,&item);
		CSetting *pSetting=(CSetting*)item.lParam;
		ItemSelected(item.hItem,pSetting,(item.state&TVIS_CUT)==0);
	}
	else
		ItemSelected(NULL,NULL,false);
	return 0;
}

void CTreeSettingsDlg::ApplyEditBox( void )
{
	if (m_pEditSetting)
	{
		{
			CSettingsLockWrite lock;

			CString str;
			m_EditBox.GetWindowText(str);
			str.TrimLeft(); str.TrimRight();
			CSetting *pSetting=m_pEditSetting->pLinkTo?m_pEditSetting->pLinkTo:m_pEditSetting;
			if (pSetting->type==CSetting::TYPE_INT)
			{
				int val=_wtol(str);
				if (pSetting->value.vt!=VT_I4 || pSetting->value.intVal!=val)
				{
					pSetting->value=CComVariant(val);
					pSetting->flags&=~CSetting::FLAG_DEFAULT;
				}
			}
			else if (pSetting->type==CSetting::TYPE_COLOR)
			{
				int val=RgbToBgr(ParseColor(str));
				if (pSetting->value.vt!=VT_I4 || pSetting->value.intVal!=val)
				{
					pSetting->value=CComVariant(val);
					pSetting->flags&=~CSetting::FLAG_DEFAULT;
				}
			}
			else if (pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY)
			{
				if (pSetting->value.vt!=VT_I4 || pSetting->value.intVal!=g_HotKey)
				{
					pSetting->value=CComVariant(g_HotKey);
					pSetting->flags&=~CSetting::FLAG_DEFAULT;
				}
			}
			else if (pSetting->type==CSetting::TYPE_DIRECTORY)
			{
				if (pSetting->value.vt!=VT_BSTR || str!=pSetting->value.bstrVal)
				{
					if (str.IsEmpty()) // empty directory strings cause unexpected behavior, so we reset to avoid this
						pSetting->value=pSetting->defValue;
					else // otherwise we are very lenient about what users can input as a path
						pSetting->value=CComVariant(str);
					if (pSetting->value==pSetting->defValue)
						pSetting->flags|=CSetting::FLAG_DEFAULT;
					else
						pSetting->flags&=~CSetting::FLAG_DEFAULT;
				}
			}
			else
			{
				if (pSetting->value.vt!=VT_BSTR || str!=pSetting->value.bstrVal)
				{
					pSetting->value=CComVariant(str);
					pSetting->flags&=~CSetting::FLAG_DEFAULT;
				}
			}
			SetSettingsDirty();
		}
		if (m_pEditSetting->flags&CSetting::FLAG_CALLBACK)
		{
			SettingChangedCallback(m_pEditSetting);
		}
	}
}

void CTreeSettingsDlg::ItemSelected( HTREEITEM hItem, CSetting *pSetting, bool bEnabled )
{
	if (m_pEditSetting)
	{
		ApplyEditBox();
		const CSetting *pOld=m_pEditSetting;
		m_pEditSetting=NULL;
		UpdateGroup(pOld);
	}
	m_EditItem=NULL;
	TEditMode mode=EDIT_NONE;
	wchar_t text[2048];
	if (pSetting)
	{
		const CComVariant &valVar=pSetting->GetValue();
		if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type!=CSetting::TYPE_RADIO)
		{
			mode=EDIT_INT;
			int val=0;
			if (valVar.vt==VT_I4)
				val=valVar.intVal;
			Sprintf(text,_countof(text),L"%d",val);
		}
		else if (pSetting->type==CSetting::TYPE_STRING || pSetting->type==CSetting::TYPE_ICON || pSetting->type==CSetting::TYPE_BITMAP || pSetting->type==CSetting::TYPE_BITMAP_JPG || pSetting->type==CSetting::TYPE_SOUND || pSetting->type==CSetting::TYPE_FONT || pSetting->type==CSetting::TYPE_DIRECTORY)
		{
			if (valVar.vt==VT_BSTR)
				Strcpy(text,_countof(text),valVar.bstrVal);
			else
				text[0]=0;
			if (pSetting->type==CSetting::TYPE_STRING)
				mode=EDIT_STRING;
			else if (pSetting->type==CSetting::TYPE_ICON)
				mode=EDIT_ICON;
			else if (pSetting->type==CSetting::TYPE_BITMAP)
				mode=EDIT_BITMAP;
			else if (pSetting->type==CSetting::TYPE_BITMAP_JPG)
				mode=EDIT_BITMAP_JPG;
			else if (pSetting->type==CSetting::TYPE_SOUND)
				mode=EDIT_SOUND;
			else if (pSetting->type==CSetting::TYPE_FONT)
				mode=EDIT_FONT;
			else
				mode=EDIT_DIRECTORY;
		}
		else if (pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY)
		{
			text[0]=0;
			g_HotKey=0;
			if (valVar.vt==VT_I4)
			{
				g_HotKey=valVar.intVal;
				GetKeyName(valVar.intVal,text,_countof(text),false,false,false);
			}
			if (bEnabled)
				mode=pSetting->type==CSetting::TYPE_HOTKEY_ANY?EDIT_HOTKEY_ANY:EDIT_HOTKEY;
		}
		else if (pSetting->type==CSetting::TYPE_COLOR)
		{
			mode=EDIT_COLOR;
			int val=0;
			if (valVar.vt==VT_I4)
				val=BgrToRgb(valVar.intVal);
			Sprintf(text,_countof(text),L"%06X",val);
		}
	}

	RECT rc;
	m_EditMode=mode;
	if (mode!=EDIT_NONE)
	{
		CString str=LoadStringEx(pSetting->nameID);
		TVITEM item={TVIF_TEXT,hItem,0,0,(LPWSTR)(LPCWSTR)str};
		TreeView_SetItem(m_Tree,&item);
		m_Tree.GetClientRect(&rc);
		int w=rc.right;
		TreeView_GetItemRect(m_Tree,hItem,&rc,TRUE);
		rc.left=rc.right;
		rc.right=w;
		m_EditItem=hItem;
		m_pEditSetting=pSetting;
	}

	if (mode==EDIT_ICON || mode==EDIT_BITMAP || mode==EDIT_BITMAP_JPG || mode==EDIT_SOUND || mode==EDIT_FONT || mode==EDIT_COLOR || mode==EDIT_DIRECTORY)
	{
		RECT rc2=rc;
		int width=(rc2.bottom-rc2.top)*3/2;
		rc2.left=rc2.right-width;
		if (mode==EDIT_SOUND)
		{
			m_PlayButton.SetWindowPos(NULL,&rc2,SWP_NOZORDER|SWP_SHOWWINDOW);
			rc2.left-=width;
			rc2.right-=width;
		}
		else
			m_PlayButton.ShowWindow(SW_HIDE);
		m_BrowseButton.SetWindowPos(NULL,&rc2,SWP_NOZORDER|SWP_SHOWWINDOW);
		m_BrowseButton.EnableWindow(bEnabled);
		rc.right=rc2.left;
	}
	else
	{
		m_BrowseButton.ShowWindow(SW_HIDE);
		m_PlayButton.ShowWindow(SW_HIDE);
	}

	if (mode!=EDIT_NONE)
	{
		m_EditBox.SetWindowText(text);
		m_EditBox.SendMessage(EM_SETREADONLY,!bEnabled,0);
		m_EditBox.SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_SHOWWINDOW);
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		SetWindowSubclass(m_EditBox,SubclassEditProc,'CLSH',(mode==EDIT_HOTKEY)?1:((mode==EDIT_HOTKEY_ANY)?2:0));
	}
	else
		m_EditBox.ShowWindow(SW_HIDE);
}

void CTreeSettingsDlg::UpdateEditPosition( void )
{
	if (m_EditMode==EDIT_NONE) return;

	RECT rc;
	m_Tree.GetClientRect(&rc);
	int w=rc.right;
	TreeView_GetItemRect(m_Tree,m_EditItem,&rc,TRUE);
	rc.left=rc.right;
	rc.right=w;
	w-=rc.left;
	int width=(rc.bottom-rc.top)*3/2;

	HDC hdc=CreateCompatibleDC(NULL);
	HFONT font0=(HFONT)SelectObject(hdc,m_EditBox.GetFont());
	SIZE size;
	CString str;
	m_EditBox.GetWindowText(str);
	GetTextExtentPoint(hdc,str,str.GetLength(),&size);
	SelectObject(hdc,font0);
	DeleteDC(hdc);
	DWORD margins=(DWORD)m_EditBox.SendMessage(EM_GETMARGINS);
	size.cx+=HIWORD(margins)+LOWORD(margins)+12;
	// adjust size and position of edit boxes for settings that use browse/play buttons
	if (m_EditMode==EDIT_ICON || m_EditMode==EDIT_BITMAP || m_EditMode==EDIT_BITMAP_JPG || m_EditMode==EDIT_FONT || m_EditMode==EDIT_COLOR || m_EditMode==EDIT_DIRECTORY)
		size.cx+=width;
	if (m_EditMode==EDIT_SOUND)
		size.cx+=width*2;
	if (size.cx<w)
		rc.right=rc.left+size.cx;

	if (m_EditMode==EDIT_ICON || m_EditMode==EDIT_BITMAP || m_EditMode==EDIT_BITMAP_JPG || m_EditMode==EDIT_SOUND || m_EditMode==EDIT_FONT || m_EditMode==EDIT_COLOR || m_EditMode==EDIT_DIRECTORY)
	{
		RECT rc2=rc;
		rc2.left=rc2.right-width;
		if (m_EditMode==EDIT_SOUND)
		{
			m_PlayButton.SetWindowPos(NULL,&rc2,SWP_NOZORDER|SWP_NOCOPYBITS);
			rc2.left-=width;
			rc2.right-=width;
		}
		m_BrowseButton.SetWindowPos(NULL,&rc2,SWP_NOZORDER|SWP_NOCOPYBITS);
		rc.right=rc2.left;
	}

	m_EditBox.SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_NOCOPYBITS);
}

bool CTreeSettingsDlg::IsVisible( const CSetting *pSetting, const wchar_t *filter ) const
{
	if (pSetting->flags&CSetting::FLAG_HIDDEN)
		return false;
	if (!pSetting->MatchFilter(filter))
		return false;
	int style, mask;
	GetSettingsStyle(style,mask);
	if ((pSetting->flags&mask) && !(pSetting->flags&style))
		return false;
	if (pSetting->type==CSetting::TYPE_RADIO)
	{
		while (pSetting->type==CSetting::TYPE_RADIO)
			pSetting--;
		return IsVisible(pSetting,filter);
	}
	return true;
}

const CSetting *CTreeSettingsDlg::GetNextSetting( const CSetting *pSetting, const wchar_t *filter )
{
	pSetting++;
	while (pSetting->type==CSetting::TYPE_RADIO)
	{
		if (!(pSetting->flags&CSetting::FLAG_HIDDEN))
			return pSetting;
		pSetting++;
	}

	if (*filter)
	{
		while (pSetting->name && (pSetting->type==CSetting::TYPE_GROUP || !IsVisible(pSetting,filter) || !pSetting->MatchFilter(filter)))
		{
			if (pSetting->type==CSetting::TYPE_GROUP && (pSetting->flags&CSetting::FLAG_HIDDEN))
			{
				// if this is a hidden group skip to the next
				pSetting++;
				while (pSetting->type!=CSetting::TYPE_GROUP)
					pSetting++;
			}
			pSetting++;
		}
	}
	else if (m_bBasic)
	{
		while (pSetting->name && (pSetting->type==CSetting::TYPE_GROUP || !IsVisible(pSetting,filter) || !(pSetting->flags&CSetting::FLAG_BASIC)))
			pSetting++;
	}
	else
	{
		while (pSetting->name && pSetting->type!=CSetting::TYPE_GROUP && !IsVisible(pSetting,filter))
			pSetting++;
	}

	if (pSetting->name && pSetting->type!=CSetting::TYPE_GROUP)
		return pSetting;
	return NULL;
}

HTREEITEM CTreeSettingsDlg::GetNextItem( HTREEITEM hItem, bool bChildren )
{
	if (!hItem) return NULL;
	if (bChildren)
	{
		HTREEITEM hNext=TreeView_GetChild(m_Tree,hItem);
		if (hNext) return hNext;
	}
	{
		HTREEITEM hNext=TreeView_GetNextSibling(m_Tree,hItem);
		if (hNext) return hNext;
	}
	return GetNextItem(TreeView_GetParent(m_Tree,hItem),false);
}

HTREEITEM CTreeSettingsDlg::FindSettingsItem( const wchar_t *name )
{
	HTREEITEM res=NULL;
	for (HTREEITEM hItem=TreeView_GetRoot(m_Tree);hItem;hItem=GetNextItem(hItem))
	{
		TVITEM item={TVIF_PARAM,hItem};
		TreeView_GetItem(m_Tree,&item);
		const CSetting *pSetting=(CSetting*)item.lParam;
		if (pSetting && wcscmp(name,pSetting->name)==0)
			res=hItem;
	}
	return res;
}

void CTreeSettingsDlg::SetGroup( CSetting *pGroup, const CString &filter, const CSetting *pSelect )
{
	ItemSelected(NULL,NULL,false);
	m_pGroup=pGroup;
	// fill tree control
	m_Tree.SendMessage(WM_SETREDRAW,FALSE);
	TreeView_SelectItem(m_Tree,NULL);
	TreeView_DeleteAllItems(m_Tree);
	int level=0;
	HTREEITEM hRadioParent=NULL;
	m_bSearch=!filter.IsEmpty();
	m_bBasic=(pGroup->nameID==IDS_BASIC_SETTINGS);
	HTREEITEM hSelect=NULL;
	for (const CSetting *pSetting=GetNextSetting(pGroup,filter);pSetting;pSetting=GetNextSetting(pSetting,filter))
	{
		wchar_t text[256];
		text[0]=0;
		if (pSetting->type==CSetting::TYPE_BOOL || pSetting->type==CSetting::TYPE_RADIO || (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO))
		{
			// the text for BOOL, RADIO or RADIO's parent items doesn't change
			Strcpy(text,_countof(text),LoadStringEx(pSetting->nameID));
		}
		if (pSetting->type!=CSetting::TYPE_RADIO)
			hRadioParent=NULL;
		HTREEITEM hParent=hRadioParent;
		if (pSetting->nest)
		{
			Assert(pSetting->type!=CSetting::TYPE_RADIO);
			hParent=FindSettingsItem(pSetting->nest);
		}
		TVINSERTSTRUCT insert={hParent,TVI_LAST,{TVIF_STATE|TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_PARAM,NULL,TVIS_EXPANDED,TVIS_EXPANDED,text,0,SETTING_STATE_SETTING,SETTING_STATE_SETTING}};
		insert.item.lParam=(LPARAM)pSetting;
		if (pSetting->IsLocked())
		{
			insert.item.state|=INDEXTOOVERLAYMASK(1);
			insert.item.stateMask|=TVIS_OVERLAYMASK;
		}
		else if (pSetting->flags&CSetting::FLAG_WARNING)
		{
			insert.item.state|=INDEXTOOVERLAYMASK(2);
			insert.item.stateMask|=TVIS_OVERLAYMASK;
		}
		HTREEITEM hItem=TreeView_InsertItem(m_Tree,&insert);
		if (pSetting->type!=CSetting::TYPE_RADIO)
			hRadioParent=hItem;
		if (pSetting==pSelect)
			hSelect=hItem;
	}
	UpdateGroup(NULL);
	m_Tree.SendMessage(WM_SETREDRAW,TRUE);
	if (hSelect)
	{
		TreeView_SelectItem(m_Tree,hSelect);
		m_Tree.PostMessage(TVM_ENSUREVISIBLE,0,(LPARAM)hSelect); // post instead of send, because the tree may be resized afterwards
	}
	m_Tree.Invalidate();
}

void CTreeSettingsDlg::UpdateGroup( const CSetting *pModified )
{
	for (HTREEITEM hItem=TreeView_GetRoot(m_Tree);hItem;hItem=GetNextItem(hItem))
	{
		wchar_t text[256];
		TVITEM item={TVIF_PARAM|TVIF_STATE|TVIF_IMAGE,hItem,0,TVIS_BOLD|TVIS_CUT,text};
		TreeView_GetItem(m_Tree,&item);
		const CSetting *pSetting=(CSetting*)item.lParam;
		if (pSetting->type==CSetting::TYPE_RADIO) continue;

		// check if the item is enabled
		bool bEnabled=pSetting->IsEnabled();

		// check if the item is default
		bool bDefault=pSetting->IsDefault();
		const CComVariant &valVar=pSetting->GetValue();

		// check if modified items should be bold
		bool bBoldSettings=GetSettingBool(L"BoldSettings");

		// calculate text
		if (pSetting!=m_pEditSetting)
		{
			if (pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY)
			{
				wchar_t val[100];
				val[0]=0;
				if (valVar.vt==VT_I4)
					GetKeyName(valVar.intVal,val,_countof(val),false,false,false);
				CString str=LoadStringEx(pSetting->nameID);
				Sprintf(text,_countof(text),L"%s: %s",str,val);
				item.mask|=TVIF_TEXT;
			}
			else if (pSetting->type>=CSetting::TYPE_STRING)
			{
				CString str=LoadStringEx(pSetting->nameID);
				const wchar_t *val=L"";
				if (valVar.vt==VT_BSTR)
					val=valVar.bstrVal;
				Sprintf(text,_countof(text),L"%s: %s",str,val);
				item.mask|=TVIF_TEXT;
			}
			else if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type!=CSetting::TYPE_RADIO)
			{
				CString str=LoadStringEx(pSetting->nameID);
				int val=0;
				if (valVar.vt==VT_I4)
					val=valVar.intVal;
				Sprintf(text,_countof(text),L"%s: %d",str,val);
				item.mask|=TVIF_TEXT;
			}
			else if (pSetting->type==CSetting::TYPE_COLOR)
			{
				CString str=LoadStringEx(pSetting->nameID);
				int val=0;
				if (valVar.vt==VT_I4)
					val=BgrToRgb(valVar.intVal);
				Sprintf(text,_countof(text),L"%s: %06X",str,val);
				item.mask|=TVIF_TEXT;
			}
		}

		// calculate state
		int image=SETTING_STATE_SETTING;
		if (pSetting->type==CSetting::TYPE_BOOL)
			image=SETTING_STATE_CHECKBOX|(IsVariantTrue(valVar)?SETTING_STATE_CHECKED:0);
		if (pSetting->type==CSetting::TYPE_COLOR)
		{
			image=SETTING_IMAGE_COLOR+(pSetting->flags>>24);

			HIMAGELIST images=GetSettingsImageList(m_Tree);
			int cx, cy;
			ImageList_GetIconSize(images,&cx,&cy);

			BITMAPINFO dib={sizeof(dib)};
			dib.bmiHeader.biWidth=cx;
			dib.bmiHeader.biHeight=cy;
			dib.bmiHeader.biPlanes=1;
			dib.bmiHeader.biBitCount=32;
			dib.bmiHeader.biCompression=BI_RGB;
			HDC hdc=CreateCompatibleDC(NULL);
			HDC hdcMask=CreateCompatibleDC(NULL);
			HBITMAP bmp=CreateDIBSection(hdc,&dib,DIB_RGB_COLORS,NULL,NULL,0);
			HBITMAP bmpMask=CreateDIBSection(hdcMask,&dib,DIB_RGB_COLORS,NULL,NULL,0);

			HGDIOBJ bmp0=SelectObject(hdc,bmp);
			HGDIOBJ bmp1=SelectObject(hdcMask,bmpMask);
			SetDCBrushColor(hdc,valVar.intVal&0xFFFFFF);
			SetDCPenColor(hdc,0);
			SelectObject(hdc,GetStockObject(DC_BRUSH));
			SelectObject(hdc,GetStockObject(DC_PEN));
			Rectangle(hdc,0,0,cx,cy);
			RECT rc={0,0,cx,cy};
			FillRect(hdcMask,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
			SelectObject(hdc,bmp0);
			SelectObject(hdcMask,bmp1);
			ImageList_Replace(images,image,bmp,bmpMask);
			DeleteObject(bmp);
			DeleteObject(bmpMask);
			DeleteDC(hdc);
			DeleteDC(hdcMask);
		}
		int state=bDefault||!bBoldSettings?0:TVIS_BOLD; // check if item should be highlighted in bold
		if (!bEnabled)
		{
			if (pSetting->type!=CSetting::TYPE_COLOR) image|=SETTING_STATE_DISABLED;
			state|=TVIS_CUT;
		}
		if (item.iImage==image)
			item.mask&=~TVIF_IMAGE;
		else
			item.iImage=item.iSelectedImage=image;

		if ((item.state&(TVIS_BOLD|TVIS_CUT))==state)
			item.mask&=~TVIF_STATE;
		else
			item.state=state;
		if (item.mask!=0)
		{
			if (item.mask&TVIF_IMAGE)
				item.mask|=TVIF_SELECTEDIMAGE;
			TreeView_SetItem(m_Tree,&item);
			RECT rc;
			TreeView_GetItemRect(m_Tree,hItem,&rc,FALSE);
			m_Tree.InvalidateRect(&rc);
		}

		// update radio buttons
		if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO)
		{
			int val=0;
			if (valVar.vt==VT_I4)
				val=valVar.intVal;
			for (HTREEITEM hRadio=TreeView_GetChild(m_Tree,hItem);hRadio;hRadio=TreeView_GetNextSibling(m_Tree,hRadio))
			{
				TVITEM radio={TVIF_IMAGE|TVIF_PARAM|TVIF_STATE,hRadio,0,TVIS_CUT};
				TreeView_GetItem(m_Tree,&radio);
				int v=(int)((CSetting*)radio.lParam-pSetting-1);

				int image=SETTING_STATE_RADIO;
				if (v==val) image|=SETTING_STATE_CHECKED;
				int state=0;
				if (!bEnabled)
				{
					if (pSetting->type!=CSetting::TYPE_COLOR) image|=SETTING_STATE_DISABLED;
					state=TVIS_CUT;
				}

				if (radio.iImage!=image || radio.state!=state)
				{
					radio.iImage=radio.iSelectedImage=image;
					radio.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_STATE;
					radio.state=state;
					TreeView_SetItem(m_Tree,&radio);
					RECT rc;
					TreeView_GetItemRect(m_Tree,hRadio,&rc,FALSE);
					m_Tree.InvalidateRect(&rc);
				}
			}
		}
	}
}

class CDefaultSettingsPanel: public ISettingsPanel
{
public:
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent );
	void SetFilter( const CString &filter ) { m_Filter=filter; }
	void Select( const CSetting *pSelect ) { m_pSelect=pSelect; }

private:
	static CTreeSettingsDlg s_Dialog;
	CString m_Filter;
	const CSetting *m_pSelect;
};

CTreeSettingsDlg CDefaultSettingsPanel::s_Dialog;

ISettingsPanel *GetDefaultSettings( const CString *filter, const CSetting *pSelect )
{
	static CDefaultSettingsPanel panel;
	if (filter)
		panel.SetFilter(*filter);
	if (pSelect)
		panel.Select(pSelect);
	return &panel;
}

HWND CDefaultSettingsPanel::Create( HWND parent )
{
	if (!s_Dialog.m_hWnd)
		s_Dialog.Create(parent,LoadDialogEx(IDD_SETTINGSTREE));
	return s_Dialog.m_hWnd;
}

HWND CDefaultSettingsPanel::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	s_Dialog.SetGroup(pGroup,m_Filter,m_pSelect);
	m_pSelect=NULL;
	s_Dialog.SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	return s_Dialog.m_hWnd;
}

bool CDefaultSettingsPanel::Validate( HWND parent )
{
	s_Dialog.Validate();
	return true;
}

DWORD RgbToBgr(DWORD val)
{
	return ((val & 0xFF) << 16) | (val & 0xFF00) | ((val >> 16) & 0xFF);
}

DWORD BgrToRgb(DWORD val)
{
	return RgbToBgr(val);
}

DWORD ParseColor(const wchar_t* str)
{
	wchar_t* end;
	return wcstoul(str, &end, 16) & 0xFFFFFF;
}
