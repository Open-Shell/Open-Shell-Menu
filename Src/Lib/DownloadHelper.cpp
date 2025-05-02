// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include <stdafx.h>
#include "resource.h"
#include "DownloadHelper.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "LanguageSettingsHelper.h"
#include "ResourceHelper.h"
#include "ComHelper.h"
#include "FNVHash.h"
#include "StringUtils.h"
#include "Translations.h"
#include <nlohmann/json.hpp>
#include <wininet.h>
#include <softpub.h>

//#define UPDATE_LOG L"-|%LOCALAPPDATA%\\OpenShell\\UpdateLog.txt"

const __int64 TIME_DIVISOR=360000000; // divisor for FILETIME to get 0.01 hour precision
const int TIME_PRECISION=100; // 0.01 hour precision

class CProgressDlg: public CResizeableDlg<CProgressDlg>
{
public:
	CProgressDlg( void );
	BEGIN_MSG_MAP( CProgressDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_CLEAR, OnClear )
		COMMAND_HANDLER( IDCANCEL, BN_CLICKED, OnCancel )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
	END_RESIZE_MAP

	void Create( HWND owner, const wchar_t *title );
	void SetText( const wchar_t *text ); // may block if messages are not processed
	void SetProgress( int progress ); // -1 for undefined
	void EnableCancel( bool bEnabled );
	bool IsCanceled( void ) const { return m_bCanceled; }

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnClear( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );

private:
	bool m_bCanceled;
	bool m_bMarquee;
	CWindow m_Progress;

	enum
	{
		COMMAND_SET_PROGRESS,
		COMMAND_ENABLE_CANCEL,
	};
};

CProgressDlg::CProgressDlg( void )
{
	m_bCanceled=m_bMarquee=false;
}

LRESULT CProgressDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	InitResize(MOVE_HORIZONTAL|MOVE_VERTICAL);
	m_Progress=GetDlgItem(IDC_PROGRESS);
	m_Progress.SendMessage(PBM_SETRANGE,0,MAKELPARAM(0,100));
	return TRUE;
}

void CProgressDlg::Create( HWND owner, const wchar_t *title )
{
	DLGTEMPLATE *pTemplate=LoadDialogEx(IDD_PROGRESS);
	CResizeableDlg<CProgressDlg>::Create(owner,pTemplate);
	SetWindowText(title);
	ShowWindow(SW_SHOW);
	SetDlgItemText(IDC_MESSAGE,L"");
}

void CProgressDlg::SetText( const wchar_t *text )
{
	SetDlgItemText(IDC_MESSAGE,text);
}

void CProgressDlg::SetProgress( int progress )
{
	PostMessage(WM_CLEAR,COMMAND_SET_PROGRESS,progress);
}

void CProgressDlg::EnableCancel( bool bEnabled )
{
	PostMessage(WM_CLEAR,COMMAND_ENABLE_CANCEL,bEnabled?1:0);
}

LRESULT CProgressDlg::OnClear( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==COMMAND_SET_PROGRESS)
	{
		int progress=(int)lParam;
		if (progress>=0)
		{
			if (m_bMarquee)
			{
				m_Progress.SendMessage(PBM_SETMARQUEE,FALSE);
				m_bMarquee=false;
			}
			m_Progress.SendMessage(PBM_SETPOS,progress);
		}
		else
		{
			if (!m_bMarquee)
			{
				m_Progress.SendMessage(PBM_SETMARQUEE,TRUE,50);
				m_Progress.SetWindowLong(GWL_STYLE,m_Progress.GetWindowLong(GWL_STYLE)|PBS_MARQUEE);
				m_bMarquee=true;
			}
			m_Progress.SendMessage(PBM_STEPIT);
		}
	}
	else if (wParam==COMMAND_ENABLE_CANCEL)
	{
		GetDlgItem(IDCANCEL).EnableWindow(lParam!=0);
	}
	return 0;
}

LRESULT CProgressDlg::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	m_bCanceled=true;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static bool g_bCheckingVersion;

enum TDownloadResult
{
	DOWNLOAD_OK,
	DOWNLOAD_CANCEL,

	// errors
	DOWNLOAD_FIRST_ERROR,
	DOWNLOAD_INTERNET=DOWNLOAD_FIRST_ERROR,
	DOWNLOAD_START,
	DOWNLOAD_FAIL,
};

// Downloads a file
// filename - returns the name of the downloaded file
static TDownloadResult DownloadFile( const wchar_t *url, std::vector<char> &buf, CString *pFilename, bool bAcceptCached, CProgressDlg *pProgress, TSettingsComponent component )
{
	const wchar_t *compName=L"Open-Shell";
	switch (component)
	{
		case COMPONENT_EXPLORER: compName=L"Classic Explorer"; break;
		case COMPONENT_MENU: compName=L"Open-Shell Menu"; break;
		case COMPONENT_IE: compName=L"Classic IE"; break;
		case COMPONENT_UPDATE: compName=L"Open-Shell Update"; break;
	}

	DWORD compVersion=GetVersionEx(g_Instance);

	wchar_t agent[100];
	Sprintf(agent,_countof(agent),L"%s %d.%d.%d (Win %x)",compName,compVersion>>24,(compVersion>>16)&0xFF,compVersion&0xFFFF,GetWinVersion());

	TDownloadResult res=DOWNLOAD_OK;
	HINTERNET hInternet=InternetOpen(agent,INTERNET_OPEN_TYPE_PRECONFIG,NULL,NULL,0);
	if (!hInternet)
		return DOWNLOAD_INTERNET;
	if (pProgress && pProgress->IsCanceled())
		res=DOWNLOAD_CANCEL;

	wchar_t host[256];
	wchar_t file[256];
	URL_COMPONENTS components={sizeof(components)};
	components.lpszHostName=host;
	components.dwHostNameLength=_countof(host);
	components.lpszUrlPath=file;
	components.dwUrlPathLength=_countof(file);
	InternetCrackUrl(url,0,0,&components);
	int time=GetTickCount();
	if (pProgress)
		pProgress->SetText(LoadStringEx(IDS_PROGRESS_CONNECT));
	HINTERNET hConnect=InternetConnect(hInternet,host,components.nPort,L"",L"",INTERNET_SERVICE_HTTP,0,0);
	if (hConnect)
	{
		if (pProgress && pProgress->IsCanceled())
			res=DOWNLOAD_CANCEL;
		
		if (res==DOWNLOAD_OK)
		{
			const wchar_t* accept[] = { L"*/*",NULL };
			HINTERNET hRequest=HttpOpenRequest(hConnect,L"GET",file,NULL,NULL,accept,((components.nScheme==INTERNET_SCHEME_HTTPS)?INTERNET_FLAG_SECURE:0)|(bAcceptCached?0:INTERNET_FLAG_RELOAD),0);
			if (hRequest)
			{
				if (pProgress && pProgress->IsCanceled())
					res=DOWNLOAD_CANCEL;
				// get size (may be missing)
				DWORD fileSize;
				if (res==DOWNLOAD_OK)
				{
					if (HttpSendRequest(hRequest,NULL,0,NULL,0))
					{
						DWORD len=sizeof(fileSize);
						DWORD index=0;
						if (!HttpQueryInfo(hRequest,HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER,&fileSize,&len,&index))
							fileSize=0;
					}
					else
						res=DOWNLOAD_START;
				}

				// get file name (must be after HttpSendRequest to resolve any redirections)
				if (res==DOWNLOAD_OK && pFilename)
				{
					wchar_t buf[1024];
					DWORD len=sizeof(buf);
					if (InternetQueryOption(hRequest,INTERNET_OPTION_URL,buf,&len))
					{
						const wchar_t *name=wcsrchr(buf,'/');
						if (name && name[1])
							*pFilename=name+1;
						else
							res=DOWNLOAD_START;
					}
					else
						res=DOWNLOAD_START;
				}

				if (res==DOWNLOAD_OK)
				{
					if (pProgress)
					{
						pProgress->SetText(LoadStringEx(IDS_PROGRESS_DOWNLOAD));
						if (fileSize==0)
							pProgress->SetProgress(-1);
					}
					int CHUNK_SIZE=32768;
					DWORD size=0;
					buf.reserve(fileSize+CHUNK_SIZE);
					while (1)
					{
						if (pProgress && pProgress->IsCanceled())
						{
							res=DOWNLOAD_CANCEL;
							break;
						}
						DWORD dwSize;
						buf.resize(size+CHUNK_SIZE);
						if (!InternetReadFile(hRequest,&buf[size],CHUNK_SIZE,&dwSize))
						{
							res=DOWNLOAD_FAIL;
							break;
						}
						if (dwSize==0)
							break;
						size+=dwSize;
						if (pProgress && fileSize)
							pProgress->SetProgress(size*100/fileSize);
					}
					buf.resize(size);
				}
				InternetCloseHandle(hRequest);
			}
			else
				res=DOWNLOAD_START;
		}
		InternetCloseHandle(hConnect);
	}
	else
		res=DOWNLOAD_START;

	InternetCloseHandle(hInternet);
	return res;
}

static int SaveFile( const wchar_t *fname, const std::vector<char> &buf )
{
	HANDLE h=CreateFile(fname,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	int err=0;
	if (h!=INVALID_HANDLE_VALUE)
	{
		DWORD size=(DWORD)buf.size();
		DWORD q;
		if (WriteFile(h,&buf[0],size,&q,NULL) && q==size)
			CloseHandle(h);
		else
		{
			err=GetLastError();
#ifdef UPDATE_LOG
			LogToFile(UPDATE_LOG,L"Failed to write file '%s': 0x%08X",fname,err);
#endif
			CloseHandle(h);
			DeleteFile(fname);
		}
	}
	else
	{
		err=GetLastError();
#ifdef UPDATE_LOG
		LogToFile(UPDATE_LOG,L"Failed to save file '%s': 0x%08X",fname,err);
#endif
	}
	return err;
}

struct VersionCheckParams
{
	TVersionCheck check;
	TSettingsComponent component;
	tNewVersionCallback callback;
	CProgressDlg *progress;
	bool nightly = false;
};

// 0 - fail, 1 - success, 2 - cancel
static DWORD WINAPI ThreadVersionCheck( void *param )
{
	VersionCheckParams params=*(VersionCheckParams*)param;
	delete param;
	param=NULL;
	ULONGLONG curTimeL;
	GetSystemTimeAsFileTime((FILETIME*)&curTimeL);
	DWORD curTime=(DWORD)(curTimeL/TIME_DIVISOR); // in 0.01 hours

	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\OpenShell")!=ERROR_SUCCESS)
	{
		g_bCheckingVersion=false;
		return 0;
	}
	DWORD curVersion=GetVersionEx(g_Instance);

	bool res = false;
	VersionData data;

	{
		auto load = data.Load(!params.nightly);

#ifdef UPDATE_LOG
		LogToFile(UPDATE_LOG, L"Load result: %d", load);
#endif
		res = (load == VersionData::LOAD_OK);
	}

	curTime+=(rand()*TIME_PRECISION)/(RAND_MAX+1)-(TIME_PRECISION/2); // add between -30 and 30 minutes to randomize access
	if (!res)
	{
		g_bCheckingVersion=false;
		// if the check failed pretend we checked 5 days ago (so we check again in 48 hours)
		if (curTime>24*5*TIME_PRECISION)
			regKey.SetDWORDValue(L"LastUpdateTime",curTime-24*5*TIME_PRECISION);
		return 0;
	}

	regKey.SetDWORDValue(L"LastUpdateTime",curTime);

	{
		DWORD remindedVersion;
		if (regKey.QueryDWORDValue(L"RemindedVersion",remindedVersion)!=ERROR_SUCCESS)
			remindedVersion=0;
		data.bNewVersion=(data.newVersion>curVersion);
		data.bIgnoreVersion=(data.bNewVersion && data.newVersion<=remindedVersion);
	}

	data.bValid=true;
	if (params.check==CHECK_UPDATE)
	{
		params.callback(data);
		g_bCheckingVersion=false;
		return 1;
	}
	if (data.bNewVersion && !data.bIgnoreVersion)
		params.callback(data);
	g_bCheckingVersion=false;
	return 0;
}

DWORD CheckForNewVersion( HWND owner, TSettingsComponent component, TVersionCheck check, tNewVersionCallback callback )
{
	if (g_bCheckingVersion) return 0;

	if (check==CHECK_UPDATE)
	{
		g_bCheckingVersion=true;
		VersionCheckParams *params=new VersionCheckParams;
		params->check=check;
		params->component=component;
		params->callback=callback;
		params->progress=NULL;

		// check the Update setting (uses the current value in the registry, not the one from memory
		{
			CRegKey regSettings, regSettingsUser, regPolicy, regPolicyUser;
			bool bUpgrade = OpenSettingsKeys(COMPONENT_SHARED, regSettings, regSettingsUser, regPolicy, regPolicyUser);

			CSetting settings[] = {
				{L"Nightly",CSetting::TYPE_BOOL,0,0,0},
				{NULL}
			};

			settings[0].LoadValue(regSettings, regSettingsUser, regPolicy, regPolicyUser);

			params->nightly = GetSettingBool(settings[0]);
		}

		if (!owner)
			return ThreadVersionCheck(params);

		CProgressDlg progress;
		params->progress=&progress;
		progress.Create(owner,LoadStringEx(IDS_PROGRESS_TITLE_CHECK));

		HANDLE hThread=CreateThread(NULL,0,ThreadVersionCheck,params,0,NULL);

		while (1)
		{
			DWORD wait=MsgWaitForMultipleObjects(1,&hThread,FALSE,INFINITE,QS_ALLINPUT);
			if (wait!=WAIT_OBJECT_0+1)
				break;
			MSG msg;
			while (PeekMessage(&msg,0,0,0,PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		progress.DestroyWindow();
		DWORD exit;
		if (!GetExitCodeThread(hThread,&exit))
			exit=0;
		CloseHandle(hThread);
		return exit;
	}
	else
	{
		ULONGLONG curTimeL;
		GetSystemTimeAsFileTime((FILETIME*)&curTimeL);
		DWORD curTime=(DWORD)(curTimeL/TIME_DIVISOR); // in 0.01 hours

		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\OpenShell")!=ERROR_SUCCESS)
			regKey.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\OpenShell");

		DWORD lastTime;
		if (regKey.QueryDWORDValue(L"LastUpdateTime",lastTime)!=ERROR_SUCCESS)
			lastTime=0;
		if ((int)(curTime-lastTime)<168*TIME_PRECISION)
			return 0; // check weekly

		// check the Update setting (uses the current value in the registry, not the one from memory
		bool nightly = false;
		{
			CRegKey regSettings, regSettingsUser, regPolicy, regPolicyUser;
			bool bUpgrade=OpenSettingsKeys(COMPONENT_SHARED,regSettings,regSettingsUser,regPolicy,regPolicyUser);

			CSetting settings[]={
				{L"Update",CSetting::TYPE_BOOL,0,0,1},
				{L"Nightly",CSetting::TYPE_BOOL,0,0,0},
				{NULL}
			};

			settings[0].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			settings[1].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);

			if (!GetSettingBool(settings[0]))
				return 0;
			nightly = GetSettingBool(settings[1]);
		}

		VersionCheckParams *params=new VersionCheckParams;
		params->check=check;
		params->component=component;
		params->callback=callback;
		params->progress=NULL;
		params->nightly=nightly;

		g_bCheckingVersion=true;
		if (check==CHECK_AUTO_WAIT)
			return ThreadVersionCheck(params);
		HANDLE hThread=CreateThread(NULL,0,ThreadVersionCheck,params,0,NULL);
		CloseHandle(hThread);
		return hThread!=NULL?1:0;
	}
}

static CString LoadStringEx( HMODULE hModule, int stringId, int langId )
{
	CString res;

	HRSRC hrsrc=FindResourceEx(hModule,RT_STRING,MAKEINTRESOURCE((stringId>>4)+1),(WORD)langId);
	if (!hrsrc) return res;
	HGLOBAL hglb=LoadResource(hModule,hrsrc);
	if (!hglb) return res;
	WORD *data=(WORD*)LockResource(hglb);
	if (!data) return res;

	stringId&=15;
	for (int i=0;i<stringId;i++)
		data+=*data+1;
	int len=*data;
	data++;

	if (len)
	{
		wchar_t *buf=res.GetBuffer(len);
		memcpy(buf,data,len*2);
		buf[len]=0;
		res.ReleaseBufferSetLength(len);
	}

	UnlockResource(hglb);
	return res;
}

static bool VerifyDigitalCertificate( const wchar_t *fname, const wchar_t *signer )
{
	// verify the certificate
	WINTRUST_FILE_INFO fileData={sizeof(fileData),fname};

	WINTRUST_DATA winTrustData={sizeof(winTrustData)};
	winTrustData.dwUIChoice=WTD_UI_NONE;
	winTrustData.fdwRevocationChecks=WTD_REVOKE_NONE;
	winTrustData.dwUnionChoice=WTD_CHOICE_FILE;
	winTrustData.dwStateAction=WTD_STATEACTION_VERIFY;
	winTrustData.pFile=&fileData;

	GUID policyGUID=WINTRUST_ACTION_GENERIC_VERIFY_V2;
	LONG lStatus=WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&policyGUID,&winTrustData);
#ifdef UPDATE_LOG
	LogToFile(UPDATE_LOG,L"Verify Trust Result: 0x%08X",lStatus);
#endif

	winTrustData.dwStateAction=WTD_STATEACTION_CLOSE;
	WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&policyGUID,&winTrustData);
	if (lStatus!=ERROR_SUCCESS && lStatus!=CERT_E_EXPIRED)
		return false;

	// verify the signer name
	CString signerName;

	HCERTSTORE hStore;
	HCRYPTMSG hMsg;
	if (CryptQueryObject(CERT_QUERY_OBJECT_FILE,fname,CERT_QUERY_CONTENT_FLAG_ALL,CERT_QUERY_FORMAT_FLAG_ALL,0,NULL,NULL,NULL,&hStore,&hMsg,NULL) && hStore && hMsg)
	{
		DWORD len;
		if (CryptMsgGetParam(hMsg,CMSG_SIGNER_INFO_PARAM,0,NULL,&len) && len>0)
		{
			std::vector<char> signerInfo(len);
			CMSG_SIGNER_INFO *pSignerInfo=(CMSG_SIGNER_INFO*)&signerInfo[0];
			if (CryptMsgGetParam(hMsg,CMSG_SIGNER_INFO_PARAM,0,pSignerInfo,&len))
			{
				CERT_INFO certInfo;
				certInfo.Issuer=pSignerInfo->Issuer;
				certInfo.SerialNumber=pSignerInfo->SerialNumber;
				PCCERT_CONTEXT pCertContext=CertFindCertificateInStore(hStore,X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,0,CERT_FIND_SUBJECT_CERT,&certInfo,NULL);
				if (pCertContext)
				{
					len=CertGetNameString(pCertContext,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,NULL,NULL,0);
					if (len>1)
					{
						wchar_t *buf=signerName.GetBuffer(len-1);
						if (buf)
						{
							DWORD res=CertGetNameString(pCertContext,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,NULL,buf,len);
							signerName.ReleaseBufferSetLength(len-1);
							if (!res) signerName.Empty();
						}

#ifdef UPDATE_LOG
						LogToFile(UPDATE_LOG,L"Signer: '%s', '%s'",signer,signerName);
#endif
					}
					else
					{
#ifdef UPDATE_LOG
						LogToFile(UPDATE_LOG,L"CertGetNameString failed");
#endif
					}
				}
				else
				{
#ifdef UPDATE_LOG
					LogToFile(UPDATE_LOG,L"CertFindCertificateInStore failed");
#endif
				}
			}
			else
			{
#ifdef UPDATE_LOG
				LogToFile(UPDATE_LOG,L"CryptMsgGetParam2 failed");
#endif
			}
		}
		else
		{
#ifdef UPDATE_LOG
			LogToFile(UPDATE_LOG,L"CryptMsgGetParam1 failed");
#endif
		}
		if (hStore) CertCloseStore(hStore,0);
		if (hMsg) CryptMsgClose(hMsg);
	}
	else
	{
#ifdef UPDATE_LOG
		LogToFile(UPDATE_LOG,L"CryptQueryObject failed");
#endif
	}

	return (signerName==signer);
}

void VersionData::Clear( void )
{
	bValid=false;
	newVersion=0;
	downloadUrl.Empty();
	downloadSigner.Empty();
	news.Empty();
	updateLink="https://github.com/Open-Shell/Open-Shell-Menu/releases";
	bNewVersion=bIgnoreVersion=false;
}

void VersionData::Swap( VersionData &data )
{
	std::swap(bValid,data.bValid);
	std::swap(newVersion,data.newVersion);
	std::swap(downloadUrl,data.downloadUrl);
	std::swap(downloadSigner,data.downloadSigner);
	std::swap(news,data.news);
	std::swap(updateLink,data.updateLink);
	std::swap(bNewVersion,data.bNewVersion);
	std::swap(bIgnoreVersion,data.bIgnoreVersion);
}

std::vector<char> DownloadUrl(const wchar_t* url)
{
#ifdef UPDATE_LOG
	LogToFile(UPDATE_LOG, L"URL: %s", url);
#endif

	std::vector<char> buffer;
	TDownloadResult download = DownloadFile(url, buffer, nullptr, false, nullptr, COMPONENT_UPDATE);

#ifdef UPDATE_LOG
	LogToFile(UPDATE_LOG, L"Download result: %d", download);
#endif

	if (download != DOWNLOAD_OK)
		buffer.clear();

	return buffer;
}

using namespace nlohmann;

VersionData::TLoadResult VersionData::Load(bool official)
{
	Clear();

	std::wstring baseUrl = L"https://api.github.com/repos/Open-Shell/Open-Shell-Menu/releases";
	if (official)
		baseUrl += L"/latest";

	auto buf = DownloadUrl(baseUrl.c_str());
	if (buf.empty())
		return LOAD_ERROR;

	try
	{
		auto jsonData = json::parse(buf.begin(), buf.end());
		auto& data = jsonData;

		if (official)
		{
			// skip prerelease versions (just in case)
			if (data["prerelease"].get<bool>())
				return LOAD_BAD_VERSION;
		}
		else
		{
			// we've got list of versions (release and pre-release)
			// lets pick first one (that should be the latest one)
			data = jsonData[0];
		}

		// make sure we didn't get draft release (for whatever reason)
		if (data["draft"].get<bool>())
			return LOAD_BAD_VERSION;

		// get version from tag name
		auto tag = data["tag_name"].get<std::string>();
		if (tag.empty())
			return LOAD_BAD_FILE;

		int v1, v2, v3;
		if (sscanf_s(tag.c_str(), "v%d.%d.%d", &v1, &v2, &v3) != 3)
			return LOAD_BAD_FILE;

		newVersion = (v1 << 24) | (v2 << 16) | v3;

		// installer url
		std::string url;
		for (const auto& asset : data["assets"])
		{
			if (asset["name"].get<std::string>().find("OpenShellSetup") == 0)
			{
				url = asset["browser_download_url"].get<std::string>();
				break;
			}
		}

		if (url.empty())
			return LOAD_BAD_FILE;

		downloadUrl.Append(CA2T(url.c_str()));

		// changelog
		auto body = data["body"].get<std::string>();
		if (!body.empty())
		{
			auto name = data["name"].get<std::string>();
			if (!name.empty())
			{
				news.Append(CA2T(name.c_str()));
				news.Append(L"\r\n\r\n");
			}

			news.Append(CA2T(body.c_str()));
			news.Replace(L"\\n", L"\n");
			news.Replace(L"\\r", L"\r");
		}

		return LOAD_OK;
	}
	catch (...)
	{
		return LOAD_BAD_FILE;
	}
}

struct DownloadFileParams
{
	// input
	const wchar_t *url;
	const wchar_t *signer;
	DWORD hash;
	const wchar_t *path;
	CString fname; // file name on input, full path on output
	CProgressDlg *progress;
	bool bAcceptCached;
	TSettingsComponent component;

	// output
	TDownloadResult downloadRes;
	int saveRes;
	bool valid;
};

static DWORD WINAPI ThreadDownloadFile( void *param )
{
	DownloadFileParams &params=*(DownloadFileParams*)param;
	param=NULL;
	params.valid=false;
	params.saveRes=0;

	std::vector<char> buf;
	params.downloadRes=DownloadFile(params.url,buf,params.fname.IsEmpty()?&params.fname:NULL,params.bAcceptCached,params.progress,params.component);
	if (params.downloadRes==DOWNLOAD_CANCEL || params.downloadRes>=DOWNLOAD_FIRST_ERROR)
		return 0;

	params.valid=!buf.empty();
	if (!params.valid)
		return 0;

	// validate hash
	if (params.hash)
	{
		if (params.progress)
			params.progress->SetText(LoadStringEx(IDS_PROGRESS_VERIFY));

		params.valid=(CalcFNVHash(&buf[0],(int)buf.size(),FNV_HASH0)==params.hash);
		if (!params.valid)
			return 0;
	}

	// save
	if (params.progress)
	{
		params.progress->SetText(LoadStringEx(IDS_PROGRESS_SAVE));
		params.progress->EnableCancel(false);
		params.progress->SetProgress(-1);
	}
	wchar_t fname[_MAX_PATH];
	Sprintf(fname,_countof(fname),L"%s\\%s",params.path,params.fname);
	params.fname=fname;
	params.saveRes=SaveFile(fname,buf);
	if (params.saveRes)
		return 0;

	// validate signer
/*
	if (params.signer)
	{
		if (params.progress)
			params.progress->SetText(LoadStringEx(IDS_PROGRESS_VERIFY));
		params.valid=VerifyDigitalCertificate(fname,params.signer);
		if (!params.valid)
		{
			DeleteFile(fname);
			return 0;
		}
	}
*/
	return 0;
}

DWORD DownloadNewVersion( HWND owner, TSettingsComponent component, const wchar_t *url, const wchar_t *signer, CString &fname, CString &error )
{
	CComString pPath;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads,0,NULL,&pPath)) || !pPath)
	{
		if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents,0,NULL,&pPath)) || !pPath)
		{
			if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop,0,NULL,&pPath)) || !pPath)
			{
				error=LoadStringEx(IDS_INST_DOWNLOAD_FAIL);
				return 0;
			}
		}
	}

	CProgressDlg progress;
	progress.Create(owner,LoadStringEx(IDS_PROGRESS_TITLE_DOWNLOAD));

	DownloadFileParams params;
	params.url=url;
	params.signer=signer;
	params.hash=0;
	params.path=pPath;
	params.progress=&progress;
	params.bAcceptCached=true;
	params.component=component;

	{
		const wchar_t* name = wcsrchr(url, '/');
		if (name && name[1])
			params.fname.Append(name+1);
	}

	HANDLE hThread=CreateThread(NULL,0,ThreadDownloadFile,&params,0,NULL);

	while (1)
	{
		DWORD wait=MsgWaitForMultipleObjects(1,&hThread,FALSE,INFINITE,QS_ALLINPUT);
		if (wait!=WAIT_OBJECT_0+1)
			break;
		MSG msg;
		while (PeekMessage(&msg,0,0,0,PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	progress.DestroyWindow();
	CloseHandle(hThread);

	if (params.downloadRes==DOWNLOAD_CANCEL)
		return 2;
	if (params.downloadRes==DOWNLOAD_INTERNET)
	{
		error=LoadStringEx(IDS_INTERNET_FAIL);
		return 0;
	}
	else if (params.downloadRes==DOWNLOAD_START)
	{
		error=LoadStringEx(IDS_INITIATE_FAIL);
		return 0;
	}
	else if (params.downloadRes==DOWNLOAD_FAIL)
	{
		error=LoadStringEx(IDS_INST_DOWNLOAD_FAIL);
		return 0;
	}

	if (params.saveRes)
	{
		wchar_t msg[256];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,params.saveRes,0,msg,_countof(msg),NULL);
		error.Format(LoadStringEx(IDS_INST_SAVE_FAIL),params.fname);
		error+="\r\n";
		error+=msg;
		return 0;
	}

	if (!params.valid)
	{
		error=LoadStringEx(IDS_INST_DOWNLOAD_FAIL);
		return 0;
	}

	fname=params.fname;
	return 1;
}
