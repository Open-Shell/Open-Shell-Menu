// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <commctrl.h>
#include <vector>
#include "resource.h"
#include "StringUtils.h"
#include "FNVHash.h"

// Setup.exe is a bootstrap application that contains installers for x86, x64 and ARM64.
// It unpacks the right installer into the temp directory and executes it.

typedef BOOL (WINAPI *FIsWow64Process)( HANDLE hProcess, PBOOL Wow64Process );
typedef BOOL (WINAPI *FIsWow64Process2)( HANDLE hProcess, USHORT *pProcessMachine, USHORT *pNativeMachine );
typedef BOOL (WINAPI *FWow64DisableWow64FsRedirection)( PVOID* OldValue );
typedef BOOL (WINAPI *FWow64RevertWow64FsRedirection)( PVOID OldValue );



enum
{
	ERR_WRONG_OS=101, // the OS is too old, Windows 7 or up is required
	ERR_OLD_VERSION, // detected version older than 1.0.0
	ERR_HASH_NOTFOUND, // the HASH resource is missing
	ERR_MSIRES_NOTFOUND, // missing MSI resource
	ERR_HASH_ERROR,
	ERR_VERRES_NOTFOUND, // missing version resource
	ERR_MSI_EXTRACTFAIL, // failed to extract the MSI file
	ERR_MSIEXEC, // msiexec failed to start
};

enum ExtractType
{
	None,
	x86,
	x64,
	ARM64
};

struct Chunk
{
	int start1, start2, len;
};

static void WriteFileXOR( HANDLE hFile, const unsigned char *buf, int size )
{
	if (size>0)
	{
		std::vector<unsigned char> buf2;
		buf2.reserve(size);
		for (int i=0;i<size;i++)
			buf2.push_back(buf[i]^0xFF);
		DWORD q;
		WriteFile(hFile,&buf2[0],size,&q,NULL);
	}
}

static int ExtractMsi( HINSTANCE hInstance, const wchar_t *msiName, ExtractType extractType, bool bQuiet )
{
	void *pRes=NULL;
	HRSRC hResInfo=FindResource(hInstance,MAKEINTRESOURCE(IDR_MSI_CHECKSUM),L"MSI_FILE");
	if (hResInfo)
	{
		HGLOBAL hRes=LoadResource(hInstance,hResInfo);
		pRes=LockResource(hRes);
	}
	if (!pRes)
	{
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_INTERNAL,strText,_countof(strText))) strText[0]=0;
			MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_HASH_NOTFOUND;
	}
	unsigned int hash0=((unsigned int*)pRes)[extractType-1];
	const Chunk *pChunks=NULL;
	int chunkCount=0;
	if (extractType==x64)
	{
		chunkCount=((unsigned int*)pRes)[3];
		pChunks=(Chunk*)((unsigned int*)pRes+4);
	}
	if (extractType==ARM64)
	{
		int x64chunkCount=((unsigned int*)pRes)[3];
		const Chunk *px64Chunks=(Chunk*)((unsigned int*)pRes+4);

		chunkCount=((unsigned int*)(px64Chunks+x64chunkCount))[0];
		pChunks = (Chunk*)((unsigned int*)(px64Chunks+x64chunkCount)+1);
	}

	// extract the installer
	const unsigned char *pRes32=NULL;
	hResInfo=FindResource(hInstance,MAKEINTRESOURCE(IDR_MSI_FILE32),L"MSI_FILE");
	if (hResInfo)
	{
		HGLOBAL hRes=LoadResource(hInstance,hResInfo);
		pRes32=(unsigned char*)LockResource(hRes);
	}
	if (!pRes32)
	{
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_INTERNAL,strText,_countof(strText))) strText[0]=0;
			MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_MSIRES_NOTFOUND;
	}
	const unsigned char *pRes64=NULL, *pResArm64=NULL;
	int size32=SizeofResource(hInstance,hResInfo);
	unsigned int hash;
	int size64=0, sizeArm64=0;
	if (extractType==x64)
	{
		HRSRC hResInfo=FindResource(hInstance,MAKEINTRESOURCE(IDR_MSI_FILE64),L"MSI_FILE");
		if (hResInfo)
		{
			HGLOBAL hRes=LoadResource(hInstance,hResInfo);
			pRes64=(unsigned char*)LockResource(hRes);
		}
		if (!pRes64)
		{
			if (!bQuiet)
			{
				wchar_t strTitle[256];
				if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
				wchar_t strText[256];
				if (!LoadString(hInstance,IDS_ERR_INTERNAL,strText,_countof(strText))) strText[0]=0;
				MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
			}
			return ERR_MSIRES_NOTFOUND;
		}

		size64=SizeofResource(hInstance,hResInfo);
		hash=FNV_HASH0;
		int start=0;
		int pos=0;
		for (int i=0;i<chunkCount;i++)
		{
			const Chunk &chunk=pChunks[i];
			if (start<chunk.start2)
				hash=CalcFNVHash(pRes64+pos,chunk.start2-start,hash);
			hash=CalcFNVHash(pRes32+chunk.start1,chunk.len,hash);
			pos+=chunk.start2-start;
			start=chunk.start2+chunk.len;
		}
		if (pos<size64)
			hash=CalcFNVHash(pRes64+pos,size64-pos,hash);
	}
	else if (extractType==ARM64)
	{
		HRSRC hResInfo=FindResource(hInstance,MAKEINTRESOURCE(IDR_MSI_FILEARM64),L"MSI_FILE");
		if (hResInfo)
		{
			HGLOBAL hRes=LoadResource(hInstance,hResInfo);
			pResArm64=(unsigned char*)LockResource(hRes);
		}
		if (!pResArm64)
		{
			if (!bQuiet)
			{
				wchar_t strTitle[256];
				if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
				wchar_t strText[256];
				if (!LoadString(hInstance,IDS_ERR_INTERNAL,strText,_countof(strText))) strText[0]=0;
				MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
			}
			return ERR_MSIRES_NOTFOUND;
		}

		sizeArm64=SizeofResource(hInstance,hResInfo);
		hash=FNV_HASH0;
		int start=0;
		int pos=0;
		for (int i=0;i<chunkCount;i++)
		{
			const Chunk& chunk = pChunks[i];
			if (start<chunk.start2)
				hash=CalcFNVHash(pResArm64+pos,chunk.start2-start,hash);
			hash=CalcFNVHash(pRes32+chunk.start1,chunk.len,hash);
			pos+=chunk.start2-start;
			start=chunk.start2+chunk.len;
		}
		if (pos<sizeArm64)
			hash=CalcFNVHash(pResArm64+pos,sizeArm64-pos,hash);
	}
	else
		hash=CalcFNVHash(pRes32,size32);
	if (hash!=hash0)
	{
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_CORRUPTED,strText,_countof(strText))) strText[0]=0;
			wchar_t message[1024];
			Sprintf(message,_countof(message),strText,msiName);
			MessageBox(NULL,message,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_HASH_ERROR;
	}

	HANDLE hFile=CreateFile(msiName,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hFile==INVALID_HANDLE_VALUE)
	{
		wchar_t strTitle[256];
		if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
		wchar_t strText[256];
		if (!LoadString(hInstance,IDS_ERR_EXTRACT,strText,_countof(strText))) strText[0]=0;

		wchar_t message[1024];
		Sprintf(message,_countof(message),strText,msiName);
		if (!bQuiet)
		{
			MessageBox(NULL,message,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_MSI_EXTRACTFAIL;
	}
	if (extractType==x64)
	{
		int start=0;
		int pos=0;
		for (int i=0;i<chunkCount;i++)
		{
			const Chunk &chunk=pChunks[i];
			if (start<chunk.start2)
				WriteFileXOR(hFile,pRes64+pos,chunk.start2-start);
			WriteFileXOR(hFile,pRes32+chunk.start1,chunk.len);
			pos+=chunk.start2-start;
			start=chunk.start2+chunk.len;
		}
		if (pos<size64)
			WriteFileXOR(hFile,pRes64+pos,size64-pos);
	}
	else if (extractType==ARM64)
	{
		int start=0;
		int pos=0;
		for (int i=0;i<chunkCount;i++)
		{
			const Chunk &chunk=pChunks[i];
			if (start<chunk.start2)
				WriteFileXOR(hFile,pResArm64+pos,chunk.start2-start);
			WriteFileXOR(hFile,pRes32+chunk.start1,chunk.len);
			pos+=chunk.start2-start;
			start=chunk.start2+chunk.len;
		}
		if (pos<sizeArm64)
			WriteFileXOR(hFile,pResArm64+pos,sizeArm64-pos);
	}
	else
	{
		WriteFileXOR(hFile,pRes32,size32);
	}
	CloseHandle(hFile);

	return 0;
}

int APIENTRY wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow )
{
	// get installer version
	VS_FIXEDFILEINFO *pVer=NULL;
	{
		void *pRes=NULL;
		HRSRC hResInfo=FindResource(hInstance,MAKEINTRESOURCE(VS_VERSION_INFO),RT_VERSION);
		if (hResInfo)
		{
			HGLOBAL hRes=LoadResource(hInstance,hResInfo);
			pRes=LockResource(hRes);
		}
		if (pRes)
			pVer=(VS_FIXEDFILEINFO*)((char*)pRes+40);
	}

	int count;
	wchar_t *const *params=CommandLineToArgvW(lpCmdLine,&count);
	if (!params) count=0;

	ExtractType extractType=None;
	bool bQuiet=false;
	for (;count>0;count--,params++)
	{
		if (_wcsicmp(params[0],L"help")==0 || _wcsicmp(params[0],L"/help")==0 || _wcsicmp(params[0],L"/h")==0 || _wcsicmp(params[0],L"/?")==0)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[4096];
			if (!LoadString(hInstance,IDS_HELP,strText,_countof(strText))) strText[0]=0;

			MessageBox(NULL,strText,strTitle,MB_OK);
			return 0;
		}
		if (_wcsicmp(params[0],L"extract32")==0)
			extractType=x86;
		if (_wcsicmp(params[0],L"extract64")==0)
			extractType=x64;
		if (_wcsicmp(params[0],L"extractARM64")==0)
			extractType=ARM64;
		if (_wcsicmp(params[0],L"/qn")==0 || _wcsicmp(params[0],L"/q")==0 || _wcsicmp(params[0],L"/quiet")==0 || _wcsicmp(params[0],L"/passive")==0)
		{
			bQuiet=true;
		}
	}

	if (!pVer)
	{
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_INTERNAL,strText,_countof(strText))) strText[0]=0;
			MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_VERRES_NOTFOUND;
	}

	if (extractType != None)
	{
		wchar_t msiName[_MAX_PATH];
		Sprintf(msiName,_countof(msiName),L"OpenShellSetup%s_%d_%d_%d.msi",(extractType==x86?L"32":(extractType==x64?L"64":L"ARM64")),HIWORD(pVer->dwProductVersionMS),LOWORD(pVer->dwProductVersionMS),HIWORD(pVer->dwProductVersionLS));
		return ExtractMsi(hInstance,msiName,extractType,bQuiet);
	}

	// check Windows version
	#pragma warning(suppress:4996)
	WORD winVer=LOWORD(GetVersion());
	winVer=MAKEWORD(HIBYTE(winVer),LOBYTE(winVer));
	if (winVer<0x601)
	{
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_WIN7,strText,_countof(strText))) strText[0]=0;
			MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_WRONG_OS;
	}

	// dynamically link to IsWow64Process because it is not available for Windows 2000
	HMODULE hKernel32=GetModuleHandle(L"kernel32.dll");
	FIsWow64Process isWow64Process=(FIsWow64Process)GetProcAddress(hKernel32,"IsWow64Process");
	FWow64DisableWow64FsRedirection wow64DisableWow64FsRedirection=(FWow64DisableWow64FsRedirection)GetProcAddress(hKernel32,"Wow64DisableWow64FsRedirection");
	FWow64RevertWow64FsRedirection wow64RevertWow64FsRedirection=(FWow64RevertWow64FsRedirection)GetProcAddress(hKernel32,"Wow64RevertWow64FsRedirection");
	if (!isWow64Process || !wow64DisableWow64FsRedirection || !wow64RevertWow64FsRedirection)
	{
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_WIN7,strText,_countof(strText))) strText[0]=0;
			MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_WRONG_OS;
	}

	// Use IsWow64Process2 if it's available (Windows 10 1511+), otherwise fall back to IsWow64Process
	FIsWow64Process2 isWow64Process2=(FIsWow64Process2)GetProcAddress(hKernel32,"IsWow64Process2");
	if (isWow64Process2)
	{
		USHORT processMachine = 0, nativeMachine = 0;
		isWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine);
		extractType=nativeMachine==IMAGE_FILE_MACHINE_AMD64?x64:nativeMachine==IMAGE_FILE_MACHINE_ARM64?ARM64:x86;
	}
	else
	{
		BOOL x64=FALSE;
		isWow64Process(GetCurrentProcess(),&x64);
		extractType=x64?ExtractType::x64:x86;
	}

	wchar_t msiName[_MAX_PATH];
	Sprintf(msiName,_countof(msiName),L"%%ALLUSERSPROFILE%%\\OpenShellSetup%s_%d_%d_%d.msi",(extractType==x86?L"32":(extractType==x64?L"64":L"ARM64")),HIWORD(pVer->dwProductVersionMS),LOWORD(pVer->dwProductVersionMS),HIWORD(pVer->dwProductVersionLS));
	DoEnvironmentSubst(msiName,_countof(msiName));
	int ex=ExtractMsi(hInstance,msiName,extractType,bQuiet);
	if (ex) return ex;

	wchar_t cmdLine[2048];
	if (wcsstr(lpCmdLine,L"%MSI%") || wcsstr(lpCmdLine,L"%msi%"))
	{
		SetEnvironmentVariable(L"MSI",msiName);
		Sprintf(cmdLine,_countof(cmdLine),L"msiexec.exe %s",lpCmdLine);
		DoEnvironmentSubst(cmdLine,_countof(cmdLine));
	}
	else
	{
		Sprintf(cmdLine,_countof(cmdLine),L"msiexec.exe /i \"%s\" %s",msiName,lpCmdLine);
	}

	// On ARM64 we must launch msiexec.exe from system32 and not syswow64 as would otherwise happen
	PVOID wow64FsRedirVal=NULL;
	if (extractType == ARM64)
		Wow64DisableWow64FsRedirection(&wow64FsRedirVal);

	// start the installer
	STARTUPINFO startupInfo={sizeof(startupInfo)};
	PROCESS_INFORMATION processInfo;
	memset(&processInfo,0,sizeof(processInfo));
	BOOL ret=CreateProcess(NULL,cmdLine,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo);
	if (extractType == ARM64)
		Wow64RevertWow64FsRedirection(wow64FsRedirVal);
	if (!ret)
	{
		DeleteFile(msiName);
		if (!bQuiet)
		{
			wchar_t strTitle[256];
			if (!LoadString(hInstance,IDS_APP_TITLE,strTitle,_countof(strTitle))) strTitle[0]=0;
			wchar_t strText[256];
			if (!LoadString(hInstance,IDS_ERR_MSIEXEC,strText,_countof(strText))) strText[0]=0;
			MessageBox(NULL,strText,strTitle,MB_OK|MB_ICONERROR);
		}
		return ERR_MSIEXEC;
	}
	else
	{
		CloseHandle(processInfo.hThread);
		// wait for the installer to finish
		WaitForSingleObject(processInfo.hProcess,INFINITE);
		DWORD code;
		GetExitCodeProcess(processInfo.hProcess,&code);
		CloseHandle(processInfo.hProcess);
		DeleteFile(msiName);
		return code;
	}
}
