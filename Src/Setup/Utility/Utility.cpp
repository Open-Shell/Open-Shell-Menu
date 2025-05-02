// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define STRICT_TYPED_ITEMIDS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <map>
#include <atlstr.h>
#include "StringUtils.h"
#include "FNVHash.h"
#include "SettingsParser.h"
#include "resource.h"
#include "ResourceHelper.h"
#include <commctrl.h>
#include "SaveLogFile.h"

// Manifest to enable the 6.0 common controls
#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='*' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")

HINSTANCE g_hInstance;

void EditLoggingOptions( void );
void ManualUninstall( void );
void ShowMetroColorViewer( void );

#ifndef _WIN64
///////////////////////////////////////////////////////////////////////////////

static void LoadFile( const wchar_t *fname, std::vector<unsigned char> &buf )
{
	buf.clear();
	FILE *f=NULL;
	if (_wfopen_s(&f,fname,L"rb") || !f) return;
	fseek(f,0,SEEK_END);
	int size=ftell(f);
	fseek(f,0,SEEK_SET);
	buf.resize(size);
	if (size==0 || fread(&buf[0],1,size,f)!=size)
		buf.clear();
	fclose(f);
}

static unsigned int CalcFileFNV( const wchar_t *fname )
{
	std::vector<unsigned char> buf;
	LoadFile(fname,buf);
	if (buf.empty()) return 0;
	return CalcFNVHash(&buf[0],(int)buf.size(),FNV_HASH0);
}

static void Printf( const char *format, ... )
{
	char buf[1024];
	va_list args;
	va_start(args,format);
	int len=Vsprintf(buf,_countof(buf),format,args);
	va_end(args);
	DWORD q;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),buf,len,&q,NULL);
#if _DEBUG
	OutputDebugStringA(buf);
#endif
}

///////////////////////////////////////////////////////////////////////////////

struct Chunk
{
	int start1, start2, len;
};

int CalcMsiChecksum( wchar_t *const *params, int count )
{
	if (count<2) return 2;

	AttachConsole(ATTACH_PARENT_PROCESS);

	// load files
	wchar_t path1[_MAX_PATH];
	std::vector<unsigned char> buf1, buf2, buf3;
	Sprintf(path1,_countof(path1),L"%s\\Setup32.msi",params[1]);
	LoadFile(path1,buf1);
	if (buf1.empty())
	{
		Printf("Failed to open file %s\n",path1);
		return 1;
	}
	wchar_t path2[_MAX_PATH];
	Sprintf(path2,_countof(path2),L"%s\\Setup64.msi",params[1]);
	LoadFile(path2,buf2);
	if (buf2.empty())
	{
		Printf("Failed to open file %s\n",path2);
		return 1;
	}
	wchar_t path3[_MAX_PATH];
	Sprintf(path3,_countof(path3),L"%s\\SetupARM64.msi",params[1]);
	LoadFile(path3,buf3);
	if (buf3.empty())
	{
		Printf("Failed to open file %s\n",path3);
		return 1;
	}

	int len1=(int)buf1.size();
	int len2=(int)buf2.size();
	int len3=(int)buf3.size();

	for (std::vector<unsigned char>::iterator it=buf1.begin();it!=buf1.end();++it)
		*it^=0xFF;
	for (std::vector<unsigned char>::iterator it=buf2.begin();it!=buf2.end();++it)
		*it^=0xFF;
	for (std::vector<unsigned char>::iterator it=buf3.begin();it!=buf3.end();++it)
		*it^=0xFF;

	const int BLOCK_SIZE = 256 * 1024;
	// detect x86/x64 common blocks (assuming at least 256K in size and in the same order in both files)
	std::vector<Chunk> chunks;
	for (int i=0,start2=0;i<len1-BLOCK_SIZE;i+=BLOCK_SIZE)
	{
		for (int j=start2;j<len2-BLOCK_SIZE;j++)
		{
			if (memcmp(&buf1[i],&buf2[j],BLOCK_SIZE)==0)
			{
				Chunk chunk;
				chunk.start1=i;
				chunk.start2=j;
				chunk.len=BLOCK_SIZE;
				while (chunk.start1>0 && chunk.start2>0 && buf1[chunk.start1-1]==buf2[chunk.start2-1])
				{
					chunk.start1--;
					chunk.start2--;
					chunk.len++;
				}
				while (chunk.start1+chunk.len<len1 && chunk.start2+chunk.len<len2 && buf1[chunk.start1+chunk.len]==buf2[chunk.start2+chunk.len])
				{
					chunk.len++;
				}
				chunks.push_back(chunk);
				i=chunk.start1+chunk.len-1;
				start2=chunk.start2+chunk.len;
				break;
			}
		}
	}
	// detect x86/ARM64 common blocks (assuming at least 256K in size and in the same order in both files)
	std::vector<Chunk> chunks2;
	for (int i=0,start2=0;i<len1-BLOCK_SIZE;i+=BLOCK_SIZE)
	{
		for (int j=start2;j<len3-BLOCK_SIZE;j++)
		{
			if (memcmp(&buf1[i],&buf3[j],BLOCK_SIZE)==0)
			{
				Chunk chunk;
				chunk.start1=i;
				chunk.start2=j;
				chunk.len=BLOCK_SIZE;
				while (chunk.start1>0 && chunk.start2>0 && buf1[chunk.start1-1]==buf3[chunk.start2-1])
				{
					chunk.start1--;
					chunk.start2--;
					chunk.len++;
				}
				while (chunk.start1+chunk.len<len1 && chunk.start2+chunk.len<len3 && buf1[chunk.start1+chunk.len]==buf3[chunk.start2+chunk.len])
				{
					chunk.len++;
				}
				chunks2.push_back(chunk);
				i=chunk.start1+chunk.len-1;
				start2=chunk.start2+chunk.len;
				break;
			}
		}
	}

	// save modified 32-bit MSI
	{
		Strcat(path1,_countof(path1),L"_");
		FILE *f=NULL;
		if (_wfopen_s(&f,path1,L"wb") || !f)
		{
			Printf("Failed to write %s\n",path1);
			return 1;
		}
		fwrite(&buf1[0],1,len1,f);
		fclose(f);
	}

	// save modified 64-bit MSI
	{
		Strcat(path2,_countof(path2),L"_");
		FILE *f=NULL;
		if (_wfopen_s(&f,path2,L"wb") || !f)
		{
			Printf("Failed to write %s\n",path2);
			return 1;
		}
		int start=0;
		for (std::vector<Chunk>::const_iterator it=chunks.begin();it!=chunks.end();++it)
		{
			if (it->start2-start>0)
				fwrite(&buf2[start],1,it->start2-start,f);
			start=it->start2+it->len;
		}
		if (len2-start>0)
			fwrite(&buf2[start],1,len2-start,f);
		fclose(f);
	}

	// save modified ARM64 MSI
	{
		Strcat(path3,_countof(path3),L"_");
		FILE* f=NULL;
		if (_wfopen_s(&f,path3,L"wb") || !f)
		{
			Printf("Failed to write %s\n",path3);
			return 1;
		}
		int start=0;
		for (std::vector<Chunk>::const_iterator it=chunks2.begin();it!=chunks2.end();++it)
		{
			if (it->start2-start>0)
				fwrite(&buf3[start],1,it->start2-start,f);
			start=it->start2+it->len;
		}
		if (len3-start>0)
			fwrite(&buf3[start],1,len3-start,f);
		fclose(f);
	}

	unsigned int fnvs[3];
	fnvs[0]=CalcFNVHash(&buf1[0],len1,FNV_HASH0);
	fnvs[1]=CalcFNVHash(&buf2[0],len2,FNV_HASH0);
	fnvs[2]=CalcFNVHash(&buf3[0],len3,FNV_HASH0);

	// save fnvs and chunks
	{
		FILE *f=NULL;
		if (_wfopen_s(&f,L"msichecksum.bin",L"wb") || !f)
		{
			Printf("Failed to write msichecksum.bin\n");
			return 1;
		}
		fwrite(fnvs,4,_countof(fnvs),f);
		int count=(int)chunks.size();
		fwrite(&count,1,4,f);
		fwrite(&chunks[0],sizeof(Chunk),count,f);
		count=(int)chunks2.size();
		fwrite(&count,1,4,f);
		fwrite(&chunks2[0],sizeof(Chunk),count,f);
		fclose(f);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

int MakeEnglishDll( wchar_t *const *params, int count )
{
	if (count<5) return 2;

	AttachConsole(ATTACH_PARENT_PROCESS);

	HMODULE hEn0=LoadLibraryEx(L"en-US.dll",NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hEn0)
	{
		Printf("Failed to open en-US.dll (err: %d)\n",GetLastError());
		return 1;
	}

	std::vector<char> version;
	{
		HRSRC hResInfo=FindResource(hEn0,MAKEINTRESOURCE(VS_VERSION_INFO),RT_VERSION);
		if (hResInfo)
		{
			HGLOBAL hRes=LoadResource(hEn0,hResInfo);
			void *pRes=LockResource(hRes);
			if (pRes)
			{
				DWORD len=SizeofResource(hEn0,hResInfo);
				if (len>=40+sizeof(VS_FIXEDFILEINFO))
				{
					version.resize(len);
					memcpy(&version[0],pRes,len);
				}
			}
		}
	}
	FreeLibrary(hEn0);
	if (version.empty())
	{
		Printf("Can't find version resource in en-US.dll\n");
		return 1;
	}

	HANDLE hEn=BeginUpdateResource(L"en-US.dll",FALSE);
	if (!hEn)
	{
		Printf("Failed to open en-US.dll (err: %d)\n",GetLastError());
		return 1;
	}

	int res=1;
	HMODULE hExplorer=NULL, hMenu=NULL, hIE=NULL;
	WORD language=MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);

	// get version, strings and dialog from ClassicExplorer32.dll
	hExplorer=LoadLibraryEx(params[1],NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hExplorer)
	{
		Printf("Failed to open %S (err: %d)\n",params[1],GetLastError());
		goto qqq;
	}

	// copy version
	{
		HRSRC hResInfo=FindResource(hExplorer,MAKEINTRESOURCE(VS_VERSION_INFO),RT_VERSION);
		void *pRes=NULL;
		if (hResInfo)
		{
			HGLOBAL hRes=LoadResource(hExplorer,hResInfo);
			pRes=LockResource(hRes);
		}
		if (!pRes)
		{
			Printf("Can't find version resource in %S\n",params[1]);
			goto qqq;
		}
		VS_FIXEDFILEINFO *pVer=(VS_FIXEDFILEINFO*)((char*)pRes+40);
		VS_FIXEDFILEINFO *pVer0=(VS_FIXEDFILEINFO*)(&version[40]);
		pVer0->dwProductVersionMS=pVer->dwProductVersionMS;
		pVer0->dwProductVersionLS=pVer->dwProductVersionLS;
		pVer0->dwFileVersionMS=pVer->dwFileVersionMS;
		pVer0->dwFileVersionLS=pVer->dwFileVersionLS;
		UpdateResource(hEn,RT_VERSION,MAKEINTRESOURCE(VS_VERSION_INFO),language,&version[0],version.size());
	}

	// copy strings
	for (int i=2000;i<3000;i+=16)
	{
		int id=i/16;
		HRSRC hResInfo=FindResource(hExplorer,MAKEINTRESOURCE(id),RT_STRING);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hExplorer,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_STRING,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hExplorer,hResInfo));
	}

	// copy dialogs
	for (int id=2000;id<2010;id++)
	{
		HRSRC hResInfo=FindResource(hExplorer,MAKEINTRESOURCE(id),RT_DIALOG);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hExplorer,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_DIALOG,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hExplorer,hResInfo));
	}

	// get strings and dialog from StartMenuDLL.dll
	hMenu=LoadLibraryEx(params[2],NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hMenu)
	{
		Printf("Failed to open %S (err: %d)\n",params[2],GetLastError());
		goto qqq;
	}

	// copy strings
	for (int i=3000;i<5000;i+=16)
	{
		int id=i/16;
		HRSRC hResInfo=FindResource(hMenu,MAKEINTRESOURCE(id),RT_STRING);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hMenu,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_STRING,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hMenu,hResInfo));
	}
	for (int i=7000;i<8000;i+=16)
	{
		int id=i/16;
		HRSRC hResInfo=FindResource(hMenu,MAKEINTRESOURCE(id),RT_STRING);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hMenu,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_STRING,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hMenu,hResInfo));
	}

	// copy dialogs
	for (int id=3000;id<4010;id++)
	{
		HRSRC hResInfo=FindResource(hMenu,MAKEINTRESOURCE(id),RT_DIALOG);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hMenu,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_DIALOG,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hMenu,hResInfo));
	}

	// get strings from ClassicIEDLL.dll
	hIE=LoadLibraryEx(params[3],NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hIE)
	{
		Printf("Failed to open %S (err: %d)\n",params[3],GetLastError());
		goto qqq;
	}

	// copy strings
	for (int i=5000;i<6000;i+=16)
	{
		int id=i/16;
		HRSRC hResInfo=FindResource(hIE,MAKEINTRESOURCE(id),RT_STRING);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hIE,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_STRING,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hIE,hResInfo));
	}

	// get strings and dialog from Update.exe
	hMenu=LoadLibraryEx(params[4],NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hMenu)
	{
		Printf("Failed to open %S (err: %d)\n",params[4],GetLastError());
		goto qqq;
	}

	// copy strings
	for (int i=6000;i<7000;i+=16)
	{
		int id=i/16;
		HRSRC hResInfo=FindResource(hMenu,MAKEINTRESOURCE(id),RT_STRING);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hMenu,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_STRING,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hMenu,hResInfo));
	}

	// copy dialogs
	for (int id=6000;id<6010;id++)
	{
		HRSRC hResInfo=FindResource(hMenu,MAKEINTRESOURCE(id),RT_DIALOG);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hMenu,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		UpdateResource(hEn,RT_DIALOG,MAKEINTRESOURCE(id),language,pRes,SizeofResource(hMenu,hResInfo));
	}

	res=0;
qqq:
	if (!EndUpdateResource(hEn,res!=0) && res==0)
	{
		Printf("Failed to update en-US.dll (err: %d)\n",GetLastError());
		res=1;
	}
	if (hExplorer) FreeLibrary(hExplorer);
	if (hMenu) FreeLibrary(hMenu);
	if (hIE) FreeLibrary(hIE);

	return res;
}

///////////////////////////////////////////////////////////////////////////////

static void UnsescapeString( wchar_t *string )
{
	wchar_t *dst=string;
	int len=Strlen(string);
	bool bQuoted=false;
	if (string[0]=='"' && string[len-1]=='"')
	{
		bQuoted=true;
		string[len-1]=0;
		if (*string) string++;
	}
	
	for (const wchar_t *src=string;*src;src++)
	{
		if (*src=='\\')
		{
			src++;
			if (!*src) break;
			if (*src=='t')
				*dst++='\t';
			else if (*src=='r')
				*dst++='\r';
			else if (*src=='n')
				*dst++='\n';
			else
				*dst++=*src;
		}
		else if (*src=='"' && bQuoted)
		{
			src++;
			if (!*src) break;
			*dst++=*src;
		}
		else
			*dst++=*src;
	}
	*dst=0;
}

static void WriteString( HANDLE csv, int id, const wchar_t *string1, int len1, const wchar_t *string2, int len2, CSettingsParser &parser, int subid=INT_MAX )
{
	DWORD q;
	wchar_t buf[256];
	int len3;
	if (subid!=INT_MAX)
		len3=Sprintf(buf,_countof(buf),L"%d/%d",id,subid&65535);
	else
		len3=Sprintf(buf,_countof(buf),L"%d",id);

	const wchar_t *comment=parser.FindSetting(buf);
	if (comment && _wcsicmp(comment,L"ignore")==0)
		return;

	WriteFile(csv,buf,len3*2,&q,NULL);
	WriteFile(csv,L"\t",2,&q,NULL);

	for (int i=0;i<len1;i++)
	{
		WORD c=string1[i];
		if (c=='\t')
			WriteFile(csv,L"\\t",4,&q,NULL);
		else if (c=='\r')
			WriteFile(csv,L"\\r",4,&q,NULL);
		else if (c=='\n')
			WriteFile(csv,L"\\n",4,&q,NULL);
		else if (c=='\\')
			WriteFile(csv,L"\\\\",4,&q,NULL);
		else
			WriteFile(csv,&c,2,&q,NULL);
	}
	WriteFile(csv,L"\t",2,&q,NULL);

	for (int i=0;i<len2;i++)
	{
		WORD c=string2[i];
		if (c=='\t')
			WriteFile(csv,L"\\t",4,&q,NULL);
		else if (c=='\r')
			WriteFile(csv,L"\\r",4,&q,NULL);
		else if (c=='\n')
			WriteFile(csv,L"\\n",4,&q,NULL);
		else if (c=='\\')
			WriteFile(csv,L"\\\\",4,&q,NULL);
		else
			WriteFile(csv,&c,2,&q,NULL);
	}
	WriteFile(csv,L"\t",2,&q,NULL);

	if (comment)
		WriteFile(csv,comment,Strlen(comment)*2,&q,NULL);
	WriteFile(csv,L"\r\n",4,&q,NULL);
}

static INT_PTR CALLBACK DefaultDlgProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_WINDOWPOSCHANGING)
	{
		WINDOWPOS *pos=(WINDOWPOS*)lParam;
		pos->flags&=~SWP_SHOWWINDOW;
	}
	return FALSE;
}

static void WriteDialog( HWND parent, HANDLE csv, int id, HINSTANCE hInstance1, const DLGTEMPLATE *pTemplate1, HINSTANCE hInstance2, const DLGTEMPLATE *pTemplate2, CSettingsParser &parser )
{
	HWND dlg1=CreateDialogIndirect(hInstance1,pTemplate1,parent,DefaultDlgProc);
	if (dlg1)
	{
		HWND dlg2=pTemplate2?CreateDialogIndirect(hInstance2,pTemplate2,parent,DefaultDlgProc):NULL;

		wchar_t text1[1024];
		wchar_t text2[1024];
		GetWindowText(dlg1,text1,_countof(text1));
		if (*text1)
		{
			if (dlg2)
				GetWindowText(dlg2,text2,_countof(text2));
			else
				text2[0]=0;
			WriteString(csv,id,text1,Strlen(text1),text2,Strlen(text2),parser,0);
		}
		for (HWND child1=GetWindow(dlg1,GW_CHILD);child1;child1=GetWindow(child1,GW_HWNDNEXT))
		{
			GetWindowText(child1,text1,_countof(text1));
			if (*text1)
			{
				int childId=(int)GetWindowLong(child1,GWL_ID);
				HWND child2=dlg2?GetDlgItem(dlg2,childId):NULL;
				if (child2)
					GetWindowText(child2,text2,_countof(text2));
				else
					text2[0]=0;
				WriteString(csv,id,text1,Strlen(text1),text2,Strlen(text2),parser,childId);
			}
		}
		DestroyWindow(dlg1);
		if (dlg2)
			DestroyWindow(dlg2);
	}
}

int ExtractStrings( wchar_t *const *params, int count )
{
	if (count<3) return 3;

	const wchar_t *dllName1=params[1];
	const wchar_t *dllName2=NULL;
	const wchar_t *csvName=params[2];
	if (count>3)
	{
		dllName2=csvName;
		csvName=params[3];
	}
	

	HMODULE hDLL1=LoadLibraryEx(dllName1,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hDLL1)
	{
		Printf("Failed to open %S (err: %d)\n",dllName1,GetLastError());
		return 1;
	}

	HMODULE hDLL2=NULL;
	if (dllName2)
	{
		hDLL2=LoadLibraryEx(dllName2,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (!hDLL2)
		{
			Printf("Failed to open %S (err: %d)\n",dllName2,GetLastError());
			return 1;
		}
	}

	int res=1;
	CSettingsParser parser;
	parser.LoadText(L"LocComments.txt");
	parser.ParseText();

	HANDLE hCSV=CreateFile(csvName,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hCSV==INVALID_HANDLE_VALUE)
	{
		Printf("Failed to write %S\n",csvName);
		FreeLibrary(hDLL1);
		if (hDLL2) FreeLibrary(hDLL2);
		return 1;
	}

	wchar_t title[]=L"\xFEFFID\tEnglish\tTranslation\tComment\r\n";
	DWORD q;
	WriteFile(hCSV,title,Strlen(title)*2,&q,NULL);

	// copy strings
	for (int i=2000;i<8000;i+=16)
	{
		int id=i/16;
		HRSRC hResInfo=FindResource(hDLL1,MAKEINTRESOURCE(id),RT_STRING);
		if (!hResInfo) continue;
		HGLOBAL hRes=LoadResource(hDLL1,hResInfo);
		void *pRes=LockResource(hRes);
		if (!pRes) continue;
		const WORD *data=(WORD*)pRes;
		for (int j=0;j<16;j++)
		{
			int len=*data;
			if (len>0)
			{
				int stringId=(id-1)*16+j;
				wchar_t buf[1024];
				if (!hDLL2 || !LoadString(hDLL2,stringId,buf,_countof(buf)))
					buf[0]=0;
				WriteString(hCSV,stringId,(const wchar_t*)data+1,len,buf,Strlen(buf),parser);
			}
			data+=len+1;
		}
	}

	HWND parent=CreateWindow(L"button",NULL,WS_POPUP,0,0,10,10,NULL,NULL,NULL,NULL);

	// copy dialogs
	for (int id=2000;id<7000;id++)
	{
		if (id>=2010 && id<3000) continue;
		if (id>=3010 && id<4000) continue;
		if (id>=4010 && id<5000) continue;
		if (id>=5010 && id<6000) continue;
		if (id>=6010 && id<7000) continue;
		HRSRC hResInfo1=FindResource(hDLL1,MAKEINTRESOURCE(id),RT_DIALOG);
		if (!hResInfo1) continue;
		void *pRes1=LockResource(LoadResource(hDLL1,hResInfo1));
		if (!pRes1) continue;

		void *pRes2=NULL;
		if (hDLL2)
		{
			HRSRC hResInfo2=FindResource(hDLL2,MAKEINTRESOURCE(id),RT_DIALOG);
			if (hResInfo2)
				pRes2=LockResource(LoadResource(hDLL2,hResInfo2));
		}

		WriteDialog(parent,hCSV,id,hDLL1,(DLGTEMPLATE*)pRes1,hDLL2,(DLGTEMPLATE*)pRes2,parser);
	}

	// additional strings
	{
		HRSRC hResInfo=FindResource(hDLL1,MAKEINTRESOURCE(1),L"L10N");
		if (hResInfo)
		{
			HGLOBAL hRes=LoadResource(hDLL1,hResInfo);
			const wchar_t *pRes=(wchar_t*)LockResource(hRes);
			if (pRes)
			{
				int size=SizeofResource(hDLL1,hResInfo)/2;
				if (*pRes==L'\xFEFF')
					pRes++, size--;
				wchar_t *pBuf=new wchar_t[size+1];
				memcpy(pBuf,pRes,size*2);
				pBuf[size]=0;
				for (int i=0;i<size;i++)
					if (pBuf[i]=='=')
						pBuf[i]='\t';
				WriteFile(hCSV,pBuf,size*2,&q,NULL);
				delete pBuf;
			}
		}
	}

	CloseHandle(hCSV);
	DestroyWindow(parent);
	if (hDLL1) FreeLibrary(hDLL1);
	if (hDLL2) FreeLibrary(hDLL2);

	return res;
}

///////////////////////////////////////////////////////////////////////////////

static BOOL CALLBACK EnumResLangProc( HMODULE hModule, LPCTSTR lpszType, LPCTSTR lpszName, WORD wIDLanguage, LONG_PTR lParam )
{
	if (IS_INTRESOURCE(lpszName))
	{
		std::vector<std::pair<int,WORD>> &oldStrings=*(std::vector<std::pair<int,WORD>>*)lParam;
		oldStrings.emplace_back(PtrToInt(lpszName),wIDLanguage);
	}
	return TRUE;
}

static BOOL CALLBACK EnumResNameProc( HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam )
{
	EnumResourceLanguages(hModule,lpszType,lpszName,EnumResLangProc,lParam);
	return TRUE;
}

static int ImportStrings( wchar_t *const *params, int count )
{
	if (count<3) return 3;
	const wchar_t *dllName=params[1];
	const wchar_t *csvName=params[2];

	HANDLE hCSV=CreateFile(csvName,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hCSV==INVALID_HANDLE_VALUE)
	{
		Printf("Failed to read %S\n",csvName);
		return 1;
	}

	int size=SetFilePointer(hCSV,0,NULL,FILE_END)/2;
	SetFilePointer(hCSV,0,NULL,FILE_BEGIN);
	std::vector<wchar_t> buf(size+1);
	DWORD q;
	ReadFile(hCSV,&buf[0],size*2,&q,NULL);
	CloseHandle(hCSV);
	buf[size]=0;
	for (int i=0;i<size;i++)
		if (buf[i]=='\r' || buf[i]=='\n')
			buf[i]=0;

	std::map<int,const wchar_t*> lines;
	wchar_t *str=&buf[0];
	int min=100000, max=0;
	while (str<&buf[size])
	{
		int len=Strlen(str);
		wchar_t *next=str+len+1;
		wchar_t *tab=wcschr(str,'\t');
		if (tab)
		{
			*tab=0;
			int id=_wtol(str);
			bool bNumber=id>0;
			for (;*str;str++)
				if (*str<'0' || *str>'9')
				{
					bNumber=false;
					break;
				}
			if (bNumber)
			{
				tab=wcschr(tab+1,'\t');
				if (tab)
				{
					str=tab+1;
					tab=wcschr(str,'\t');
					if (tab) *tab=0;
					if (lines.find(id)!=lines.end())
					{
						Printf("Duplicate string ID %d\n",id);
						return 1;
					}
					UnsescapeString(str);
					lines[id]=str;
					if (min>id) min=id;
					if (max<id) max=id;
				}
			}
		}
		str=next;
	}

	HMODULE hDll=LoadLibraryEx(dllName,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!hDll)
	{
		Printf("Failed to open %S (err: %d)\n",dllName,GetLastError());
		return 1;
	}

	std::vector<std::pair<int,WORD>> oldStrings;
	EnumResourceNames(hDll,RT_STRING,EnumResNameProc,(LONG_PTR)&oldStrings);
	FreeLibrary(hDll);

	HANDLE hUpdate=BeginUpdateResource(dllName,FALSE);
	if (!hUpdate)
	{
		Printf("Failed to open %S (err: %d)\n",dllName,GetLastError());
		return 1;
	}

	// delete all string resources
	for (int i=0;i<(int)oldStrings.size();i++)
	{
		UpdateResource(hUpdate,RT_STRING,MAKEINTRESOURCE(oldStrings[i].first),oldStrings[i].second,NULL,0);
	}

	// add new string lines
	max+=32;
	for (int i=min;i<max;i+=16)
	{
		int id=i/16;
		int idx=(id-1)*16;
		std::vector<wchar_t> res;
		for (int j=0;j<16;j++,idx++)
		{
			std::map<int,const wchar_t*>::const_iterator it=lines.find(idx);
			const wchar_t *str=L"";
			if (it!=lines.end())
				str=it->second;
			int len=Strlen(str);
			res.push_back((wchar_t)len);
			for (int c=0;c<len;c++)
				res.push_back(str[c]);
		}
		if (res.size()>16)
			UpdateResource(hUpdate,RT_STRING,MAKEINTRESOURCE(id),LANG_NEUTRAL,&res[0],res.size()*2);
	}

	if (!EndUpdateResource(hUpdate,FALSE))
	{
		Printf("Failed to update %S (err: %d)\n",dllName,GetLastError());
		return 1;
	}
	return 0;
}
#endif

///////////////////////////////////////////////////////////////////////////////

static HRESULT CALLBACK TaskDialogCallback( HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData )
{
	if (uNotification==TDN_HYPERLINK_CLICKED)
	{
		const wchar_t *link=(const wchar_t*)lParam;
		ShellExecute(hwnd,NULL,link,NULL,NULL,SW_SHOWNORMAL);
	}
	return S_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Open-Shell utility - multiple utilities for building and maintaining Open-Shell
// Usage:
//   no parameters - saves a troubleshooting log
//   crcmsi <msi path> // creates a file with checksum of all msi files
//   makeEN <explorer dll> <start menu dll> <ie dll> <update exe> // extracts the localization resources and creates a sample en-US.DLL
//   extract <dll> <csv> // extracts the string table, the dialog text, and the L10N text from a DLL and stores it in a CSV
//   extract en-us.dll <dll> <csv> // extracts the string table, the dialog text, and the L10N text from two DLL and stores it in a CSV
//   import <dll> <csv> // replaces the string table in the DLL with the text from the CSV

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpstrCmdLine, int nCmdShow )
{
	g_hInstance=hInstance;

	if (!*lpstrCmdLine)
	{
#ifndef _WIN64
		DWORD winVer=GetVersionEx(GetModuleHandle(L"user32.dll"));

		TASKDIALOG_BUTTON taskButtons[4]={
			{101,L"Save system log\nSaves detailed information about your system for troubleshooting purposes."},
			{102,L"Open-Shell logging options\nEnable or disable various logging options."},
			{103,L"Remove Open-Shell\nManually removes Open-Shell from your system. Use only if the conventional methods for uninstallation do not work."},
			{104,L"Show Metro colors\nSee the color palette that can be used in start menu skins."},
		};
		TASKDIALOGCONFIG tasks={sizeof(tasks)};
		tasks.dwFlags=TDF_USE_HICON_MAIN|TDF_USE_COMMAND_LINKS|TDF_ENABLE_HYPERLINKS;
		tasks.dwCommonButtons=TDCBF_CANCEL_BUTTON;
		tasks.pszWindowTitle=L"Open-Shell Utility";
		tasks.hMainIcon=LoadIcon(hInstance,MAKEINTRESOURCE(IDI_ICON1));
		tasks.pszMainInstruction=L"Select task to perform";
		tasks.cButtons=HIWORD(winVer)>=0x0602?4:3;
		tasks.pButtons=taskButtons;
		tasks.pszFooter=L"<A HREF=\"https://open-shell.github.io/Open-Shell-Menu\">Open-Shell Homepage</A>";
		tasks.pfCallback=TaskDialogCallback;

		int seleciton;
		if (TaskDialogIndirect(&tasks,&seleciton,NULL,NULL)==IDCANCEL)
			return 0;
		if (seleciton==101)
		{
			ShowSaveLogFile();
		}
		else if (seleciton==102)
		{
			EditLoggingOptions();
		}
		else if (seleciton==103)
		{
			wchar_t exe[_MAX_PATH];
			GetModuleFileName(NULL,exe,_countof(exe));
			CoInitialize(NULL);
			ShellExecute(NULL,L"runas",exe,L"uninstall",NULL,SW_SHOWNORMAL);
			CoUninitialize();
		}
		else if (seleciton==104)
		{
			ShowMetroColorViewer();
		}
#endif
		return 0;
	}

	int count;
	wchar_t *const *params=CommandLineToArgvW(lpstrCmdLine,&count);

#ifndef _WIN64
	if (_wcsicmp(params[0],L"crcmsi")==0)
	{
		return CalcMsiChecksum(params,count);
	}

	if (_wcsicmp(params[0],L"makeEN")==0)
	{
		return MakeEnglishDll(params,count);
	}

	if (_wcsicmp(params[0],L"extract")==0)
	{
		return ExtractStrings(params,count);
	}

	if (_wcsicmp(params[0],L"import")==0)
	{
		return ImportStrings(params,count);
	}

	if (_wcsicmp(params[0],L"colors")==0)
	{
		ShowMetroColorViewer();
		return 0;
	}
	if (_wcsicmp(params[0],L"uninstall")==0)
	{
		ManualUninstall();
		return 0;
	}
#endif

	if (_wcsicmp(params[0],L"savelog")==0 || _wcsicmp(params[0],L"saveloga")==0)
	{
		if (count<=1)
			return 1;
		return SaveLogFile(params[1],_wcsicmp(params[0],L"saveloga")==0);
	}

	return 1;
}
