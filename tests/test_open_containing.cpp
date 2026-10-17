#define NOMINMAX
#define STRICT_TYPED_ITEMIDS
#include <windows.h>
#include <shlobj.h>
#include <atlbase.h>
#include "ComHelper.h"
#include <thread>
#include <string>
#include <cstdio>

static int checks=0,calls=0;
static DWORD callerThread=0,selectionThread=0;
static APTTYPE selectionApartment=APTTYPE_CURRENT;
static bool selectionArguments=false,messagePumped=false;
static HWND callerWindow=NULL;
static HANDLE messageEvent=NULL;
static HRESULT selectionResult=S_OK;
static CAbsolutePidl selected;

static void Check(bool value,const char *description)
{
	if (!value) { fprintf(stderr,"FAIL: %s\n",description); ExitProcess(1); }
	checks++; printf("PASS: %s\n",description); fflush(stdout);
}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
	if (message==WM_APP+1) { SetEvent(messageEvent); return 0; }
	return DefWindowProc(window,message,wParam,lParam);
}
// Replace only the final Explorer-opening call. Link binding and PIDL extraction
// remain real Windows APIs running through the actual production STA executor.
static HRESULT CaptureSelection(PCIDLIST_ABSOLUTE pidl,UINT count,PCUITEMID_CHILD_ARRAY children,DWORD flags)
{
	calls++;
	selected.Clone(pidl);
	selectionThread=GetCurrentThreadId();
	APTTYPEQUALIFIER qualifier;
	CoGetApartmentType(&selectionApartment,&qualifier);
	selectionArguments=count==0 && children==NULL && flags==0;
	PostMessage(callerWindow,WM_APP+1,0,0);
	messagePumped=WaitForSingleObject(messageEvent,3000)==WAIT_OBJECT_0;
	return selectionResult;
}
#define SHOpenFolderAndSelectItems CaptureSelection
// PRODUCTION_FUNCTIONS
#undef SHOpenFolderAndSelectItems

static CAbsolutePidl Parse(const std::wstring &path)
{
	CAbsolutePidl pidl;
	Check(SUCCEEDED(SHParseDisplayName(path.c_str(),NULL,&pidl,0,NULL)),"private fixture PIDL resolved");
	return pidl;
}
static void MakeFile(const std::wstring &path)
{
	HANDLE file=CreateFile(path.c_str(),GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
	Check(file!=INVALID_HANDLE_VALUE,"new private fixture file created without overwriting user files");
	CloseHandle(file);
}
static void MakeShortcut(const std::wstring &path,const std::wstring &target)
{
	CComPtr<IShellLink> link;
	Check(SUCCEEDED(link.CoCreateInstance(CLSID_ShellLink)),"real Shell shortcut created");
	Check(SUCCEEDED(link->SetPath(target.c_str())) && SUCCEEDED(link->SetArguments(L"--fixture-argument")),"shortcut target and independent arguments stored");
	CComQIPtr<IPersistFile> persist(link);
	Check(persist && SUCCEEDED(persist->Save(path.c_str(),FALSE)),"new private shortcut saved");
}
static void RunCase(const char *name,PCIDLIST_ABSOLUTE original,PCIDLIST_ABSOLUTE expected,HRESULT apiResult=S_OK,bool exploreShortcut=false)
{
	printf("Case: %s\n",name); fflush(stdout);
	calls=0; selected.Clear(); selectionThread=0;
	selectionApartment=APTTYPE_CURRENT;
	selectionArguments=messagePumped=false;
	selectionResult=apiResult;
	ResetEvent(messageEvent);
	HRESULT result=exploreShortcut?SHOpenFolderAndSelectItemsSafe(original,0,NULL,0):OpenContainingItemSafe(original);
	Check(calls==1,"exactly one folder-selection request issued");
	Check(result==apiResult,"folder-selection HRESULT preserved");
	Check(expected?selected && ILIsEqual(selected,expected):!selected,"Explorer would receive the expected target or fallback PIDL");
	Check(selectionThread && selectionThread!=callerThread && selectionApartment==APTTYPE_STA,"selection and link resolution run on a separate STA");
	Check(selectionArguments,"single-item selection arguments preserved");
	Check(messagePumped,"caller processes its own messages while the worker runs");
}
int wmain(int argc,wchar_t **argv)
{
	if (argc!=2) return 2;
	Check(SUCCEEDED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)),"caller STA initialized");
	callerThread=GetCurrentThreadId();
	messageEvent=CreateEvent(NULL,TRUE,FALSE,NULL);
	Check(messageEvent!=NULL,"private synchronization event created");
	WNDCLASS wc={}; wc.lpfnWndProc=WindowProc; wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName=L"OpenShell.OpenContaining.Test";
	Check(RegisterClass(&wc)!=0,"private message-window class registered");
	callerWindow=CreateWindowEx(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,NULL,wc.hInstance,NULL);
	Check(callerWindow!=NULL,"private invisible caller window created");
	std::wstring base=std::wstring(argv[1])+L"\\fixtures-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
	Check(CreateDirectory(base.c_str(),NULL)!=FALSE,"fresh isolated fixture root created");
	std::wstring links=base+L"\\Start Menu",programs=base+L"\\Program Files (\u00dc)";
	Check(CreateDirectory(links.c_str(),NULL) && CreateDirectory(programs.c_str(),NULL),"shortcut folder differs from program target folder");
	std::wstring program=programs+L"\\Application.exe";
	MakeFile(program);
	std::wstring shortcut=links+L"\\Application \u00e4.lnk",folderShortcut=links+L"\\Folder.lnk",invalidShortcut=links+L"\\Invalid.lnk";
	MakeShortcut(shortcut,program); MakeShortcut(folderShortcut,programs); MakeFile(invalidShortcut);
	auto programPidl=Parse(program),folderPidl=Parse(programs),linkPidl=Parse(shortcut),folderLinkPidl=Parse(folderShortcut),invalidPidl=Parse(invalidShortcut);
	Check(!ILIsEqual(linkPidl,programPidl),"regression fixture distinguishes shortcut from program PIDL");
	RunCase("shortcut to program with spaces, Unicode and arguments",linkPidl,programPidl);
	RunCase("shortcut to folder",folderLinkPidl,folderPidl);
	RunCase("ordinary program file",programPidl,programPidl);
	RunCase("ordinary folder",folderPidl,folderPidl);
	RunCase("unresolvable invalid shortcut falls back",invalidPidl,invalidPidl);
	RunCase("invalid input is passed through to the final API",NULL,NULL,E_INVALIDARG);
	RunCase("target-folder API failure is not hidden by opening the shortcut folder",linkPidl,programPidl,E_ACCESSDENIED);
	RunCase("separate shortcut-folder command remains unchanged",linkPidl,linkPidl,S_OK,true);
	DestroyWindow(callerWindow); CloseHandle(messageEvent);
	printf("%d checks passed. No Explorer windows were opened; fixtures are under build/open-containing-tests.\n",checks);
}
