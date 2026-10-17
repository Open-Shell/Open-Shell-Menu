// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// ProgramsTree.cpp - contains the implementation of the programs tree for the Win7 style

#include "stdafx.h"
#include "ProgramsTree.h"
#include "MenuContainer.h"
#include "MetroLinkManager.h"
#include "Translations.h"
#include "Settings.h"
#include "ResourceHelper.h"
#include "FNVHash.h"
#include "LogManager.h"
#include "StartMenuDLL.h"
#include "SettingsUI.h"
#include <uxtheme.h>
#include <algorithm>

bool CProgramsTree::s_bFoldersFirst;

const wchar_t *ORDER_PREFIX=L"#";

CProgramsTree::CProgramsTree( void )
{
	m_RefCount=1;
	m_pOwner=NULL;
	m_ImageList=NULL;
	m_bHoverTimer=m_bTrackMouse=m_bRefreshPosted=m_bAutoSort=m_bDragApps=m_bInsertAfter=false;
	m_LastMousePos.x=m_LastMousePos.y=0;
	m_MinX=m_MaxX=m_RootX=0;
	m_DropLocation=DROP_NOWHERE;
	m_DragItem=m_DropTarget=m_HoverItem=NULL;
	m_TreeTheme=m_ScrollTheme=NULL;
	m_InsertMark=NULL;
}

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

void CProgramsTree::Create( CMenuContainer *pOwner )
{
	m_pOwner=pOwner;
	HWND hWnd=CreateWindowEx(0,WC_TREEVIEW,NULL,WS_CHILD|TVS_EDITLABELS|TVS_FULLROWSELECT|(CMenuContainer::s_TipHideTime?TVS_INFOTIP:0)|TVS_NOHSCROLL|TVS_SHOWSELALWAYS|TVS_NONEVENHEIGHT,0,0,0,0,pOwner->m_hWnd,NULL,g_Instance,NULL);
	TreeView_SetExtendedStyle(hWnd,TVS_EX_AUTOHSCROLL,TVS_EX_AUTOHSCROLL);
	const MenuSkin &skin=CMenuContainer::s_Skin;
	m_TreeTheme=OpenThemeData(hWnd,L"treeview");
	if (skin.BHasScrollbar)
		m_ScrollTheme=OpenThemeData(hWnd,L"scrollbar");

	const MenuSkin::ItemDrawSettings &settings=skin.ItemSettings[MenuSkin::PROGRAMS_TREE_ITEM];

	SendMessage(hWnd,WM_SETFONT,(LPARAM)settings.font,FALSE);

	TreeView_SetItemHeight(hWnd,settings.itemHeight);

	SubclassWindow(hWnd);
	m_ImageList=ImageList_Create(CItemManager::SMALL_ICON_SIZE+settings.iconPadding.left+settings.iconPadding.right,CItemManager::SMALL_ICON_SIZE,ILC_COLOR32|ILC_MASK|ILC_MIRROR,1,16);
	TreeView_SetImageList(hWnd,m_ImageList,TVSIL_NORMAL);
	TreeView_SetIndent(hWnd,TreeView_GetIndent(hWnd)+skin.Programs_indent);
	m_DragItem=NULL;
	m_DropLocation=DROP_NOWHERE;
	m_DropTarget=NULL;
	m_bRefreshPosted=false;
	m_pDropTargetProxy=new CDropTargetProxy(this);
	RegisterDragDrop(hWnd,m_pDropTargetProxy);
	m_LastMousePos.x=m_LastMousePos.y=-1;
	s_bFoldersFirst=GetSettingBool(L"FoldersFirst");

	HWND tooltip=TreeView_GetToolTips(m_hWnd);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,CMenuContainer::s_TipHideTime);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,CMenuContainer::s_TipShowTime);
	SendMessage(tooltip,TTM_SETDELAYTIME,TTDT_RESHOW,CMenuContainer::s_TipShowTime);
	SetWindowSubclass(tooltip,SubclassInfoTipProc,'CLSH',0);

	AddRef(); // this will be released in OnFinalMeessage
}

LRESULT CALLBACK CProgramsTree::CustomWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CProgramsTree *pThis=static_cast<CProgramsTree *>((CWindowImpl<CProgramsTree>*)hWnd);
	if (!pThis->m_ScrollTheme)
	{
		return WindowProc(hWnd,uMsg,wParam,lParam);
	}
	DWORD oldThread=g_CustomScrollbarThread;
	g_CustomScrollbarThread=GetCurrentThreadId();
	g_CustomScrollbarTheme=pThis->m_ScrollTheme;
	LRESULT res=WindowProc(hWnd,uMsg,wParam,lParam);
	g_CustomScrollbarThread=oldThread;
	if (!oldThread)
		g_CustomScrollbarTheme=NULL;
	return res;
}

LRESULT CProgramsTree::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_pDropTargetProxy->Reset();
	m_pDropTargetProxy=NULL;
	RevokeDragDrop(m_hWnd);
	ImageList_Destroy(m_ImageList);
	m_ImageList=NULL;
	if (m_TreeTheme)
	{
		CloseThemeData(m_TreeTheme);
		m_TreeTheme=NULL;
	}
	if (m_ScrollTheme)
	{
		CloseThemeData(m_ScrollTheme);
		m_ScrollTheme=NULL;
	}
	return 0;
}

LRESULT CProgramsTree::OnDeleteItem( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	// free data
	NMTREEVIEW *pItem=(NMTREEVIEW*)pnmh;
	delete (CTreeItem*)pItem->itemOld.lParam;
	return 0;
}

LRESULT CProgramsTree::OnItemExpanding( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	// generate child items
	NMTREEVIEW *pView=(NMTREEVIEW*)pnmh;
	if (pView->itemNew.state&TVIS_EXPANDEDONCE)
		return 0;
	CreateFolderItems(pView->itemNew.hItem);
	HTREEITEM hChild=TreeView_GetChild(m_hWnd,pView->itemNew.hItem);
	if (hChild && !TreeView_GetNextSibling(m_hWnd,hChild))
		PostMessage(TVM_EXPAND,TVE_EXPAND,(LPARAM)hChild);
	return 0;
}

LRESULT CProgramsTree::OnSingleExpand( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	return TVNRET_SKIPOLD;
}

LRESULT CProgramsTree::OnBeginDrag( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTREEVIEW *pDrag=(NMTREEVIEW*)pnmh;
	CTreeItem *pItem=(CTreeItem*)pDrag->itemNew.lParam;
	if (!pItem) return 0;
	m_DragItem=pDrag->itemNew.hItem;
	m_bDragApps=pItem->bApps;
	if (pItem->bApps)
	{
		m_pOwner->DragOutApps(pItem->pItemInfo1);
	}
	else
	{
		m_pOwner->DragTreeItem(pItem,pItem->bApp);
	}
	m_DropLocation=DROP_NOWHERE;
	m_DragItem=NULL;
	return 0;
}

LRESULT CProgramsTree::OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN)
	{
		HTREEITEM hItem;
		const CTreeItem *pItem=GetSelectedItem(hItem);
		if (!pItem) return 0;
		if (pItem->bFolder)
			TreeView_Expand(m_hWnd,hItem,TVE_TOGGLE);
		else if (pItem->pItemInfo1)
		{
			RECT rc;
			TreeView_GetItemRect(m_hWnd,hItem,&rc,FALSE);
			MapWindowPoints(m_pOwner->m_hWnd,&rc);
			CMenuContainer::ActivateData data;
			data.bProgramsTree=true;
			m_pOwner->ActivateTreeItem(pItem,rc,CMenuContainer::ACTIVATE_EXECUTE,NULL,&data);
		}
		return 0;
	}
	if (wParam==VK_TAB)
	{
		m_pOwner->SendMessage(WM_KEYDOWN,wParam,lParam);
		return 0;
	}

	// Del to delete, F2 to rename
	if (wParam==VK_DELETE || wParam==VK_F2)
	{
		HTREEITEM hItem;
		const CTreeItem *pItem=GetSelectedItem(hItem);
		if (!pItem) return 0;
		if (pItem->pItemInfo1)
		{
			RECT rc;
			TreeView_GetItemRect(m_hWnd,hItem,&rc,FALSE);
			MapWindowPoints(m_pOwner->m_hWnd,&rc);
			CMenuContainer::ActivateData data;
			data.bProgramsTree=true;
			m_pOwner->ActivateTreeItem(pItem,rc,wParam==VK_F2?CMenuContainer::ACTIVATE_RENAME:CMenuContainer::ACTIVATE_DELETE,NULL,&data);
			if (data.command==CMenuContainer::CMD_DELETE)
			{
				HTREEITEM hParent=TreeView_GetParent(m_hWnd,hItem);
				if (hParent)
				{
					TVITEM item={TVIF_PARAM,hParent};
					TreeView_GetItem(m_hWnd,&item);
					const CTreeItem *pParent=(CTreeItem*)item.lParam;
					PostRefreshMessage(pParent->pItemInfo1);
				}
				else
					PostRefreshMessage();
			}
			else if (data.command==CMenuContainer::CMD_RENAME)
			{
				if (data.pNewItemInfo)
					SaveRenamedOrder(hItem,data.pNewItemInfo);
				PostRefreshMessage(data.pNewItemInfo);
			}
		}
		return 0;
	}

	if (wParam==VK_ESCAPE)
	{
		m_pOwner->SetMenuMode(CMenuContainer::MODE_NORMAL);
		m_pOwner->SetHotItem(m_pOwner->m_ProgramButtonIndex);
		return 0;
	}

	HTREEITEM hSelect1=TreeView_GetSelection(m_hWnd);
	DWORD state1=hSelect1?TreeView_GetItemState(m_hWnd,hSelect1,TVIS_EXPANDED)&TVIS_EXPANDED:0;

	LRESULT res=DefWindowProc(uMsg,wParam,lParam);

	HTREEITEM hSelect2=TreeView_GetSelection(m_hWnd);
	DWORD state2=hSelect2?TreeView_GetItemState(m_hWnd,hSelect2,TVIS_EXPANDED)&TVIS_EXPANDED:0;

	if (hSelect1==hSelect2 && state1==state2)
	{
		if (wParam==VK_UP || wParam==VK_DOWN || wParam==VK_LEFT || wParam==VK_RIGHT)
		{
			m_pOwner->SendMessage(uMsg,wParam,lParam);
		}
	}
	return res;
}

LRESULT CProgramsTree::OnSysKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN)
	{
		HTREEITEM hItem;
		const CTreeItem *pItem=GetSelectedItem(hItem);
		if (!pItem) return 0;
		if (pItem->pItemInfo1)
		{
			RECT rc;
			TreeView_GetItemRect(m_hWnd,hItem,&rc,FALSE);
			MapWindowPoints(m_pOwner->m_hWnd,&rc);
			CMenuContainer::ActivateData data;
			data.bProgramsTree=true;
			m_pOwner->ActivateTreeItem(pItem,rc,CMenuContainer::ACTIVATE_PROPERTIES,NULL,&data);
		}
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	bHandled=(wParam==VK_TAB || wParam==VK_RETURN || wParam==VK_ESCAPE);
	return 0;
}

LRESULT CProgramsTree::OnSetFocus( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	for (std::vector<CMenuContainer*>::reverse_iterator it=CMenuContainer::s_Menus.rbegin();*it!=m_pOwner;++it)
		if (!(*it)->m_bDestroyed)
			(*it)->PostMessage(WM_CLOSE);
	bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	TVHITTESTINFO test={{(short)LOWORD(lParam),(short)HIWORD(lParam)}};
	if (m_LastMousePos.x!=-1 && (m_LastMousePos.x!=test.pt.x || m_LastMousePos.y!=test.pt.y) && TreeView_HitTest(m_hWnd,&test))
	{
		RECT rc;
		TreeView_GetItemRect(m_hWnd,test.hItem,&rc,FALSE);
		int bottom=rc.bottom;
		GetClientRect(&rc);
		if (bottom<=rc.bottom)
			TreeView_SelectItem(m_hWnd,test.hItem);

		if (!m_bHoverTimer)
		{
			SetTimer(TIMER_HOVER,CMenuContainer::s_HoverTime);
			m_bHoverTimer=true;
		}
		if (m_pOwner->m_Submenu<0)
			SetFocus();
		m_pOwner->SetHotItem(m_pOwner->m_ProgramTreeIndex);
	}
	m_LastMousePos=test.pt;

	if (!m_bTrackMouse)
	{
		TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,m_hWnd,0};
		TrackMouseEvent(&track);
		m_bTrackMouse=true;
	}

	bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnMouseLeave( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_pOwner->SetHotItem(-1);
	m_bTrackMouse=false;
	if (m_bHoverTimer)
	{
		KillTimer(TIMER_HOVER);
		m_bHoverTimer=false;
	}
	return 0;
}

LRESULT CProgramsTree::OnNcMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	TreeView_SelectItem(m_hWnd,NULL);
	if (!m_bHoverTimer)
	{
		SetTimer(TIMER_HOVER,CMenuContainer::s_HoverTime);
		m_bHoverTimer=true;
	}
	if (m_pOwner->m_Submenu<0)
		SetFocus();
	m_pOwner->SetHotItem(m_pOwner->m_ProgramTreeIndex);
	bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnNcMouseLeave( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_bTrackMouse=false;
	if (m_bHoverTimer)
	{
		KillTimer(TIMER_HOVER);
		m_bHoverTimer=false;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnNcLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	SetFocus();
	TreeView_SelectItem(m_hWnd,NULL);
	bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnMouseActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	SetFocus();
	if (CMenuContainer::s_bBehindTaskbar && CMenuContainer::s_TaskBar)
		m_pOwner->SetWindowPos(CMenuContainer::s_TaskBar,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
	return MA_NOACTIVATE;
}

LRESULT CProgramsTree::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==TIMER_HOVER)
	{
		KillTimer(TIMER_HOVER);
		m_bHoverTimer=false;
		RECT rc;
		GetWindowRect(&rc);
		if (PtInRect(&rc,CPoint(GetMessagePos())))
			SetFocus();
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (CMenuContainer::s_bNoContextMenu) return 0;
	HTREEITEM hItem;
	const CTreeItem *pItem=GetSelectedItem(hItem);
	if (pItem)
	{
		RECT rc;
		TreeView_GetItemRect(m_hWnd,hItem,&rc,FALSE);
		MapWindowPoints(m_pOwner->m_hWnd,&rc);
		CMenuContainer::ActivateData data;
		data.bProgramsTree=true;
		data.bExpanded=(TreeView_GetItemState(m_hWnd,hItem,TVIS_EXPANDED)&TVIS_EXPANDED)!=0;
		HTREEITEM hParent=TreeView_GetParent(m_hWnd,hItem);
		const CTreeItem *pParent=NULL;
		if (hParent)
		{
			TVITEM item={TVIF_PARAM,hParent};
			TreeView_GetItem(m_hWnd,&item);
			pParent=(CTreeItem*)item.lParam;
			data.parent=pParent->pItemInfo1->GetPidl();
			data.bAutoSort=pParent->bAutoSort;
		}
		else
		{
			data.parent=m_pOwner->m_Path1[0];
			data.bAutoSort=m_bAutoSort;
		}
		data.hTreeItem=hItem;
		m_pOwner->ActivateTreeItem(pItem,rc,CMenuContainer::ACTIVATE_MENU,NULL,&data);
		if (data.command==CMenuContainer::CMD_RENAME && data.pNewItemInfo)
			SaveRenamedOrder(hItem,data.pNewItemInfo);
		if (data.command==CMenuContainer::CMD_TOGGLE)
			TreeView_Expand(m_hWnd,hItem,TVE_TOGGLE);
		else if (data.command==CMenuContainer::CMD_DELETE || data.command==CMenuContainer::CMD_MARKOLD)
			PostRefreshMessage(pParent?pParent->pItemInfo1:NULL);
		else if (data.command==CMenuContainer::CMD_RENAME || data.command==CMenuContainer::CMD_NEWFOLDER)
			PostRefreshMessage(data.pNewItemInfo);
	}

	return 0;
}

LRESULT CProgramsTree::OnScroll( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (CMenuContainer::s_Skin.Programs_background==0)
		Invalidate();
	bHandled=FALSE;
	return 0;
}

bool CProgramsTree::GetInsertRect( RECT &rc ) const
{
	if (!m_InsertMark) return false;

	RECT itemRect;
	TreeView_GetItemRect(m_hWnd,m_InsertMark,&itemRect,TRUE);

	GetClientRect(&rc);
	rc.left=itemRect.left-CItemManager::SMALL_ICON_SIZE-3-CMenuContainer::s_Skin.ItemSettings[MenuSkin::PROGRAMS_TREE_ITEM].iconPadding.right;
	const POINT *sizes=CMenuContainer::s_Skin.GetArrowsBitmapSizes();
	int h=sizes[1].y;
	rc.top=(m_bInsertAfter?itemRect.bottom:itemRect.top)-h/2;
	rc.bottom=rc.top+h;
	return true;
}

LRESULT CProgramsTree::OnSetInsertMark( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	RECT rc;
	if (GetInsertRect(rc))
		InvalidateRect(&rc);
	m_InsertMark=(HTREEITEM)lParam;
	m_bInsertAfter=wParam!=0;
	if (GetInsertRect(rc))
		InvalidateRect(&rc);
	bHandled=FALSE;
	return 0;
}

LRESULT CProgramsTree::OnRefresh( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_bRefreshPosted=false;
	RefreshTree(NULL,(CItemManager::ItemInfo*)lParam);
	return 0;
}

// Subclass the renaming editbox to handle Esc and Enter
static LRESULT CALLBACK SubclassEditboxProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_GETDLGCODE)
		return DLGC_WANTALLKEYS;
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CProgramsTree::OnBeginLabelEdit( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	SetWindowSubclass(TreeView_GetEditControl(m_hWnd),SubclassEditboxProc,'CLSH',0);
/*	NMTVDISPINFO *pInfo=(NMTVDISPINFO*)pnmh;
	if (!pInfo->item.lParam || ((CTreeItem*)pInfo->item.lParam)->bSeparator)
		return TRUE;*/
	return FALSE;
}

LRESULT CProgramsTree::OnEndLabelEdit( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
/*	NMTVDISPINFO *pInfo=(NMTVDISPINFO*)pnmh;
	if (pInfo->item.pszText)
	{
		if (!*pInfo->item.pszText)
		{
			::MessageBox(m_hWnd,LoadStringEx(IDS_ERROR_EMPTY),LoadStringEx(IDS_ERROR_TITLE),MB_OK|MB_ICONERROR);
			return FALSE;
		}
		bool bValid=true;
		for (const wchar_t *c=pInfo->item.pszText;*c;c++)
		{
			if ((*c>='a' && *c<='z') || (*c>='A' && *c<='Z') || (*c>='0' && *c<='9') || *c=='_')
				continue;
			::MessageBox(m_hWnd,LoadStringEx(IDS_ERROR_ASCII),LoadStringEx(IDS_ERROR_TITLE),MB_OK|MB_ICONERROR);
			return FALSE;
		}
		wchar_t text[1024];
		if (m_pOwner->IsSeparator(pInfo->item.pszText))
		{
			Sprintf(text,_countof(text),LoadStringEx(IDS_ERROR_SEPARATOR),pInfo->item.pszText);
			::MessageBox(m_hWnd,text,LoadStringEx(IDS_ERROR_TITLE),MB_OK|MB_ICONERROR);
			return FALSE;
		}
		if (FindItemByName(NULL,pInfo->item.pszText,pInfo->item.hItem))
		{
			Sprintf(text,_countof(text),LoadStringEx(IDS_DUPLICATE_ITEM),pInfo->item.pszText);
			::MessageBox(m_hWnd,text,LoadStringEx(IDS_ERROR_TITLE),MB_OK|MB_ICONERROR);
			return FALSE;
		}
		((CTreeItem*)pInfo->item.lParam)->name=pInfo->item.pszText;
		return TRUE;
	}*/
	return FALSE;
}

LRESULT CProgramsTree::OnGetInfoTip( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVGETINFOTIP *pTip=(NMTVGETINFOTIP*)pnmh;
	const CTreeItem *pTreeItem=(CTreeItem*)pTip->lParam;
	if (pTreeItem && pTreeItem->pItemInfo1 && !pTreeItem->bFolder)
	{
		// get the tip from the shell
		CComPtr<IShellItem> pItem;
		if (FAILED(SHCreateItemFromIDList(pTreeItem->pItemInfo1->GetPidl(),IID_IShellItem,(void**)&pItem)))
			return 0;

		CComPtr<IQueryInfo> pQueryInfo;
		if (FAILED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IQueryInfo,(void**)&pQueryInfo)))
			return 0;

		CComString pText;
		if (FAILED(pQueryInfo->GetInfoTip(QITIPF_LINKNOTARGET,&pText)) || !pText)
			return 0;

		Strcpy(pTip->pszText,pTip->cchTextMax,pText);
	}
	return 0;
}

LRESULT CProgramsTree::OnClick( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	CPoint pt(GetMessagePos());
	ScreenToClient(&pt);
	TVHITTESTINFO test={pt};
	TreeView_HitTest(m_hWnd,&test);
	if (test.flags&TVHT_ONITEM|TVHT_ONITEMINDENT)
	{
		TreeView_SelectItem(m_hWnd,test.hItem);
		TVITEM item={TVIF_PARAM,test.hItem};
		TreeView_GetItem(m_hWnd,&item);
		const CTreeItem *pItem=(CTreeItem*)item.lParam;
		if (!pItem) return TRUE;
		if (pItem->bFolder)
			TreeView_Expand(m_hWnd,test.hItem,TVE_TOGGLE);
		else if (pItem->pItemInfo1)
		{
			RECT rc;
			TreeView_GetItemRect(m_hWnd,test.hItem,&rc,FALSE);
			MapWindowPoints(m_pOwner->m_hWnd,&rc);
			CMenuContainer::ActivateData data;
			data.bProgramsTree=true;
			m_pOwner->ActivateTreeItem(pItem,rc,CMenuContainer::ACTIVATE_EXECUTE,NULL,&data);
		}
	}
	return TRUE;
}

LRESULT CProgramsTree::OnRClick( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	if (CMenuContainer::s_bNoContextMenu) return 1;
	CPoint pt(GetMessagePos());
	TVHITTESTINFO test={pt};
	ScreenToClient(&test.pt);
	TreeView_HitTest(m_hWnd,&test);
	if (test.flags&TVHT_ONITEM|TVHT_ONITEMINDENT)
	{
		TreeView_SelectItem(m_hWnd,test.hItem);
		TVITEM item={TVIF_PARAM,test.hItem};
		TreeView_GetItem(m_hWnd,&item);
		const CTreeItem *pItem=(CTreeItem*)item.lParam;
		if (pItem)
		{
			RECT rc;
			TreeView_GetItemRect(m_hWnd,test.hItem,&rc,FALSE);
			MapWindowPoints(m_pOwner->m_hWnd,&rc);
			CMenuContainer::ActivateData data;
			data.bProgramsTree=true;
			data.bExpanded=(TreeView_GetItemState(m_hWnd,test.hItem,TVIS_EXPANDED)&TVIS_EXPANDED)!=0;
			HTREEITEM hParent=TreeView_GetParent(m_hWnd,test.hItem);
			const CTreeItem *pParent=NULL;
			if (hParent)
			{
				TVITEM item={TVIF_PARAM,hParent};
				TreeView_GetItem(m_hWnd,&item);
				pParent=(CTreeItem*)item.lParam;
				data.parent=pParent->pItemInfo1->GetPidl();
				data.bAutoSort=pParent->bAutoSort;
			}
			else
			{
				data.parent=m_pOwner->m_Path1[0];
				data.bAutoSort=m_bAutoSort;
			}
			data.hTreeItem=test.hItem;
			m_pOwner->ActivateTreeItem(pItem,rc,CMenuContainer::ACTIVATE_MENU,&pt,&data);
			if (data.command==CMenuContainer::CMD_RENAME && data.pNewItemInfo)
				SaveRenamedOrder(test.hItem,data.pNewItemInfo);
			if (data.command==CMenuContainer::CMD_TOGGLE)
				TreeView_Expand(m_hWnd,test.hItem,TVE_TOGGLE);
			else if (data.command==CMenuContainer::CMD_DELETE || data.command==CMenuContainer::CMD_MARKOLD)
				PostRefreshMessage(pParent?pParent->pItemInfo1:NULL);
			else if (data.command==CMenuContainer::CMD_RENAME || data.command==CMenuContainer::CMD_NEWFOLDER)
				PostRefreshMessage(data.pNewItemInfo);
		}
	}
	return 1;
}

LRESULT CProgramsTree::OnPaint( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	// use buffered paint to allow the tree control to appear on a transparent background
	PAINTSTRUCT ps;
	HDC hdc=BeginPaint(&ps);
	DrawTree(hdc,ps.rcPaint);
	EndPaint(&ps);
	return 0;
}

void CProgramsTree::DeleteAllItems( void )
{
	TreeView_DeleteAllItems(m_hWnd);
}

void CProgramsTree::ClearAllNewRec( HTREEITEM hParent )
{
	for (HTREEITEM hItem=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hItem;hItem=TreeView_GetNextSibling(m_hWnd,hItem))
	{
		TVITEM item={TVIF_PARAM,hItem};
		TreeView_GetItem(m_hWnd,&item);
		CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
		pTreeItem->bNew=false;
		ClearAllNewRec(hItem);
	}
}

void CProgramsTree::ClearAllNew( void )
{
	ClearAllNewRec(NULL);
}

void CProgramsTree::AddFirstFolder( std::vector<CTreeItem*> &items, IShellItem *pParent, bool bPrograms )
{
	unsigned int hash0Prog=CalcFNVHash(L"\\",FNV_HASH0);
	unsigned int hash0=FNV_HASH0;
	CShellItemEnumerator enumerator(pParent);
	if (!enumerator.IsValid()) return;

	CComPtr<IShellItem> pChild;
	CAbsolutePidl childPidl;
	while (enumerator.GetNext(pChild,childPidl))
	{
		SFGAOF itemFlags;
		if (SUCCEEDED(pChild->GetAttributes(SFGAO_FOLDER|SFGAO_HIDDEN,&itemFlags)))
		{
			if (itemFlags&SFGAO_HIDDEN)
				continue;
			CTreeItem *pItem=new CTreeItem();
			pItem->bFolder=(itemFlags&SFGAO_FOLDER)!=0;
			pItem->bEmpty=false;
			pItem->pItemInfo1=g_ItemManager.GetItemInfo(pChild,childPidl,CItemManager::INFO_SMALL_ICON|CItemManager::INFO_LINK|CItemManager::INFO_METRO);
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				if (pItem->pItemInfo1->IsMetroLink())
				{
					if (GetSettingBool(L"HideProgramsMetro") || _wcsicmp(pItem->pItemInfo1->GetAppid(),DESKTOP_APP_ID)==0)
						continue;
					CString name=pItem->pItemInfo1->GetMetroName();
					pItem->SetName(name,false);
					name.MakeUpper();
					pItem->nameHash=CalcFNVHash(name,hash0);
					pItem->nameHashProg=CalcFNVHash(name,hash0Prog);
				}
			}
			if (pItem->name.IsEmpty())
			{
				CComString pName;
				if (FAILED(pChild->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
					continue;
				pItem->SetName(pName,false);
				CComString pName2;
				if (SUCCEEDED(pChild->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName2)))
				{
					pName2.MakeUpper();
					pItem->nameHash=CalcFNVHash(pName2,hash0);
					pItem->nameHashProg=CalcFNVHash(pName2,hash0Prog);
				}
				else
				{
					pName.MakeUpper();
					pItem->nameHash=CalcFNVHash(pName,hash0);
					pItem->nameHashProg=CalcFNVHash(pName2,hash0Prog);
				}
			}
			if (pItem->bFolder)
				pItem->folderHash=CalcFNVHash(pItem->pItemInfo1->PATH,CalcFNVHash(ORDER_PREFIX));
			pItem->pItemInfo2=NULL;
			pItem->order=-1;
			pItem->bPrograms=bPrograms;
			pItem->bNew=g_ItemManager.IsNewProgram(pItem->pItemInfo1->GetPidl(),pItem->bFolder,false);
			items.push_back(pItem);
		}
	}
}

void CProgramsTree::AddSecondFolder( std::vector<CTreeItem*> &items, IShellItem *pParent, bool bPrograms )
{
	unsigned int hash0Prog=CalcFNVHash(L"\\",FNV_HASH0);
	unsigned int hash0=FNV_HASH0;
	CShellItemEnumerator enumerator(pParent);
	if (!enumerator.IsValid()) return;

	CComPtr<IShellItem> pChild;
	CAbsolutePidl childPidl;
	while (enumerator.GetNext(pChild,childPidl))
	{
		SFGAOF itemFlags;
		if (SUCCEEDED(pChild->GetAttributes(SFGAO_FOLDER|SFGAO_HIDDEN,&itemFlags)))
		{
			if (itemFlags&SFGAO_HIDDEN)
				continue;
			CTreeItem *pItem=new CTreeItem();
			pItem->bFolder=(itemFlags&SFGAO_FOLDER)!=0;
			pItem->bEmpty=false;
			pItem->pItemInfo1=g_ItemManager.GetItemInfo(pChild,childPidl,CItemManager::INFO_SMALL_ICON|CItemManager::INFO_LINK|CItemManager::INFO_METRO);
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				if (pItem->pItemInfo1->IsMetroLink())
				{
					if (GetSettingBool(L"HideProgramsMetro") || _wcsicmp(pItem->pItemInfo1->GetAppid(),DESKTOP_APP_ID)==0)
						continue;
					CString name=pItem->pItemInfo1->GetMetroName();
					pItem->SetName(name,false);
					name.MakeUpper();
					pItem->nameHash=CalcFNVHash(name,hash0);
					pItem->nameHashProg=CalcFNVHash(name,hash0Prog);
				}
			}
			if (pItem->name.IsEmpty())
			{
				CComString pName;
				if (FAILED(pChild->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
					continue;
				pItem->SetName(pName,false);
				CComString pName2;
				if (SUCCEEDED(pChild->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName2)))
				{
					pName2.MakeUpper();
					pItem->nameHash=CalcFNVHash(pName2,hash0);
					pItem->nameHashProg=CalcFNVHash(pName2,hash0Prog);
				}
				else
				{
					pName.MakeUpper();
					pItem->nameHash=CalcFNVHash(pName,hash0);
					pItem->nameHashProg=CalcFNVHash(pName2,hash0Prog);
				}
			}
			if (pItem->bFolder)
				pItem->folderHash=CalcFNVHash(pItem->pItemInfo1->PATH,CalcFNVHash(ORDER_PREFIX));
			pItem->pItemInfo2=NULL;
			pItem->order=-1;
			pItem->bPrograms=bPrograms;
			pItem->bNew=g_ItemManager.IsNewProgram(pItem->pItemInfo1->GetPidl(),pItem->bFolder,false);
			unsigned int nameHash=pItem->GetNameHash();
			for (std::vector<CTreeItem*>::iterator it=items.begin();it!=items.end();++it)
			{
				if ((*it)->GetNameHash()==nameHash)
				{
					(*it)->pItemInfo2=pItem->pItemInfo1;
					if (pItem->bNew)
						(*it)->bNew=true;
					delete pItem;
					pItem=NULL;
					break;
				}
			}
			if (pItem)
				items.push_back(pItem);
		}
	}
}

void CProgramsTree::AddMetroApps( std::vector<CTreeItem*> &items )
{
	unsigned int hash0Prog=CalcFNVHash(L"\\",FNV_HASH0);
	unsigned int hash0=FNV_HASH0;
	std::vector<MetroLink> links;
	GetMetroLinks(links,true);
	for (std::vector<MetroLink>::const_iterator it=links.begin();it!=links.end();++it)
	{
		const CItemManager::ItemInfo *pItemInfo=g_ItemManager.GetItemInfo(it->pItem,it->pidl,CItemManager::INFO_SMALL_ICON|CItemManager::INFO_LINK|CItemManager::INFO_METRO);
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		if (_wcsicmp(pItemInfo->GetAppid(),DESKTOP_APP_ID)==0)
			continue;
		CString name;
		if (pItemInfo->IsMetroLink())
			name=pItemInfo->GetMetroName();
		else
		{
			CComString pName;
			if (FAILED(it->pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)) || wcsncmp(pName,L"@{",2)==0)
				continue;
			name=pName;
		}
		CTreeItem *pItem=new CTreeItem();
		pItem->pItemInfo1=pItemInfo;
		pItem->SetName(name,false);
		name.MakeUpper();
		pItem->nameHash=CalcFNVHash(name,hash0);
		pItem->nameHashProg=CalcFNVHash(name,hash0Prog);
		pItem->bApp=true;
		pItem->order=-1;
		pItem->bNew=g_ItemManager.IsNewProgram(pItemInfo->GetPidl(),false,true);
		items.push_back(pItem);
	}

	ULONGLONG curTime;
	GetSystemTimeAsFileTime((FILETIME*)&curTime);
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu",KEY_WRITE)!=ERROR_SUCCESS)
		regKey.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu");
	regKey.SetQWORDValue(L"LastAppsTime",curTime);
}

void CProgramsTree::GetFolderItems( std::vector<CTreeItem*> &items, HTREEITEM hParent )
{
	CTreeItem *pTreeItem=NULL;
	if (hParent)
	{
		TVITEM item={TVIF_PARAM,hParent};
		TreeView_GetItem(m_hWnd,&item);
		pTreeItem=(CTreeItem*)item.lParam;
		if (pTreeItem->bApps)
		{
			AddMetroApps(items);
		}
		else
		{
			CComPtr<IShellItem> pFolder1, pFolder2;
			SHCreateItemFromIDList(pTreeItem->pItemInfo1->GetPidl(),IID_IShellItem,(void**)&pFolder1);
			if (!pFolder1) return;

			if (pTreeItem->pItemInfo2)
				SHCreateItemFromIDList(pTreeItem->pItemInfo2->GetPidl(),IID_IShellItem,(void**)&pFolder2);

			AddFirstFolder(items,pFolder1,false);

			if (pFolder2)
				AddSecondFolder(items,pFolder2,false);
			if (items.empty())
			{
				CTreeItem *pItem=new CTreeItem();
				pItem->bFolder=false;
				pItem->bEmpty=true;
				pItem->SetName(FindTranslation(L"Menu.Empty",L"(Empty)"),false);
				pItem->nameHash=pItem->nameHashProg=0;
				pItem->pItemInfo1=pItem->pItemInfo2=NULL;
				items.push_back(pItem);
			}
		}
	}
	else
	{
		CComPtr<IShellItem> pRoot;
		CAbsolutePidl pidl;
		if (GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_PINNED)
		{
			if (SUCCEEDED(ShGetKnownFolderIDList(FOLDERID_StartMenu,&pidl)))
			{
				SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pRoot);
				if (pRoot)
				{
					AddFirstFolder(items,pRoot,false);
					pRoot=NULL;
				}
				pidl.Clear();
			}
			if (!CMenuContainer::s_bNoCommonFolders && SUCCEEDED(ShGetKnownFolderIDList(FOLDERID_CommonStartMenu,&pidl)))
			{
				SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pRoot);
				if (pRoot)
				{
					AddSecondFolder(items,pRoot,false);
					pRoot=NULL;
				}
				pidl.Clear();
			}
		}
		if (SUCCEEDED(ShGetKnownFolderIDList(FOLDERID_Programs,&pidl)))
		{
			SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pRoot);
			if (pRoot)
			{
				AddFirstFolder(items,pRoot,true);
				pRoot=NULL;
				for (std::vector<CTreeItem*>::iterator it=items.begin();it!=items.end();++it)
				{
					if (ILIsEqual((*it)->pItemInfo1->GetPidl(),pidl))
					{
						items.erase(it);
						break;
					}
				}
			}
			pidl.Clear();
		}
		if (!CMenuContainer::s_bNoCommonFolders && SUCCEEDED(ShGetKnownFolderIDList(FOLDERID_CommonPrograms,&pidl)))
		{
			SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pRoot);
			if (pRoot)
			{
				AddSecondFolder(items,pRoot,true);
				pRoot=NULL;
				for (std::vector<CTreeItem*>::iterator it=items.begin();it!=items.end();++it)
				{
					if (ILIsEqual((*it)->pItemInfo1->GetPidl(),pidl))
					{
						items.erase(it);
						break;
					}
				}
			}
			pidl.Clear();
		}
		if (GetSettingBool(L"AllProgramsMetro"))
		{
			CTreeItem *pItem=new CTreeItem();
			pItem->bFolder=true;
			pItem->bApps=true;
			pItem->SetName(FindTranslation(L"Menu.Apps",L"Apps"),false);
			pItem->pItemInfo1=g_ItemManager.GetCustomIcon(L",2",CItemManager::ICON_SIZE_TYPE_SMALL);
			if (pItem->bFolder)
				pItem->folderHash=CalcFNVHash(L"?Apps",CalcFNVHash(ORDER_PREFIX));
			pItem->pItemInfo2=NULL;
			pItem->order=-1;
			pItem->bNew=g_ItemManager.HasNewApps(false);
			items.push_back(pItem);
		}
	}
	unsigned int folderHash=pTreeItem?pTreeItem->folderHash:CalcFNVHash(ORDER_PREFIX);

	// load item names from the registry
	std::vector<unsigned int> hashes;
	CRegKey regOrder;
	if (pTreeItem)
		pTreeItem->bAutoSort=false;
	else
		m_bAutoSort=false;
	if (regOrder.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order",KEY_READ)==ERROR_SUCCESS)
	{
		wchar_t name[100];
		{
			ULONG size=0;
			Sprintf(name,_countof(name),L"%08X",folderHash);
			regOrder.QueryBinaryValue(name,NULL,&size);
			if (size>0 && !(size&3))
			{
				hashes.resize(size/4);
				regOrder.QueryBinaryValue(name,&hashes[0],&size);
			}
		}
	}
	if (hashes.size()==1 && hashes[0]=='AUTO')
	{
		if (pTreeItem)
			pTreeItem->bAutoSort=true;
		else
			m_bAutoSort=true;
	}
	else
	{
		// assign each item an index based on its position in items. store in order
		for (std::vector<CTreeItem*>::iterator it=items.begin();it!=items.end();++it)
		{
			unsigned int hash=(*it)->GetNameHash();
			(*it)->order=32768;
			for (int i=0;i<(int)hashes.size();i++)
			{
				if (hashes[i]==hash)
				{
					(*it)->order=i;
					break;
				}
			}
		}
	}

	std::sort(items.begin(),items.end(),CmpTreeItems);
}

void CProgramsTree::CreateFolderItems( HTREEITEM hParent )
{
	std::vector<CTreeItem*> items;
	GetFolderItems(items,hParent);

	HTREEITEM hAfter=NULL;
	for (std::vector<CTreeItem*>::const_iterator it=items.begin();it!=items.end();++it)
	{
		TVINSERTSTRUCT insert={hParent,hAfter?hAfter:TVI_FIRST,{TVIF_CHILDREN|TVIF_PARAM|TVIF_TEXT}};
		insert.itemex.pszText=(LPWSTR)(LPCWSTR)(*it)->name;
		insert.itemex.cChildren=(*it)->bFolder?1:0;
		insert.itemex.lParam=(LPARAM)*it;
		hAfter=TreeView_InsertItem(m_hWnd,&insert);
	}
}

void CProgramsTree::CreateItems( void )
{
	TreeView_DeleteAllItems(m_hWnd);
	CreateFolderItems(NULL);
	RECT rc;
	TreeView_GetItemRect(m_hWnd,TreeView_GetRoot(m_hWnd),&rc,TRUE);
	m_RootX=rc.left;
}

void CProgramsTree::RefreshTree( HTREEITEM hParent, const CItemManager::ItemInfo *pSelectItem )
{
	std::vector<CTreeItem*> newItems;
	GetFolderItems(newItems,hParent);
	int index=0;
	HTREEITEM hAfter=NULL;
	for (HTREEITEM hItem=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hItem;)
	{
		TVITEM item={TVIF_PARAM|TVIF_STATE,hItem,0,TVIS_SELECTED};
		TreeView_GetItem(m_hWnd,&item);
		CTreeItem *pOldItem=(CTreeItem*)item.lParam;
		int nextIndex=-1;
		for (int i=index;i<(int)newItems.size();i++)
		{
			if (*pOldItem==*newItems[i])
			{
				pOldItem->bNew=newItems[i]->bNew;
				nextIndex=i;
				break;
			}
		}
		HTREEITEM hNext=TreeView_GetNextSibling(m_hWnd,hItem);
		if (nextIndex>=0)
		{
			for (int i=index;i<nextIndex;i++)
			{
				TVINSERTSTRUCT insert={hParent,hAfter?hAfter:TVI_FIRST,{TVIF_CHILDREN|TVIF_PARAM|TVIF_TEXT}};
				insert.itemex.pszText=(LPWSTR)(LPCWSTR)newItems[i]->name;
				insert.itemex.cChildren=newItems[i]->bFolder?1:0;
				insert.itemex.lParam=(LPARAM)newItems[i];
				hAfter=TreeView_InsertItem(m_hWnd,&insert);
			}
			delete newItems[nextIndex];
			index=nextIndex+1;
			hAfter=hItem;
		}
		else
		{
			if (item.state&TVIS_SELECTED)
				TreeView_SelectItem(m_hWnd,NULL);
			TreeView_DeleteItem(m_hWnd,hItem);
		}
		hItem=hNext;
	}
	for (int i=index;i<(int)newItems.size();i++)
	{
		TVINSERTSTRUCT insert={hParent,hAfter?hAfter:TVI_FIRST,{TVIF_CHILDREN|TVIF_PARAM|TVIF_TEXT}};
		insert.itemex.pszText=(LPWSTR)(LPCWSTR)newItems[i]->name;
		insert.itemex.cChildren=newItems[i]->bFolder?1:0;
		insert.itemex.lParam=(LPARAM)newItems[i];
		hAfter=TreeView_InsertItem(m_hWnd,&insert);
	}
	size_t count=0;
	for (HTREEITEM hItem=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hItem;hItem=TreeView_GetNextSibling(m_hWnd,hItem))
	{
		TVITEM item={TVIF_STATE|TVIF_PARAM,hItem,0,TVIS_EXPANDEDONCE};
		TreeView_GetItem(m_hWnd,&item);
		if (item.state&TVIS_EXPANDEDONCE)
			RefreshTree(hItem,pSelectItem);
		count++;
		if (pSelectItem && ((CTreeItem*)item.lParam)->pItemInfo1==pSelectItem)
			TreeView_SelectItem(m_hWnd,hItem);
	}
	Assert(count==newItems.size());
}

const CProgramsTree::CTreeItem *CProgramsTree::GetSelectedItem( HTREEITEM &hItem )
{
	hItem=TreeView_GetSelection(m_hWnd);
	if (!hItem) return NULL;
	TVITEM item={TVIF_PARAM,hItem};
	TreeView_GetItem(m_hWnd,&item);
	return (CTreeItem*)item.lParam;
}

bool CProgramsTree::CmpTreeItems( const CTreeItem *item1, const CTreeItem *item2 )
{
	if (item1->order!=item2->order) return item1->order<item2->order;
	if (!item1->bFolder && item2->bFolder) return !s_bFoldersFirst;
	if (item1->bFolder && !item2->bFolder) return s_bFoldersFirst;
	if (item1->bApps && !item2->bApps) return false;
	if (!item1->bApps && item2->bApps) return true;
	return CMenuContainer::CompareMenuString(item1->name,item2->name)<0;
}

void CProgramsTree::SelectItem( int y )
{
	SetFocus();
	RECT rcClient;
	GetClientRect(&rcClient);
	HTREEITEM hSelect=NULL, hLast=NULL;
	for (HTREEITEM hItem=TreeView_GetFirstVisible(m_hWnd);hItem;hItem=TreeView_GetNextVisible(m_hWnd,hItem))
	{
		RECT rc;
		TreeView_GetItemRect(m_hWnd,hItem,&rc,FALSE);
		if (rc.top<rcClient.top)
			continue;
		if (rc.bottom>rcClient.bottom)
			break;
		TVITEM item={TVIF_PARAM,hItem};
		TreeView_GetItem(m_hWnd,&item);
		const CTreeItem *pItem=(CTreeItem*)item.lParam;
		if (!hSelect)
			hSelect=hItem;
		else if (rc.top<=y && rc.bottom>y)
		{
			hSelect=hItem;
			break;
		}
		hLast=hItem;
	}
	if (!hSelect) hSelect=hLast;
	if (hSelect)
		TreeView_SelectItem(m_hWnd,hSelect);
}

void CProgramsTree::SelectFirst( void )
{
	SetFocus();
	HTREEITEM hSelect=TreeView_GetRoot(m_hWnd);
	if (hSelect)
		TreeView_SelectItem(m_hWnd,hSelect);
}

void CProgramsTree::SelectLast( void )
{
	SetFocus();
	HTREEITEM hSelect=TreeView_GetRoot(m_hWnd);
	if (!hSelect) return;
	while (1)
	{
		for (HTREEITEM hItem=hSelect;hItem;hItem=TreeView_GetNextSibling(m_hWnd,hItem))
			hSelect=hItem;
		if (!(TreeView_GetItemState(m_hWnd,hSelect,TVIS_EXPANDED)&TVIS_EXPANDED))
			break;

		HTREEITEM hChild=TreeView_GetChild(m_hWnd,hSelect);
		if (!hChild) break;
		hSelect=hChild;
	}
	TreeView_SelectItem(m_hWnd,hSelect);
}

// IDropTarget
HRESULT CProgramsTree::DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	CMenuContainer::s_bRightDrag=(grfKeyState&MK_RBUTTON)!=0;
	m_pOwner->m_DragImageWindow=m_hWnd;
	m_pOwner->m_DragImagePoint={pt.x,pt.y};
	m_pOwner->m_DragImageEffect=*pdwEffect;
	if (m_pOwner->m_pDropTargetHelper)
	{
		POINT p={pt.x,pt.y};
		m_pOwner->m_pDropTargetHelper->DragEnter(m_hWnd,pDataObj,&p,*pdwEffect);
	}
	m_pOwner->m_DragHoverTime=GetMessageTime()-10000;
	m_HoverItem=NULL;
	m_pOwner->m_pDragObject=pDataObj;
	CMenuContainer::RestoreActiveDragImage();
	return S_OK;
}

void CProgramsTree::GetDragEffect( DWORD &grfKeyState, DWORD *pdwEffect )
{
	grfKeyState&=MK_SHIFT|MK_CONTROL|MK_ALT;
	if (CMenuContainer::s_bNoDragDrop)
	{
		*pdwEffect=DROPEFFECT_NONE; // can't drop here
		return;
	}

	if (m_bDragApps)
	{
		*pdwEffect&=DROPEFFECT_MOVE;
		return;
	}

	// only accept known data formats
	FORMATETC format1={CMenuContainer::s_ShellFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	FORMATETC format2={CMenuContainer::s_ShellUrlFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	FORMATETC format3={CMenuContainer::s_MetroLinkFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	if (m_pOwner->m_pDragObject->QueryGetData(&format1)!=S_OK && m_pOwner->m_pDragObject->QueryGetData(&format2)!=S_OK && m_pOwner->m_pDragObject->QueryGetData(&format3)!=S_OK)
	{
		*pdwEffect=DROPEFFECT_NONE;
		return;
	}

	bool bDragApp=m_pOwner->m_pDragObject->QueryGetData(&format3)==S_OK; // dragging a wrapped metro link or the Apps folder
	bool bDropApps=false; // dropping in the Apps folder
	HTREEITEM hDropFolder=NULL;
	if (m_DropTarget && (m_DropLocation==DROP_BEFORE || m_DropLocation==DROP_AFTER || m_DropLocation==DROP_LAST))
		hDropFolder=TreeView_GetParent(m_hWnd,m_DropTarget);
	else
		hDropFolder=m_DropTarget;
	if (hDropFolder)
	{
		TVITEM item={TVIF_PARAM,hDropFolder};
		TreeView_GetItem(m_hWnd,&item);
		const CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
		bDropApps=pTreeItem->bApps;
	}

	if (CMenuContainer::s_pDragSource)
	{
		if (CMenuContainer::s_pDragSource->m_Items[CMenuContainer::s_pDragSource->m_DragIndex].id==MENU_RECENT)
			*pdwEffect&=DROPEFFECT_LINK; // dragging a recent item (allow only link)
		else if (!CMenuContainer::s_bRightDrag && grfKeyState==0 && CMenuContainer::s_bDragMovable)
			*pdwEffect&=(bDragApp && !bDropApps)?DROPEFFECT_LINK:DROPEFFECT_MOVE; // dragging normal item - default to move
	}

	if (bDragApp)
	{
		if (bDropApps)
			*pdwEffect&=(m_DragItem && hDropFolder && TreeView_GetParent(m_hWnd,m_DragItem)==hDropFolder)?DROPEFFECT_MOVE:DROPEFFECT_NONE; // dragging a metro link to Apps folder
		else
			*pdwEffect&=DROPEFFECT_LINK; // dragging a metro link to another folder
	}

	// handle keys
	if (!CMenuContainer::s_bRightDrag)
	{
		if (grfKeyState==MK_SHIFT)
			*pdwEffect&=DROPEFFECT_MOVE;
		if (grfKeyState==MK_CONTROL)
			*pdwEffect&=DROPEFFECT_COPY;
		if (grfKeyState==(MK_CONTROL|MK_SHIFT) || grfKeyState==MK_ALT)
			*pdwEffect&=DROPEFFECT_LINK;
	}
}

HRESULT CProgramsTree::DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	CMenuContainer::s_bRightDrag=(grfKeyState&MK_RBUTTON)!=0;
	POINT p={pt.x,pt.y};
	m_pOwner->m_DragImagePoint=p;
	m_pOwner->m_DragImageEffect=*pdwEffect;
	if (m_pOwner->m_pDropTargetHelper)
	{
		m_pOwner->m_pDropTargetHelper->DragOver(&p,*pdwEffect);
	}
	ScreenToClient(&p);

	if (m_bDragApps)
	{
		// scroll tree during dragging Apps
		if ((GetMessageTime()-m_pOwner->m_DragHoverTime)>200)
		{
			m_pOwner->m_DragHoverTime=GetMessageTime();
			RECT rc;
			GetClientRect(&rc);
			int h=TreeView_GetItemHeight(m_hWnd);
			if (p.y<h)
				SendMessage(WM_VSCROLL,SB_LINEUP);
			else if (p.y>rc.bottom-h)
				SendMessage(WM_VSCROLL,SB_LINEDOWN);
		}
	}

	TVHITTESTINFO test={p};
	if (TreeView_HitTest(m_hWnd,&test))
	{
		m_DropTarget=test.hItem;
	}
	else
	{
		HTREEITEM hLast=TreeView_GetLastVisible(m_hWnd);
		if (hLast)
		{
			RECT rc;
			TreeView_GetItemRect(m_hWnd,hLast,&rc,FALSE);
			if (test.pt.y>=rc.bottom)
			{
				// after the last visible item
				m_DropTarget=hLast;
			}
		}
	}

	const CTreeItem *pTreeItem=NULL;
	bool bAutoSort=false;
	if (m_DropTarget)
	{
		TVITEM item={TVIF_PARAM,m_DropTarget};
		TreeView_GetItem(m_hWnd,&item);
		pTreeItem=(CTreeItem*)item.lParam;
		if (m_DragItem)
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
			if (m_DropTarget)
			{
				HTREEITEM hParent=TreeView_GetParent(m_hWnd,m_DropTarget);
				if (hParent==TreeView_GetParent(m_hWnd,m_DragItem))
				{
					if (hParent)
					{
						TVITEM parent={TVIF_PARAM,hParent};
						TreeView_GetItem(m_hWnd,&parent);
						bAutoSort=((CTreeItem*)parent.lParam)->bAutoSort;
					}
					else
						bAutoSort=m_bAutoSort;
				}
			}
		}
	}
	if (m_DropTarget)
	{
		RECT rc;
		TreeView_GetItemRect(m_hWnd,m_DropTarget,&rc,FALSE);
		int dy=test.pt.y-rc.top;
		if (bAutoSort)
		{
			if (pTreeItem->bFolder && !m_bDragApps)
				m_DropLocation=DROP_INSIDE;
			else
				m_DropTarget=NULL;
		}
		else if (pTreeItem->bFolder && !m_bDragApps)
		{
			int h1=(rc.bottom-rc.top)/4;
			int h2=(rc.bottom-rc.top)-h1;
			if (dy<h1)
				m_DropLocation=DROP_BEFORE;
			else if (dy>=h2)
				m_DropLocation=DROP_AFTER;
			else
				m_DropLocation=DROP_INSIDE;
		}
		else if (m_bDragApps && TreeView_GetParent(m_hWnd,m_DropTarget))
			m_DropTarget=NULL;
		else
		{
			int h=(rc.bottom-rc.top)/2;
			m_DropLocation=(dy<h)?DROP_BEFORE:DROP_AFTER;
		}
	}

	GetDragEffect(grfKeyState,pdwEffect);

	// display drop location
	if (!m_DropTarget)
	{
		TreeView_SelectDropTarget(m_hWnd,NULL);
		TreeView_SetInsertMark(m_hWnd,NULL,FALSE);
		*pdwEffect=0;
	}
	else
	{
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

	if (!m_bDragApps)
	{
		// check if the hover delay is done and it's time to open the item
		if (m_DropTarget && m_DropTarget==m_HoverItem)
		{
			if ((GetMessageTime()-m_pOwner->m_DragHoverTime)>(int)CMenuContainer::s_HoverTime*4)
			{
				TreeView_Expand(m_hWnd,m_HoverItem,TVE_EXPAND);
			}
		}
		else
		{
			m_HoverItem=m_DropTarget;
			m_pOwner->m_DragHoverTime=GetMessageTime();
		}
	}
	return S_OK;
}

HRESULT CProgramsTree::DragLeave( void )
{
	m_pOwner->m_DragImageWindow=NULL;
	if (m_pOwner->m_pDropTargetHelper)
		m_pOwner->m_pDropTargetHelper->DragLeave();
	m_pOwner->m_pDragObject.Release();
	TreeView_SelectDropTarget(m_hWnd,NULL);
	TreeView_SetInsertMark(m_hWnd,NULL,FALSE);
	m_DropLocation=DROP_NOWHERE;
	return S_OK;
}

HRESULT CProgramsTree::Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	m_pOwner->m_DragImageWindow=NULL;
	if (CMenuContainer::s_pDragSource)
	{
		if (!CMenuContainer::s_pDragSource->m_bDestroyed)
			CMenuContainer::s_pDragSource->KillTimer(CMenuContainer::TIMER_DRAG);
	}
	GetDragEffect(grfKeyState,pdwEffect);
	m_pOwner->m_pDragObject.Release();

	if (m_pOwner->m_pDropTargetHelper)
	{
		POINT p={pt.x,pt.y};
		m_pOwner->m_pDropTargetHelper->Drop(pDataObj,&p,*pdwEffect);
	}

	std::vector<unsigned int> order;
	if (m_DropTarget && (m_DropLocation==DROP_BEFORE || m_DropLocation==DROP_AFTER || m_DropLocation==DROP_LAST))
	{
		// calculate new order
		HTREEITEM hParent=TreeView_GetParent(m_hWnd,m_DropTarget);
		size_t dropIndex=-1;
		unsigned int dragHash=0;
		if (m_DragItem)
		{
			TVITEM item={TVIF_PARAM,m_DragItem};
			TreeView_GetItem(m_hWnd,&item);
			const CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
			dragHash=hParent?pTreeItem->nameHash:pTreeItem->nameHashProg;
		}
		else
		{
			// get name from dragged item
			CString name;
			if (CMenuContainer::s_pDragSource && CMenuContainer::s_pDragSource->m_Items[CMenuContainer::s_pDragSource->m_DragIndex].pItemInfo)
			{
				const CItemManager::ItemInfo *pInfo=CMenuContainer::s_pDragSource->m_Items[CMenuContainer::s_pDragSource->m_DragIndex].pItemInfo;
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					if (pInfo->IsMetroLink())
						name=pInfo->GetMetroName();
				}
				if (name.IsEmpty())
				{
					CComPtr<IShellItem> pItem;
					SHCreateItemFromIDList(pInfo->GetPidl(),IID_IShellItem,(void**)&pItem);
					CComString pName;
					if (pItem && (SUCCEEDED(pItem->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName)) || SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName))))
						name=pName;
				}
			}
			else
			{
				// TODO: get name from data object
			}
			if (!name.IsEmpty())
			{
				name.MakeUpper();
				unsigned int hash0=hParent?FNV_HASH0:CalcFNVHash(L"\\",FNV_HASH0);
				dragHash=CalcFNVHash(name,hash0);
			}
		}
		for (HTREEITEM hItem=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hItem;hItem=TreeView_GetNextSibling(m_hWnd,hItem))
		{
			TVITEM item={TVIF_PARAM,hItem};
			TreeView_GetItem(m_hWnd,&item);
			const CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
			if (hItem==m_DropTarget && m_DropLocation==DROP_BEFORE)
				dropIndex=order.size();
			if (hItem!=m_DragItem)
				order.push_back(pTreeItem->GetNameHash());
			if (hItem==m_DropTarget && m_DropLocation==DROP_AFTER)
				dropIndex=order.size();
		}
		if (m_DropLocation==DROP_LAST)
			dropIndex=order.size();
		order.insert(order.begin()+dropIndex,dragHash);

		if (m_DragItem && hParent==TreeView_GetParent(m_hWnd,m_DragItem))
		{
			// dropped in the same folder, just reorder
			OrderElements(m_DragItem,hParent,order,false,true);
			CMenuContainer::PlayMenuSound(SOUND_DROP);
			m_DropTarget=NULL;
		}
	}

	// clear the insert mark
	TreeView_SelectDropTarget(m_hWnd,NULL);
	TreeView_SetInsertMark(m_hWnd,NULL,FALSE);
	TDropLocation dropLocation=m_DropLocation;
	m_DropLocation=DROP_NOWHERE;
	if (!m_DropTarget) return 0;

	// simulate dropping the object into the original folder
	CMenuContainer::PlayMenuSound(SOUND_DROP);

	if (dropLocation!=DROP_INSIDE)
	{
		// drop in parent folder
		m_DropTarget=TreeView_GetParent(m_hWnd,m_DropTarget);
		dropLocation=DROP_INSIDE;
	}

	CAbsolutePidl dropFolder;
	if (m_DropTarget)
	{
		// drop inside a folder
		TVITEM item={TVIF_PARAM,m_DropTarget};
		TreeView_GetItem(m_hWnd,&item);
		const CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
		dropFolder=pTreeItem->pItemInfo1->GetPidl();
	}
	else
	{
		// drop at top
		ShGetKnownFolderIDList(FOLDERID_Programs,&dropFolder);
	}
	if (!dropFolder) return E_FAIL;

	// must use IShellFolder to get to the drop target because the BindToHandler doesn't support passing the parent window (easily)
	CComPtr<IShellFolder> pDesktop;
	SHGetDesktopFolder(&pDesktop);
	CComPtr<IShellFolder> pFolder;
	CComPtr<IDropTarget> pTarget;
	if (!pDesktop || FAILED(pDesktop->BindToObject(dropFolder,NULL,IID_IShellFolder,(void**)&pFolder)) || FAILED(pFolder->CreateViewObject(g_OwnerWindow,IID_IDropTarget,(void**)&pTarget)))
		return S_OK;

	DWORD dwEffect=*pdwEffect;
	if (CMenuContainer::s_bRightDrag)
	{
		if (FAILED(pTarget->DragEnter(pDataObj,MK_RBUTTON|grfKeyState,pt,&dwEffect)))
			return E_FAIL;
		dwEffect=*pdwEffect;
		pTarget->DragOver(MK_RBUTTON|grfKeyState,pt,&dwEffect);
	}
	else
	{
		if (FAILED(pTarget->DragEnter(pDataObj,MK_LBUTTON|grfKeyState,pt,&dwEffect)))
			return E_FAIL;
		dwEffect=*pdwEffect;
		pTarget->DragOver(MK_LBUTTON|grfKeyState,pt,pdwEffect);
	}
	// Preserve the async mode negotiated by the data source.
	for (std::vector<CMenuContainer*>::iterator it=CMenuContainer::s_Menus.begin();it!=CMenuContainer::s_Menus.end();++it)
		if (!(*it)->m_bDestroyed)
			(*it)->EnableWindow(FALSE); // disable all menus
	bool bOld=CMenuContainer::s_bPreventClosing;
	CMenuContainer::s_bPreventClosing=true;
	m_pOwner->AddRef();
	pTarget->Drop(pDataObj,grfKeyState,pt,pdwEffect);
	CComQIPtr<IDataObjectAsyncCapability> async(pDataObj);
	BOOL asynchronous=FALSE;
	bool bAsyncDrop=async && SUCCEEDED(async->InOperation(&asynchronous)) && asynchronous;
	if (bAsyncDrop) CMenuContainer::KeepAfterAsyncDrop();
	CMenuContainer::s_bPreventClosing=bOld;
	for (std::vector<CMenuContainer*>::iterator it=CMenuContainer::s_Menus.begin();it!=CMenuContainer::s_Menus.end();++it)
		if (!(*it)->m_bDestroyed)
			(*it)->EnableWindow(TRUE); // enable all menus
	if (!m_pOwner->m_bDestroyed && !bAsyncDrop)
	{
		SetForegroundWindow(m_pOwner->m_hWnd);
		m_pOwner->SetActiveWindow();
		m_pOwner->SetHotItem(m_pOwner->m_ProgramTreeIndex);
		SetFocus();
	}
	if (!order.empty())
	{
		OrderElements(NULL,m_DropTarget,order,false,false);
	}
	PostRefreshMessage();
	m_pOwner->Release();

	return S_OK;
}

void CProgramsTree::PostRefreshMessage( const CItemManager::ItemInfo *pSelectItem )
{
	if (!m_bRefreshPosted && !m_pOwner->m_bDestroyed)
	{
		m_bRefreshPosted=true;
		PostMessage(TVM_REFRESH,0,(LPARAM)pSelectItem);
	}
}

int CALLBACK CProgramsTree::CmpTreeItemsCB( LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort )
{
	const CTreeItem *pTreeItem1=(CTreeItem*)lParam1;
	const CTreeItem *pTreeItem2=(CTreeItem*)lParam2;
	return pTreeItem1->order-pTreeItem2->order;
}

// reorders the tree elements and saves the order in registry
// bResort - resort the tree with the given order
void CProgramsTree::OrderElements( HTREEITEM hChild, HTREEITEM hParent, const std::vector<unsigned int> &order, bool bAutoSort, bool bResort )
{
	CTreeItem *pParentItem=NULL;
	unsigned int folderHash;
	if (hParent)
	{
		TVITEM item={TVIF_PARAM,hParent};
		TreeView_GetItem(m_hWnd,&item);
		pParentItem=(CTreeItem*)item.lParam;
		folderHash=pParentItem->folderHash;
	}
	else
		folderHash=CalcFNVHash(ORDER_PREFIX);

	if (order.empty())
	{
		if (pParentItem)
			pParentItem->bAutoSort=bAutoSort;
		else
			m_bAutoSort=bAutoSort;

		if (hChild)
		{
			TVITEM item={TVIF_PARAM,hChild};
			TreeView_GetItem(m_hWnd,&item);
			PostMessage(TVM_REFRESH,0,item.lParam);
		}
		else
			PostMessage(TVM_REFRESH);
	}
	else if (bResort)
	{
		// set item order
		for (HTREEITEM hItem=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hItem;hItem=TreeView_GetNextSibling(m_hWnd,hItem))
		{
			TVITEM item={TVIF_PARAM,hItem};
			TreeView_GetItem(m_hWnd,&item);
			CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
			pTreeItem->order=32768;
			unsigned int nameHash=pTreeItem->GetNameHash();
			for (int i=0;i<(int)order.size();i++)
			{
				if (nameHash==order[i])
				{
					pTreeItem->order=i;
					break;
				}
			}
		}
		// sort by order
		TVSORTCB sort={hParent,CmpTreeItemsCB,0};
		TreeView_SortChildrenCB(m_hWnd,&sort,0);
	}

	// save order
	CRegKey regOrder;
	wchar_t name[100];
	if (regOrder.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order")!=ERROR_SUCCESS)
		regOrder.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order");

	Sprintf(name,_countof(name),L"%08X",folderHash);
	if (bAutoSort)
	{
		DWORD cAuto='AUTO';
		regOrder.SetBinaryValue(name,&cAuto,4);
	}
	else if (order.empty())
		regOrder.SetBinaryValue(name,NULL,0);
	else
		regOrder.SetBinaryValue(name,&order[0],(int)order.size()*4);
}

// saves the item order by replacing the item with the new info
void CProgramsTree::SaveRenamedOrder( HTREEITEM hItem, const CItemManager::ItemInfo *pNewInfo )
{
	HTREEITEM hParent=TreeView_GetParent(m_hWnd,hItem);
	CTreeItem *pParentItem=NULL;
	unsigned int folderHash;
	if (hParent)
	{
		TVITEM item={TVIF_PARAM,hParent};
		TreeView_GetItem(m_hWnd,&item);
		pParentItem=(CTreeItem*)item.lParam;
		if (pParentItem->bAutoSort)
			return;
		folderHash=pParentItem->folderHash;
	}
	else
	{
		if (m_bAutoSort)
			return;
		folderHash=CalcFNVHash(ORDER_PREFIX);
	}

	std::vector<unsigned int> order;
	for (HTREEITEM hChild=hParent?TreeView_GetChild(m_hWnd,hParent):TreeView_GetRoot(m_hWnd);hChild;hChild=TreeView_GetNextSibling(m_hWnd,hChild))
	{
		TVITEM item={TVIF_PARAM,hChild};
		TreeView_GetItem(m_hWnd,&item);
		const CTreeItem *pTreeItem=(CTreeItem*)item.lParam;
		if (hChild==hItem)
		{
			CComPtr<IShellItem> pChild;
			if (FAILED(SHCreateItemFromIDList(pNewInfo->GetPidl(),IID_IShellItem,(void**)&pChild)))
				return;
			CComString pName;
			if (FAILED(pChild->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName)) && FAILED(pChild->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
				return;
			pName.MakeUpper();
			unsigned int hash0=pTreeItem->bPrograms?CalcFNVHash(L"\\",FNV_HASH0):FNV_HASH0;
			order.push_back(CalcFNVHash(pName,hash0));
		}
		else
			order.push_back(pTreeItem->GetNameHash());
	}
	if (order.empty())
		return;

	// save order
	CRegKey regOrder;
	wchar_t name[100];
	if (regOrder.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order")!=ERROR_SUCCESS)
		regOrder.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order");

	Sprintf(name,_countof(name),L"%08X",folderHash);
	regOrder.SetBinaryValue(name,&order[0],(int)order.size()*4);
}
