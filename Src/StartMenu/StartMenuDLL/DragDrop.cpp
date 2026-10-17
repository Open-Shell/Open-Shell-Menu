// ## MenuContainer.h
// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// DragDrop.cpp - handles the drag and drop functionality of CMenuContainer

#include "stdafx.h"
#include "MenuContainer.h"
#include "StartMenuDLL.h"
#include "SettingsUI.h"
#include "FNVHash.h"
#include "Settings.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include "FileHelper.h"
#include <algorithm>

// Let the native shell target negotiate its own asynchronous transfer. A
// synchronous file-conflict dialog can deadlock inside the OLE drag loop while
// its UI thread releases a COM reference back to this STA.
static HRESULT EnableShellAsyncDrop( IDataObject *pDataObj )
{
	CComQIPtr<IDataObjectAsyncCapability> pAsync(pDataObj);
	return pAsync?pAsync->SetAsyncMode(VARIANT_TRUE):E_NOINTERFACE;
}

static bool WasShellDropAsync( IDataObject *pDataObj )
{
	CComQIPtr<IDataObjectAsyncCapability> pAsync(pDataObj);
	BOOL asynchronous=FALSE;
	// InOperation tells the source that StartOperation was used; it is not
	// a completion event and must never be polled to wait for completion.
	return pAsync && SUCCEEDED(pAsync->InOperation(&asynchronous)) && asynchronous;
}

// Our icon cache contains premultiplied 32-bit DIBs. The Shell drag-image
// helper expects straight alpha and takes ownership of its bitmap on success.
static HRESULT InitializeMenuDragImage( IDragSourceHelper2 *helper, IDataObject *data, HBITMAP icon )
{
	if (!helper || !data || !icon) return E_INVALIDARG;
	HBITMAP copy=(HBITMAP)CopyImage(icon,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
	if (!copy) return E_OUTOFMEMORY;
	BITMAP bitmap={};
	if (!GetObject(copy,sizeof(bitmap),&bitmap) || !bitmap.bmBits || bitmap.bmBitsPixel!=32)
	{
		DeleteObject(copy);
		return E_FAIL;
	}
	for (int y=0;y<bitmap.bmHeight;y++)
	{
		DWORD *pixels=(DWORD*)((BYTE*)bitmap.bmBits+y*bitmap.bmWidthBytes);
		for (int x=0;x<bitmap.bmWidth;x++)
		{
			DWORD pixel=pixels[x],alpha=pixel>>24;
			if (alpha && alpha<255)
			{
				DWORD red=(std::min)(255UL,(((pixel>>16)&255)*255+alpha/2)/alpha);
				DWORD green=(std::min)(255UL,(((pixel>>8)&255)*255+alpha/2)/alpha);
				DWORD blue=(std::min)(255UL,((pixel&255)*255+alpha/2)/alpha);
				pixels[x]=(alpha<<24)|(red<<16)|(green<<8)|blue;
			}
			else if (!alpha)
				pixels[x]=0;
		}
	}
	SHDRAGIMAGE image={{bitmap.bmWidth,bitmap.bmHeight},{bitmap.bmWidth/2,bitmap.bmHeight},copy,CLR_NONE};
	helper->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);
	HRESULT result=helper->InitializeFromBitmap(&image,data);
	if (FAILED(result)) DeleteObject(copy);
	return result;
}

// A newly raised topmost submenu can cover the existing Shell drag window.
// Show(TRUE) and DragOver do not repair its Z order. Restart only the visual
// helper, preserving the actual OLE target and the source data object identity.
static HRESULT RestoreShellDragImage( IDropTargetHelper *helper, HWND target, IDataObject *data, const POINT &point, DWORD effect )
{
	if (!helper || !target || !data) return E_INVALIDARG;
	POINT position=point;
	helper->DragLeave();
	HRESULT result=helper->DragEnter(target,data,&position,effect);
	if (SUCCEEDED(result)) result=helper->DragOver(&position,effect);
	return result;
}

void CMenuContainer::RestoreActiveDragImage( void )
{
	for (auto menu : s_Menus)
		if (!menu->m_bDestroyed && menu->m_pDragObject && menu->m_pDropTargetHelper && menu->m_DragImageWindow)
		{
			CComPtr<IDataObject> data=menu->m_pDragObject;
			CComPtr<IDropTargetHelper> helper=menu->m_pDropTargetHelper;
			RestoreShellDragImage(helper,menu->m_DragImageWindow,data,menu->m_DragImagePoint,menu->m_DragImageEffect);
			break;
		}
}

class CMetroDataObject: public IDataObject
{
public:
	CMetroDataObject( const wchar_t *path );
	~CMetroDataObject( void );

	// from IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **ppvObject );
	virtual ULONG STDMETHODCALLTYPE AddRef( void )
	{
		return ++m_RefCount;
	}
	virtual ULONG STDMETHODCALLTYPE Release( void )
	{
		if (m_RefCount==1)
		{
			delete this;
			return 0;
		}
		return --m_RefCount;
	}

	// from IDataObject
	virtual HRESULT STDMETHODCALLTYPE GetData( FORMATETC *pformatetcIn, STGMEDIUM *pmedium );
	virtual HRESULT STDMETHODCALLTYPE SetData( FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease ) { return E_NOTIMPL; }
	virtual HRESULT STDMETHODCALLTYPE GetDataHere( FORMATETC *pformatetc, STGMEDIUM *pmedium ) { return E_NOTIMPL; }
	virtual HRESULT STDMETHODCALLTYPE QueryGetData( FORMATETC *pformatetc );
	virtual HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc( FORMATETC *pformatectIn, FORMATETC *pformatetcOut );
	virtual HRESULT STDMETHODCALLTYPE EnumFormatEtc( DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc );
	virtual HRESULT STDMETHODCALLTYPE DAdvise( FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection ) { return OLE_E_ADVISENOTSUPPORTED; }
	virtual HRESULT STDMETHODCALLTYPE DUnadvise( DWORD dwConnection ) { return E_NOTIMPL; }
	virtual HRESULT STDMETHODCALLTYPE EnumDAdvise( IEnumSTATDATA **ppenumAdvise ) { return OLE_E_ADVISENOTSUPPORTED; }

private:
	int m_RefCount;
	std::map<CLIPFORMAT,HGLOBAL> m_Data;
	bool m_bContents;

	void SetData( CLIPFORMAT format, HGLOBAL data );
};

static CLIPFORMAT g_PreferredEffectFormat;

CMetroDataObject::CMetroDataObject( const wchar_t *path )
{
	m_RefCount=0;
	m_bContents=false;
	SetData(CMenuContainer::s_MetroLinkFormat,NULL);

	if (!path) return;
	HANDLE file=CreateFile(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (file!=INVALID_HANDLE_VALUE)
	{
		DWORD size=GetFileSize(file,NULL);
		if (size>0)
		{
			HGLOBAL hContents=NULL, hDesc=NULL;
			hContents=GlobalAlloc(GMEM_MOVEABLE,size);
			if (hContents)
				hDesc=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(FILEGROUPDESCRIPTOR));
			if (hDesc)
			{
				FILEGROUPDESCRIPTOR *desc=(FILEGROUPDESCRIPTOR*)GlobalLock(hDesc);
				desc->cItems=1;
				desc->fgd->dwFlags=FD_ATTRIBUTES|FD_FILESIZE|FD_LINKUI|FD_UNICODE;
				desc->fgd->dwFileAttributes=FILE_ATTRIBUTE_NORMAL;
				desc->fgd->nFileSizeLow=size;
				Strcpy(desc->fgd->cFileName,_countof(desc->fgd->cFileName),PathFindFileName(path));
				GlobalUnlock(hDesc);
				SetData(CMenuContainer::s_DescriptorFormat,hDesc);
				hDesc=NULL;

				DWORD q;
				if (!ReadFile(file,GlobalLock(hContents),size,&q,NULL))
					q=0;
				GlobalUnlock(hContents);
				if (q==size)
				{
					SetData(CMenuContainer::s_ContentsFormat,hContents);
					hContents=NULL;
					m_bContents=true;
				}
			}
			if (hContents) GlobalFree(hContents);
			if (hDesc) GlobalFree(hDesc);
		}
		CloseHandle(file);
	}
}

CMetroDataObject::~CMetroDataObject( void )
{
	for (std::map<CLIPFORMAT,HGLOBAL>::iterator it=m_Data.begin();it!=m_Data.end();++it)
		if (it->second)
			GlobalFree(it->second);
}

HRESULT STDMETHODCALLTYPE CMetroDataObject::QueryInterface( REFIID riid, void **ppvObject )
{
	if (riid==IID_IDataObject || riid==IID_IUnknown)
	{
		AddRef();
		*ppvObject=this;
		return S_OK;
	}
	*ppvObject=NULL;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE CMetroDataObject::GetData( FORMATETC *pformatetcIn, STGMEDIUM *pmedium )
{
	if (!pformatetcIn || !pmedium)
		return E_INVALIDARG;

	pmedium->hGlobal=NULL;
	pmedium->pUnkForRelease=NULL;

	if (pformatetcIn->dwAspect!=DVASPECT_CONTENT) return DV_E_DVASPECT;
	if (!(pformatetcIn->tymed&TYMED_HGLOBAL)) return DV_E_TYMED;

	std::map<CLIPFORMAT,HGLOBAL>::iterator it=m_Data.find(pformatetcIn->cfFormat);
	if (it==m_Data.end()) return DV_E_FORMATETC;

	wchar_t name[100];
	GetClipboardFormatName(pformatetcIn->cfFormat,name,100);
	Trace(L"GetData: %s, %d",name,pformatetcIn->cfFormat);

	pmedium->tymed=TYMED_HGLOBAL;
	SIZE_T size=GlobalSize(it->second);
	pmedium->hGlobal=GlobalAlloc(GMEM_MOVEABLE,size);
	if (!pmedium->hGlobal) return E_OUTOFMEMORY;

	void *src=GlobalLock(it->second);
	void *dst=GlobalLock(pmedium->hGlobal);
	memcpy(dst,src,size);
	GlobalUnlock(pmedium->hGlobal);
	GlobalUnlock(it->second);
	return S_OK;
}

void CMetroDataObject::SetData( CLIPFORMAT format, HGLOBAL data )
{
	Assert(m_Data.find(format)==m_Data.end());
	m_Data[format]=data;
}

HRESULT STDMETHODCALLTYPE CMetroDataObject::QueryGetData( FORMATETC *pformatetc )
{
	if (!pformatetc) return E_INVALIDARG;
	if (pformatetc->dwAspect!=DVASPECT_CONTENT) return DV_E_DVASPECT;
	if (!(pformatetc->tymed&TYMED_HGLOBAL)) return DV_E_TYMED;
	if (m_Data.find(pformatetc->cfFormat)==m_Data.end()) return DV_E_CLIPFORMAT;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CMetroDataObject::GetCanonicalFormatEtc( FORMATETC *pformatectIn, FORMATETC *pformatetcOut )
{
	return pformatetcOut?DATA_S_SAMEFORMATETC:E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE CMetroDataObject::EnumFormatEtc( DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc )
{
	if (!ppenumFormatEtc) return E_POINTER;
	*ppenumFormatEtc=NULL;
	if (dwDirection == DATADIR_GET)
	{
		FORMATETC formats[] =
		{
			{CMenuContainer::s_MetroLinkFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},
			{CMenuContainer::s_DescriptorFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},
			{CMenuContainer::s_ContentsFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},
		};
		HRESULT hr = SHCreateStdEnumFmtEtc(m_bContents?3:1,formats,ppenumFormatEtc);
		return hr;
	}
	return E_NOTIMPL;
}

CComPtr<IDataObject> CMenuContainer::CreateMetroDataObject( const CItemManager::ItemInfo *pInfo )
{
	CString path;
	if (!pInfo->PATH.IsEmpty())
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		path=pInfo->GetPath();
	}
	IDataObject *pDataObjectIn=new CMetroDataObject(path);
	CComPtr<IDataObject> pDataObject;
	SHCreateDataObject(NULL,0,NULL,pDataObjectIn,IID_IDataObject,(void**)&pDataObject);
	if (pDataObject)
	{
		if (m_pDragSourceHelper)
		{
			g_ItemManager.UpdateItemInfo(pInfo,CItemManager::INFO_EXTRA_LARGE_ICON|CItemManager::INFO_REFRESH_NOW,false);
			if (pInfo->extraLargeIcon)
				InitializeMenuDragImage(m_pDragSourceHelper,pDataObject,pInfo->extraLargeIcon->bitmap);
		}
	}
	else
	{
		pDataObjectIn->Release();
	}
	return pDataObject;
}

///////////////////////////////////////////////////////////////////////////////

bool CMenuContainer::DragOutApps( const CItemManager::ItemInfo *pInfo )
{
	// drag the Apps tree item for reordering
	CComPtr<IDataObject> pDataObj=CreateMetroDataObject(pInfo);

	// do drag drop
	s_pDragSource=NULL;
	s_bDragFromTree=false;
	m_DragIndex=-1;
	s_bPreventClosing=true;
	m_DragTime=GetMessageTime();
	SetTimer(TIMER_DRAG,100);
	s_bDragClosed=false;
	DWORD dwEffect=DROPEFFECT_MOVE;
	HRESULT res=SHDoDragDrop(NULL,pDataObj,NULL,dwEffect,&dwEffect);

	s_pDragSource=NULL;
	s_bDragFromTree=false;
	if (!m_bDestroyed)
		KillTimer(TIMER_DRAG);
	s_bPreventClosing=false;

	if (s_bDragClosed)
	{
		for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			if (!(*it)->m_bDestroyed)
				(*it)->PostMessage(WM_CLOSE);
	}
	return true;
}

bool CMenuContainer::DragOut( int index, bool bApp )
{
	if (!(m_Options&CONTAINER_DRAG) || s_bNoDragDrop) return false;
	const MenuItem &item=m_Items[index];
	if (!item.pItem1 || (item.id!=MENU_NO && item.id!=MENU_RECENT)) return false;

	bool bLeft=(GetKeyState(VK_LBUTTON)<0);
	bool bRight=(GetKeyState(VK_RBUTTON)<0);
	if (!bLeft && !bRight) return false;

	CComPtr<IShellFolder> pFolder;
	PCUITEMID_CHILD child;

	// get IDataObject for the current item
	CComPtr<IDataObject> pDataObj;
	bool bProtectedLink=false;
	if (bApp && GetWinVersion()<WIN_VER_WIN10)
		pDataObj=CreateMetroDataObject(item.pItemInfo);
	else
	{
		if (FAILED(SHBindToParent(item.pItem1,IID_IShellFolder,(void**)&pFolder,&child)))
			return true;
		if (FAILED(pFolder->GetUIObjectOf(NULL,1,&child,IID_IDataObject,NULL,(void**)&pDataObj)))
			return true;

		{
			CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
			bProtectedLink=!m_bSubMenu || item.pItemInfo->IsProtectedLink();
		}
		if (m_pDragSourceHelper && item.pItemInfo)
		{
			// Reuse the icon already displayed by the menu. Do not extract a new
			// icon through Shell extensions or pump COM messages before the drag.
			MenuSkin::TItemDrawType drawType=index==m_ProgramTreeIndex?MenuSkin::PROGRAMS_TREE_ITEM:item.drawType;
			const CItemManager::IconInfo *icon=s_Skin.ItemSettings[drawType].iconSize==MenuSkin::ICON_SIZE_LARGE?
				item.pItemInfo->largeIcon:item.pItemInfo->smallIcon;
			if (icon) InitializeMenuDragImage(m_pDragSourceHelper,pDataObj,icon->bitmap);
		}
	}
	if (bProtectedLink)
	{
		// protected links default to DROPEFFECT_LINK
		HGLOBAL hGlobal=GlobalAlloc(GMEM_MOVEABLE,4);
		if (hGlobal)
		{
			*(DWORD*)GlobalLock(hGlobal)=DROPEFFECT_LINK;
			GlobalUnlock(hGlobal);
			FORMATETC format={s_PreferredEffectFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
			STGMEDIUM medium={TYMED_HGLOBAL};
			medium.hGlobal=hGlobal;
			if (FAILED(pDataObj->SetData(&format,&medium,TRUE)))
				GlobalFree(hGlobal);
		}
	}

	// This shell-created data object owns its asynchronous lifetime.
	EnableShellAsyncDrop(pDataObj);

	// do drag drop
	s_pDragSource=this;
	s_bDragFromTree=(index==m_ProgramTreeIndex);
	m_DragIndex=index;
	s_bDragMovable=(item.id==MENU_NO && index<m_OriginalCount) || (item.jumpIndex>=0 && s_JumpList.groups[LOWORD(item.jumpIndex)].type==CJumpGroup::TYPE_PINNED);

	s_bPreventClosing=true;
	m_DragTime=GetMessageTime();
	s_bDragClosed=false;
	SetTimer(TIMER_DRAG,100);
	DWORD dwEffect=DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK;
	HRESULT res=SHDoDragDrop(NULL,pDataObj,NULL,dwEffect,&dwEffect);
	bool bAsyncDrop=WasShellDropAsync(pDataObj);

	s_pDragSource=NULL;
	s_bDragFromTree=false;
	if (!m_bDestroyed)
		KillTimer(TIMER_DRAG);
	s_bDragMovable=false;
	s_bPreventClosing=false;

	if (s_bDragClosed)
	{
		for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			if (!(*it)->m_bDestroyed)
				(*it)->PostMessage(WM_CLOSE);
		return true;
	}

	if (bAsyncDrop)
	{
		// Leave the menu usable while the native shell owns the transfer.
		// Do not validate files early or take focus back from its dialog.
		KeepAfterAsyncDrop();
		QueueFolderRefresh();
		return true;
	}

	if (pFolder && res==DRAGDROP_S_DROP && !m_bDestroyed)
	{
		// check if the item still exists. refresh the menu if it doesn't
		SFGAOF flags=SFGAO_VALIDATE;
		if (FAILED(pFolder->GetAttributesOf(1,&child,&flags)))
		{
			SetActiveWindow();
			// close all submenus
			for (int i=(int)s_Menus.size()-1;s_Menus[i]!=this;i--)
				if (!s_Menus[i]->m_bDestroyed)
					s_Menus[i]->DestroyWindow();
			// update menu
			PostRefreshMessage();
		}
	}

	// activate the top non-destroyed menu
	for (int i=(int)s_Menus.size()-1;i>=0;i--)
		if (!s_Menus[i]->m_bDestroyed)
		{
			SetForegroundWindow(s_Menus[i]->m_hWnd);
			s_Menus[i]->SetActiveWindow();
			break;
		}

	return true;
}

void CMenuContainer::SetDropTip( IDataObject *pDataObj, bool bPin )
{
	DROPDESCRIPTION desc={bPin?DROPIMAGE_LINK:DROPIMAGE_INVALID};
	Strcpy(desc.szMessage,_countof(desc.szMessage),bPin?FindTranslation(L"Menu.PinStart",L"Pin to Start menu"):L"");
	HGLOBAL hDesc=GlobalAlloc(GMEM_MOVEABLE,sizeof(desc));
	if (hDesc)
	{
		memcpy(GlobalLock(hDesc),&desc,sizeof(desc));
		GlobalUnlock(hDesc);
		FORMATETC fmte={CMenuContainer::s_DropDescriptionFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
		STGMEDIUM medium={};
		medium.tymed=TYMED_HGLOBAL;
		medium.hGlobal=hDesc;

		if (FAILED(pDataObj->SetData(&fmte,&medium,TRUE)))
		{
			GlobalFree(hDesc);
		}
	}
}

void CMenuContainer::GetDragEffect( DWORD &grfKeyState, DWORD *pdwEffect )
{
	grfKeyState&=MK_SHIFT|MK_CONTROL|MK_ALT;
	if (s_bNoDragDrop || !(m_Options&CONTAINER_DROP))
	{
		*pdwEffect=DROPEFFECT_NONE; // can't drop here
		return;
	}
	if (!m_pDropFolder[0] && !(s_pDragSource==this && s_bDragMovable && !s_bDragFromTree))
	{
		*pdwEffect=DROPEFFECT_NONE; // can't drop here
		return;
	}

	// only accept known data formats
	FORMATETC format1={s_ShellFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	FORMATETC format2={s_ShellUrlFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	FORMATETC format3={s_MetroLinkFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	if (m_pDragObject->QueryGetData(&format1)!=S_OK && m_pDragObject->QueryGetData(&format2)!=S_OK && m_pDragObject->QueryGetData(&format3)!=S_OK)
	{
		*pdwEffect=DROPEFFECT_NONE;
		return;
	}

	if (s_pDragSource)
	{
		if (s_pDragSource->m_Items[s_pDragSource->m_DragIndex].id==MENU_RECENT)
			*pdwEffect&=DROPEFFECT_LINK; // dragging a recent item (allow only link)
		else if (grfKeyState==0 && !s_bRightDrag && s_pDragSource==this && s_bDragMovable && !s_bDragFromTree)
			*pdwEffect&=DROPEFFECT_MOVE; // dragging within the same menu - use move by default
		else if (grfKeyState==0 && !s_bRightDrag)
			*pdwEffect&=(s_bDragMovable && m_bSubMenu && (s_pDragSource->m_Options&CONTAINER_PROGRAMS))?DROPEFFECT_MOVE:DROPEFFECT_LINK; // dragging normal item to a different menu - default to move or link
	}

	if (m_pDragObject->QueryGetData(&format3)==S_OK)
	{
		if (m_Options&CONTAINER_APPS)
			*pdwEffect&=(s_pDragSource==this && !s_bDragFromTree)?DROPEFFECT_MOVE:DROPEFFECT_NONE; // dragging a metro link to Apps folder
		else
			*pdwEffect&=DROPEFFECT_LINK; // dragging a metro link to another folder
	}

	// handle keys
	if (!s_bRightDrag)
	{
		if (grfKeyState==MK_SHIFT)
			*pdwEffect&=DROPEFFECT_MOVE;
		if (grfKeyState==MK_CONTROL)
			*pdwEffect&=DROPEFFECT_COPY;
		if (grfKeyState==(MK_CONTROL|MK_SHIFT) || grfKeyState==MK_ALT)
			*pdwEffect&=DROPEFFECT_LINK;
	}
	else if (!m_bSubMenu && grfKeyState==0 && (*pdwEffect&DROPEFFECT_LINK))
	{
		// when a file is dragged to the start menu he usually wants to make a shortcut
		// so when right-dragging, and linking is allowed, make it the default
		grfKeyState=MK_SHIFT|MK_CONTROL;
	}
}

HRESULT STDMETHODCALLTYPE CMenuContainer::DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	s_bRightDrag=(grfKeyState&MK_RBUTTON)!=0;
	m_pDragObject=pDataObj;
	m_DragImageWindow=m_hWnd;
	m_DragImagePoint={pt.x,pt.y};
	m_DragImageEffect=*pdwEffect;
	if (m_pDropTargetHelper)
	{
		POINT p={pt.x,pt.y};
		m_pDropTargetHelper->DragEnter(m_hWnd,m_pDragObject,&p,*pdwEffect);
	}
	if (!m_bSubMenu && !s_bShowTopEmpty)
	{
		// when dragging over the main menu, show an (Empty) item at the top so the user can drop items there
		for (size_t i=0;i<m_Items.size();i++)
			if (m_Items[i].id==MENU_EMPTY_TOP)
			{
				s_bShowTopEmpty=true;
				if (m_ScrollHeight>0 && m_FolderHash[0])
					s_MenuScrolls[m_FolderHash[0]]=m_ScrollOffset;
				else
					s_MenuScrolls.erase(m_FolderHash[0]);
				InitWindow();
				break;
			}
	}
	m_DragHoverTime=GetMessageTime()-10000;
	m_DragHoverItem=-1;
	// Also cover entry into an already raised topmost menu.
	if (m_pDropTargetHelper)
		RestoreShellDragImage(m_pDropTargetHelper,m_hWnd,m_pDragObject,m_DragImagePoint,*pdwEffect);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CMenuContainer::DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	POINT p0={pt.x,pt.y};
	m_DragImagePoint=p0;
	m_DragImageEffect=*pdwEffect;
	if (m_pDropTargetHelper)
		m_pDropTargetHelper->DragOver(&p0,*pdwEffect);

	POINT p=p0;
	ScreenToClient(&p);
	int index=HitTest(p,NULL,true);

	int mark=-1;
	bool bAfter=false;

	const CItemManager::ItemInfo *pInfo=NULL;
	RECT rcItem;
	if (index>=0)
	{
		GetItemRect(index,rcItem);
		if (!m_bSubMenu && index<m_OriginalCount && (!s_pDragSource || s_bDragFromTree || s_bDragMovable || m_Items[index].id!=MENU_RECENT))
		{
			int h=(rcItem.bottom-rcItem.top)/4;
			if (m_Items[index].id!=MENU_NO || (p.y>=rcItem.top+h && p.y<rcItem.bottom-h))
				pInfo=m_Items[index].pItemInfo;
		}
	}
	int dropTargetIndex=pInfo?index:-1;
	if (m_pDropTargetInfo!=pInfo)
	{
		if (m_pDropTarget)
		{
			m_pDropTarget->DragLeave();
			m_pDropTarget=NULL;
		}
		if (pInfo && pInfo->GetPidl())
		{
			m_pDropTargetInfo=pInfo;
			CComPtr<IShellItem> pItem;
			SHCreateItemFromIDList(pInfo->GetPidl(),IID_IShellItem,(void**)&pItem);
			if (pItem)
			{
				pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IDropTarget,(void**)&m_pDropTarget);
				if (m_pDropTarget)
				{
					m_pDropTarget->DragEnter(m_pDragObject,grfKeyState,pt,pdwEffect);
					if (*pdwEffect==0)
						m_pDropTargetInfo=NULL;
				}
			}
		}
		else
			m_pDropTargetInfo=NULL;
	}
	if (!m_pDropTargetInfo)
	{
		dropTargetIndex=-1;
		m_pDropTarget=NULL;
	}
	if (dropTargetIndex!=m_DropTargetIndex)
	{
		InvalidateItem(dropTargetIndex);
		InvalidateItem(m_DropTargetIndex);
		m_DropTargetIndex=dropTargetIndex;
	}
	if (m_pDropTarget)
	{
		SetDropTip(m_pDragObject,false);
		SetInsertMark(-1,false);
		HRESULT result=m_pDropTarget->DragOver(grfKeyState,pt,pdwEffect);
		m_DragImageEffect=*pdwEffect;
		return result;
	}

	s_bRightDrag=(grfKeyState&MK_RBUTTON)!=0;
	GetDragEffect(grfKeyState,pdwEffect);

	if (index>=0 && index<m_OriginalCount && m_Items[index].id!=MENU_RECENT)
	{
		// set the new insert mark
		mark=index;
		int y=(rcItem.top+rcItem.bottom)/2;
		if (p.y<y)
		{
			// insert above
			if (m_Items[index].id!=MENU_NO && m_Items[index].id!=MENU_EMPTY && m_Items[index].id!=MENU_EMPTY_TOP && (index==0 || m_Items[index-1].id!=MENU_NO))
				mark=-1;
		}
		else
		{
			// insert below
			bAfter=true;
			if (m_Items[index].id!=MENU_NO && m_Items[index].id!=MENU_EMPTY && m_Items[index].id!=MENU_EMPTY_TOP && (index==m_Items.size()-1 || m_Items[index+1].id!=MENU_NO))
				mark=-1;
		}
		if (mark==-1 && m_Items[index].bFolder && (m_Items[index].bPrograms || m_Items[index].id==MENU_NO))
		{
			SetHotItem(index);
		}
		else
		{
			SetHotItem(-1);
		}
		if (!*pdwEffect || ((m_Options&CONTAINER_AUTOSORT) && s_pDragSource==this && !s_bDragFromTree && s_bDragMovable))
			mark=-1;
	}
	else if (s_pDragSource==this && !s_bDragFromTree && s_bDragMovable && m_Items[m_DragIndex].jumpIndex>=0 && index>=0 && m_Items[index].jumpIndex>=0)
	{
		int groupIndex=LOWORD(m_Items[m_DragIndex].jumpIndex);
		if (s_JumpList.groups[groupIndex].type==CJumpGroup::TYPE_PINNED && LOWORD(m_Items[index].jumpIndex)==groupIndex && (*pdwEffect&DROPEFFECT_MOVE))
		{
			// reorder jump list
			*pdwEffect=DROPEFFECT_MOVE;
			int y=(rcItem.top+rcItem.bottom)/2;
			bAfter=p.y>=y;
			SetHotItem(-1);
		}
		else
			index=-1;
		mark=index;
	}
	else
	{
		if (!m_bSubMenu && index<m_OriginalCount && PtInRect(&m_rContent,p) && (s_pDragSource!=this || s_bDragFromTree || !s_bDragMovable || m_Items[m_DragIndex].id==MENU_RECENT))
		{
			if (s_bWin7Style)
			{
				for (int i=0;i<m_ScrollCount;i++)
				{
					if (m_Items[i].id==MENU_NO || m_Items[i].id==MENU_EMPTY || m_Items[i].id==MENU_EMPTY_TOP)
						mark=i;
				}
			}
			else
				mark=m_ScrollCount-1;
			bAfter=true;
		}
		index=-1;
	}
	SetInsertMark(mark,bAfter);
	UpdateScroll(&p,false);

	// check if the hover delay is done and it's time to open the item
	if (index>=0 && index==m_DragHoverItem)
	{
		int hoverTime=(int)s_HoverTime;
		if (m_Items[index].id==MENU_PROGRAMS && GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE)
			hoverTime=(int)s_ProgramsHoverTime;
		if ((GetMessageTime()-m_DragHoverTime)>hoverTime && m_Submenu!=m_DragHoverItem)
		{
			// expand m_DragHoverItem
			if (index<m_OriginalCount && !m_Items[index].bHasJumpList && (!m_Items[index].bFolder || m_Items[index].pItem1))
			{
				ActivateData data;
				data.bNoModifiers=GetKeyState(VK_SHIFT)<0;
				ActivateItem(index,ACTIVATE_OPEN,NULL,&data);
			}
			if (!m_Items[index].bFolder)
				SetHotItem(-1);
			m_DragHoverTime=GetMessageTime();
		}
	}
	else
	{
		m_DragHoverItem=index;
		m_DragHoverTime=GetMessageTime();
	}

	if (m_InsertMark<0)
		*pdwEffect=0;

	int before=m_InsertMark;
	if (before>=0 && m_bInsertAfter && (before!=0 || (m_Items[0].id!=MENU_EMPTY && m_Items[0].id!=MENU_EMPTY_TOP)))
		before++;
	if (before>=0 && !m_bSubMenu && (*pdwEffect&DROPEFFECT_LINK) && (s_pDragSource!=this || s_bDragFromTree || !s_bDragMovable))
	{
		*pdwEffect=DROPEFFECT_LINK;
		SetDropTip(m_pDragObject,true);
	}
	else
		SetDropTip(m_pDragObject,false);
	m_DragImageEffect=*pdwEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CMenuContainer::DragLeave( void )
{
	m_DragImageWindow=NULL;
	if (m_pDropTarget)
	{
		m_pDropTarget->DragLeave();
		m_pDropTarget=NULL;
	}
	InvalidateItem(m_DropTargetIndex);
	m_DropTargetIndex=-1;
	m_pDropTargetInfo=NULL;
	if (m_pDropTargetHelper)
		m_pDropTargetHelper->DragLeave();
	SetDropTip(m_pDragObject,false);
	SetInsertMark(-1,false);
	m_pDragObject=NULL;
	UpdateScroll(NULL,false);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CMenuContainer::Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	m_DragImageWindow=NULL;
	if (s_pDragSource)
	{
		if (!s_pDragSource->m_bDestroyed)
			s_pDragSource->KillTimer(TIMER_DRAG);
	}
	if (m_pDropTargetHelper)
	{
		POINT p={pt.x,pt.y};
		m_pDropTargetHelper->Drop(pDataObj,&p,*pdwEffect);
	}
	if (m_pDropTarget)
	{
		m_pDragObject=NULL;
		HRESULT res=m_pDropTarget->Drop(pDataObj,grfKeyState,pt,pdwEffect);
		if (WasShellDropAsync(pDataObj))
		{
			KeepAfterAsyncDrop();
			QueueFolderRefresh();
		}
		m_pDropTarget=NULL;
		return res;
	}

	GetDragEffect(grfKeyState,pdwEffect);
	m_pDragObject=NULL;

	int before=m_InsertMark;
	if (before<0) return S_OK;
	if (before>=0 && m_bInsertAfter && (before!=0 || (m_Items[0].id!=MENU_EMPTY && m_Items[0].id!=MENU_EMPTY_TOP)))
		before++;

	// clear the insert mark
	SetInsertMark(-1,false);

	int folderIndex=0;
	if (before>=0)
		folderIndex=m_Items[min(before,(int)m_Items.size()-1)].priority>1?1:0;

	if (s_pDragSource==this && !s_bDragFromTree && s_bDragMovable && (*pdwEffect&DROPEFFECT_MOVE) && m_DragIndex!=m_ProgramTreeIndex && m_Items[m_DragIndex].priority==(m_Items[min(before,(int)m_Items.size()-1)].priority&2))
	{
		if (before==m_DragIndex || before==m_DragIndex+1)
			return S_OK;
		// dropped in the same menu, just rearrange the items
		PlayMenuSound(SOUND_DROP);
		if (m_Items[m_DragIndex].jumpIndex>=0 && s_JumpList.groups[LOWORD(m_Items[m_DragIndex].jumpIndex)].type==CJumpGroup::TYPE_PINNED)
		{
			// reordering pinned item
			int groupIdx=LOWORD(m_Items[m_DragIndex].jumpIndex);
			int itemIdx=HIWORD(m_Items[m_DragIndex].jumpIndex);
			for (int i=0;i<=m_DragIndex;i++)
			{
				if (m_Items[i].jumpIndex>=0 && LOWORD(m_Items[i].jumpIndex)==groupIdx)
				{
					// found first pinned index
					PinJumpItem(s_JumpAppInfo,s_JumpList,groupIdx,itemIdx,true,before-i);
					PostRefreshMessage();
					break;
				}
			}
		}
		else if (!(m_Options&CONTAINER_AUTOSORT))
		{
			std::vector<SortMenuItem> items;
			int skip1=0, skip2=0;
			for (int i=0;i<m_OriginalCount;i++)
			{
				if (m_Items[i].id==MENU_NO)
				{
					SortMenuItem item(m_Items[i]);
					items.push_back(item);
				}
				else
				{
					if (i<m_DragIndex) skip1++;
					if (i<before) skip2++;
				}
			}
			SortMenuItem drag=items[m_DragIndex-skip1];
			items.erase(items.begin()+(m_DragIndex-skip1));
			if (before-skip2>m_DragIndex-skip1)
				before--;
			items.insert(items.begin()+(before-skip2),drag);
			SaveItemOrder(items);
			if (m_bTwoColumns && s_MenuMode==MODE_JUMPLIST)
				SetMenuMode(MODE_NORMAL);
			PostRefreshMessage();
		}
	}
	else if (m_pDropFolder[folderIndex])
	{
		// simulate dropping the object into the original folder
		PlayMenuSound(SOUND_DROP);

		if (before>=0 && !m_bSubMenu && (*pdwEffect&DROPEFFECT_LINK) && (s_pDragSource!=this || s_bDragFromTree || !s_bDragMovable))
			*pdwEffect=DROPEFFECT_LINK;

		bool bDropped=false;
		bool bAsyncDrop=false;
		if (!m_bSubMenu && (*pdwEffect&DROPEFFECT_LINK) && !s_bRightDrag)
		{
			// if dropping a single folder onto the main menu, create a fake folder
			CComPtr<IShellItemArray> pArray;
			if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(pDataObj,IID_IShellItemArray,(void**)&pArray)))
			{
				DWORD count;
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(pArray->GetCount(&count)) && count==1 && SUCCEEDED(pArray->GetItemAt(0,&pItem)))
				{
					CComString pPath;
					if (pItem->GetDisplayName(SIGDN_FILESYSPATH,&pPath)==S_OK && PathIsDirectory(pPath))
					{
						wchar_t path[_MAX_PATH];
						if (SUCCEEDED(SHGetPathFromIDList(m_Path1[0],path)))
						{
							wchar_t fname[_MAX_FNAME];
							Strcpy(fname,_countof(fname),PathFindFileName(pPath));
							int len=Strlen(fname);
							while (len>0 && (fname[len-1]=='\\' || fname[len-1]==':'))
								len--;
							fname[len]=0;
							PathAppend(path,fname);
							wchar_t finalPath[_MAX_PATH];
							PathYetAnotherMakeUniqueName(finalPath,path,NULL,PathFindFileName(path));
							if (CreateFakeFolder(pPath,finalPath))
							{
								bDropped=true;
								wchar_t locName[_MAX_PATH];
								int locIndex;
								if (SHGetLocalizedName(pPath,locName,_countof(locName),&locIndex)==S_OK)
								{
									SHSetLocalizedName(finalPath,locName,locIndex);
								}
							}
						}
					}
				}
			}
		}
		if (!bDropped)
		{
			// must use IShellFolder to get to the drop target because the BindToHandler doesn't support passing the parent window (easily)
			CComPtr<IShellFolder> pDesktop;
			SHGetDesktopFolder(&pDesktop);
			CComPtr<IShellFolder> pFolder;
			CComPtr<IDropTarget> pTarget;
			if (!pDesktop || FAILED(pDesktop->BindToObject(m_Path1[folderIndex],NULL,IID_IShellFolder,(void**)&pFolder)) || FAILED(pFolder->CreateViewObject(g_OwnerWindow,IID_IDropTarget,(void**)&pTarget)))
				return S_OK;

			DWORD dwEffect=*pdwEffect;

			if (s_bRightDrag)
			{
				if (FAILED(pTarget->DragEnter(pDataObj,MK_RBUTTON|grfKeyState,pt,&dwEffect)))
					return S_OK;
				dwEffect=*pdwEffect;
				pTarget->DragOver(MK_RBUTTON|grfKeyState,pt,&dwEffect);
			}
			else
			{
				if (FAILED(pTarget->DragEnter(pDataObj,MK_LBUTTON|grfKeyState,pt,&dwEffect)))
					return S_OK;
				dwEffect=*pdwEffect;
				pTarget->DragOver(MK_LBUTTON|grfKeyState,pt,pdwEffect);
			}
			// Preserve the async mode negotiated by the data source.
			for (auto& it : s_Menus)
			{
				if (!it->m_bDestroyed)
				{
					it->EnableWindow(FALSE); // disable all menus
					it->SetWindowPos(HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				}
			}
			bool bAllPrograms=s_bAllPrograms;
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,FALSE);
			bool bOld=s_bPreventClosing;
			s_bPreventClosing=true;
			AddRef();
			pTarget->Drop(pDataObj,grfKeyState,pt,pdwEffect);
			bAsyncDrop=WasShellDropAsync(pDataObj);
			if (bAsyncDrop) KeepAfterAsyncDrop();
			s_bPreventClosing=bOld;
			for (auto& it : s_Menus)
			{
				if (!it->m_bDestroyed)
				{
					it->EnableWindow(TRUE); // enable all menus
					it->SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				}
			}
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,TRUE);
		}
		else
		{
			AddRef();
		}

		if (!m_bDestroyed && !bAsyncDrop)
		{
			SetForegroundWindow(m_hWnd);
			SetActiveWindow();
			SetFocus();
		}

		if (before>=0 && !(m_Options&CONTAINER_AUTOSORT))
		{
			std::vector<SortMenuItem> items;
			int skip=0;
			for (int i=0;i<m_OriginalCount;i++)
			{
				if (m_Items[i].id==MENU_NO)
				{
					SortMenuItem item(m_Items[i]);
					items.push_back(item);
				}
				else if (i<before)
					skip++;
			}
			SortMenuItem ins(L"",FNV_HASH0,false,false,folderIndex*2,0);
			items.insert(items.begin()+(before-skip),ins);
			SaveItemOrder(items);
		}
		if (m_bTwoColumns && s_MenuMode==MODE_JUMPLIST)
			SetMenuMode(MODE_NORMAL);
		if (bAsyncDrop) QueueFolderRefresh();
		else PostRefreshMessage();
		Release();
	}
	return S_OK;
}

///////////////////////////////////////////////////////////////////////////////

HRESULT STDMETHODCALLTYPE CDropTargetProxy::DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	if (!m_pOwner) return E_FAIL;
	return m_pOwner->DragEnter(pDataObj,grfKeyState,pt,pdwEffect);
}

HRESULT STDMETHODCALLTYPE CDropTargetProxy::DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	if (!m_pOwner) return E_FAIL;
	return m_pOwner->DragOver(grfKeyState,pt,pdwEffect);
}

HRESULT STDMETHODCALLTYPE CDropTargetProxy::DragLeave( void )
{
	if (!m_pOwner) return E_FAIL;
	return m_pOwner->DragLeave();
}

HRESULT STDMETHODCALLTYPE CDropTargetProxy::Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	if (!m_pOwner) return E_FAIL;
	return m_pOwner->Drop(pDataObj,grfKeyState,pt,pdwEffect);
}
