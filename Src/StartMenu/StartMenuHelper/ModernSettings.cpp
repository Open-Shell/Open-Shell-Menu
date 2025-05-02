// Modern settings helper

// - parse modern settings definitions from %windir%\ImmersiveControlPanel\Settings\AllSystemSettings_{253E530E-387D-4BC2-959D-E6F86122E5F2}.xml
// - store cached data (parsed settings, localized strings) in %LOCALAPPDATA%\OpenShell\ModernSettings.dat
// - provide mapped view over cached data

#include "stdafx.h"
#include "ModernSettings.h"
#include "ResourceHelper.h"
#include <appmodel.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <mutex>

enum class Id : uint32_t
{
	Header = 'SMSO',
	Undef = 0,
	Blob,
	FileName,
	DeepLink,
	Icon,
	Glyph,
	PageId,
	HostId,
	GroupId,
	SettingId,
	Description,
	Keywords,
};

#pragma pack(1)
struct FileHdr
{
	uint32_t openShellVersion = GetVersionEx(g_Instance);
	uint32_t windowsVersion = GetVersionEx(GetModuleHandle(L"user32.dll"));
	uint32_t userLanguageId = GetUserDefaultUILanguage();

	bool operator==(const FileHdr& other) const
	{
		return (windowsVersion == other.windowsVersion) &&
		       (openShellVersion == other.openShellVersion) &&
		       (userLanguageId == other.userLanguageId);
	}
};

struct ItemHdr
{
	Id       id;
	uint32_t size;

	const uint8_t* data() const
	{
		return (const uint8_t*)this + sizeof(*this);
	}

	const ItemHdr* next() const
	{
		return (const ItemHdr*)(data() + size);
	}

	std::wstring_view asString() const
	{
		std::wstring_view retval((const wchar_t*)data(), size / sizeof(wchar_t));
		if (!retval.empty() && retval.back() == 0)
		{
			retval.remove_suffix(1);
			return retval;
		}

		return {};
	}
};
#pragma pack()

class AttributeWriter
{
public:
	std::vector<uint8_t> buffer()
	{
		return std::move(m_buffer);
	}

	void addBlob(Id id, const void* data, size_t size)
	{
		ItemHdr hdr{ id, (uint32_t)size };
		append(&hdr, sizeof(hdr));
		append(data, size);
	}

	void addString(Id id, const std::wstring& str)
	{
		if (!str.empty())
			addBlob(id, str.data(), (str.size() + 1) * sizeof(str[0]));
	}

private:
	void append(const void* data, size_t size)
	{
		m_buffer.insert(m_buffer.end(), (const uint8_t*)data, (const uint8_t*)data + size);
	}

	std::vector<uint8_t> m_buffer;
};

static void ProcessAttributes(const void* buffer, size_t size, std::function<void(const ItemHdr&)> callback)
{
	if (size < sizeof(ItemHdr))
		return;

	auto item = (const ItemHdr*)buffer;
	auto last = (const ItemHdr*)((const uint8_t*)buffer + size);

	while (item < last)
	{
		auto next = item->next();
		if (next <= item || next > last)
			break;

		callback(*item);

		item = next;
	}
}

///

static std::wstring GetPackageFullName(const wchar_t* packageFamily)
{
	static auto pGetPackagesByPackageFamily = static_cast<decltype(&GetPackagesByPackageFamily)>((void*)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetPackagesByPackageFamily"));
	if (pGetPackagesByPackageFamily)
	{
		UINT32 count = 0;
		UINT32 bufferLength = 0;

		if (pGetPackagesByPackageFamily(packageFamily, &count, nullptr, &bufferLength, nullptr) == ERROR_INSUFFICIENT_BUFFER && count > 0)
		{
			std::vector<PWSTR> names(count);
			std::vector<WCHAR> buffer(bufferLength);

			if (pGetPackagesByPackageFamily(packageFamily, &count, names.data(), &bufferLength, buffer.data()) == ERROR_SUCCESS && count > 0)
				return names[0];
		}
	}

	return {};
}

static std::pair<std::wstring_view, std::wstring_view> ParseResourceString(const wchar_t* resourceString)
{
	std::wstring_view str = resourceString;

	if (str[0] == '@' && str[1] == '{')
	{
		str.remove_prefix(2);
		if (str.back() == '}')
			str.remove_suffix(1);

		auto pos = str.find('?');
		if (pos != str.npos)
			return { str.substr(0, pos), str.substr(pos + 1) };
	}

	return {};
}

static std::wstring FormatResourceString(const std::wstring_view& package, const std::wstring_view& resource)
{
	std::wstring retval(L"@{");

	retval += package;
	retval += L"?";
	retval += resource;
	retval += L"}";

	return retval;
}


static std::wstring TranslateIndirectString(const WCHAR* string)
{
	std::wstring retval;
	retval.resize(1024);

	auto hr = ::SHLoadIndirectString(string, retval.data(), (UINT)retval.size(), nullptr);

	if (hr == E_INVALIDARG)
	{
		auto [package, resource] = ParseResourceString(string);
		if (!package.empty() && !resource.empty())
		{
			auto fullPackage = GetPackageFullName(std::wstring(package).c_str());
			if (!fullPackage.empty())
			{
				auto fullStr = FormatResourceString(fullPackage, resource);
				hr = ::SHLoadIndirectString(fullStr.c_str(), retval.data(), (UINT)retval.size(), nullptr);
			}
		}
	}

	if (SUCCEEDED(hr))
	{
		retval.resize(wcslen(retval.data()));
		return retval;
	}

	return {};
}

static std::wstring TranslateIndirectMultiString(const WCHAR* string)
{
	std::wstring retval;
	std::wstring_view str(string);

	// remove '@'
	str.remove_prefix(1);

	while (!str.empty())
	{
		auto len = str.find(L'@', 1);
		if (len == std::wstring::npos)
			len = str.length();

		std::wstring tmp(str.substr(0, len));
		retval += TranslateIndirectString(tmp.c_str());

		str.remove_prefix(len);
	}

	return retval;
}

static std::wstring GetTranslatedString(CComPtr<IXMLDOMNode>& parent, const WCHAR* name)
{
	CComPtr<IXMLDOMNode> node;
	if (parent->selectSingleNode(CComBSTR(name), &node) == S_OK)
	{
		CComBSTR value;
		if (node->get_text(&value) == S_OK)
		{
			if (value[0] == L'@')
			{
				if (value[1] == L'@')
					return TranslateIndirectMultiString(value);
				else
					return TranslateIndirectString(value);
			}
			else
			{
				return (LPWSTR)value;
			}
		}
	}

	return {};
}

static void ParseFileName(CComPtr<IXMLDOMNode>& parent, AttributeWriter& writer)
{
	writer.addString(Id::FileName, GetTranslatedString(parent, L"Filename"));
}

static void ParseApplicationInformation(CComPtr<IXMLDOMNode>& parent, AttributeWriter& writer)
{
	CComPtr<IXMLDOMNode> node;
	if (parent->selectSingleNode(CComBSTR(L"ApplicationInformation"), &node) == S_OK)
	{
		writer.addString(Id::DeepLink, GetTranslatedString(node, L"DeepLink"));
		writer.addString(Id::Icon, GetTranslatedString(node, L"Icon"));
		writer.addString(Id::Glyph, GetTranslatedString(node, L"Glyph"));
	}
}

static void ParseSettingIDs(CComPtr<IXMLDOMNode>& node, AttributeWriter& writer)
{
	writer.addString(Id::PageId, GetTranslatedString(node, L"PageID"));
	writer.addString(Id::HostId, GetTranslatedString(node, L"HostID"));
	writer.addString(Id::GroupId, GetTranslatedString(node, L"GroupID"));
	writer.addString(Id::SettingId, GetTranslatedString(node, L"SettingID"));
}

static void ParseSettingPaths(CComPtr<IXMLDOMNode>& parent, AttributeWriter& writer)
{
	CComPtr<IXMLDOMNode> node;
	if (parent->selectSingleNode(CComBSTR(L"SettingPaths/Path"), &node) == S_OK)
		ParseSettingIDs(node, writer);
}

static void ParseSettingIdentity(CComPtr<IXMLDOMNode>& parent, AttributeWriter& writer)
{
	CComPtr<IXMLDOMNode> node;
	if (parent->selectSingleNode(CComBSTR(L"SettingIdentity"), &node) == S_OK)
	{
		// Win11 24H2+
		ParseSettingPaths(node, writer);
		// older
		ParseSettingIDs(node, writer);
	}
}

static void ParseSettingInformation(CComPtr<IXMLDOMNode>& parent, AttributeWriter& writer)
{
	CComPtr<IXMLDOMNode> node;
	if (parent->selectSingleNode(CComBSTR(L"SettingInformation"), &node) == S_OK)
	{
		auto description = GetTranslatedString(node, L"Description");
		if (description.empty())
			description = GetTranslatedString(node, L"Name");

		writer.addString(Id::Description, description);

		auto keywords = GetTranslatedString(node, L"HighKeywords");
		keywords += GetTranslatedString(node, L"LowKeywords");
		keywords += GetTranslatedString(node, L"Keywords");

		writer.addString(Id::Keywords, keywords);
	}
}

static std::vector<uint8_t> ParseSetting(CComPtr<IXMLDOMNode>& parent)
{
	AttributeWriter writer;

	ParseFileName(parent, writer);
	ParseApplicationInformation(parent, writer);
	ParseSettingIdentity(parent, writer);
	ParseSettingInformation(parent, writer);

	return writer.buffer();
}

static std::vector<std::vector<uint8_t>> ParseModernSettings()
{
	std::vector<std::vector<uint8_t>> retval;

	CComPtr<IXMLDOMDocument> doc;
	if (SUCCEEDED(doc.CoCreateInstance(L"Msxml2.FreeThreadedDOMDocument")))
	{
		doc->put_async(VARIANT_FALSE);

		wchar_t path[MAX_PATH]{};
		wcscpy_s(path, LR"(%windir%\ImmersiveControlPanel\Settings\AllSystemSettings_{FDB289F3-FCFC-4702-8015-18926E996EC1}.xml)");
		DoEnvironmentSubst(path, _countof(path));

		if (!PathFileExists(path))
		{
			wcscpy_s(path, LR"(%windir%\ImmersiveControlPanel\Settings\AllSystemSettings_{253E530E-387D-4BC2-959D-E6F86122E5F2}.xml)");
			DoEnvironmentSubst(path, _countof(path));
		}

		VARIANT_BOOL loaded;
		if (SUCCEEDED(doc->load(CComVariant(path), &loaded)) && loaded)
		{
			CComPtr<IXMLDOMNode> root;
			if (doc->selectSingleNode(CComBSTR(L"PCSettings"), &root) == S_OK)
			{
				CComPtr<IXMLDOMNode> node;
				root->get_firstChild(&node);
				while (node)
				{
					auto buffer = ParseSetting(node);
					if (!buffer.empty())
						retval.push_back(std::move(buffer));

					CComPtr<IXMLDOMNode> next;
					if (FAILED(node->get_nextSibling(&next)))
						break;
					node = std::move(next);
				}
			}
		}
	}

	return retval;
}

static std::vector<uint8_t> SerializeModernSettings(const std::vector<std::vector<uint8_t>>& settings)
{
	AttributeWriter writer;

	FileHdr hdr{};
	writer.addBlob(Id::Header, &hdr, sizeof(hdr));

	for (const auto& setting : settings)
		writer.addBlob(Id::Blob, setting.data(), setting.size());

	return writer.buffer();
}

ModernSettings::ModernSettings(const wchar_t* fname) : m_storage(fname)
{
	if (m_storage)
	{
		bool valid = false;
		auto s = m_storage.get();
		ProcessAttributes(s.data, s.size, [&](const ItemHdr& item) {
			switch (item.id)
			{
			case Id::Header:
				if (item.size >= sizeof(FileHdr))
				{
					const auto hdr = (const FileHdr*)item.data();
					if (FileHdr() == *hdr)
						valid = true;
				}
				break;
			case Id::Blob:
				if (valid)
				{
					const Blob blob = { item.data(), item.size };
					ModernSettings::Setting s(blob);
					if (s)
						m_settings.emplace(s.fileName, blob);
				}
				break;
			}
		});
	}
}

ModernSettings::Setting::Setting(const Blob& blob)
{
	ProcessAttributes(blob.data, blob.size, [&](const ItemHdr& item) {
		switch (item.id)
		{
		case Id::FileName:
			fileName = item.asString();
			break;
		case Id::DeepLink:
			deepLink = item.asString();
			break;
		case Id::Glyph:
			glyph = item.asString();
			break;
		case Id::Icon:
			icon = item.asString();
			break;
		case Id::PageId:
			pageId = item.asString();
			break;
		case Id::HostId:
			hostId = item.asString();
			break;
		case Id::GroupId:
			groupId = item.asString();
			break;
		case Id::SettingId:
			settingId = item.asString();
			break;
		case Id::Description:
			description = item.asString();;
			break;
		case Id::Keywords:
			keywords = item.asString();
			break;
		}
	});
}

std::vector<std::wstring_view> ModernSettings::enumerate() const
{
	std::vector<std::wstring_view> retval;
	retval.reserve(m_settings.size());

	for (const auto& i : m_settings)
		retval.emplace_back(i.first);

	return retval;
}

ModernSettings::Setting ModernSettings::get(const std::wstring_view& name) const
{
	auto it = m_settings.find(name);
	if (it != m_settings.end())
		return { (*it).second };

	return {};
}

static std::mutex s_lock;
static std::shared_ptr<ModernSettings> s_settings;

std::wstring GetLocalAppData()
{
	WCHAR path[MAX_PATH]{};
	wcscpy_s(path, L"%LOCALAPPDATA%\\OpenShell");
	DoEnvironmentSubst(path, _countof(path));

	// make sure directory exists
	SHCreateDirectory(nullptr, path);

	return { path };
}

std::shared_ptr<ModernSettings> GetModernSettings()
{
	std::unique_lock l(s_lock);

	if (!s_settings)
	{
		auto path = GetLocalAppData();
		path += L"\\ModernSettings.dat";

		// try to open cached settings
		s_settings = std::make_shared<ModernSettings>(path.c_str());
		if (s_settings->size() == 0)
		{
			// file doesn't exist or wrong format
			s_settings.reset();

			// re-parse settings
			auto settings = ParseModernSettings();
			if (!settings.empty())
			{
				// sort by setting name (in reverse order)
				// this way we will have newer settings (like SomeSetting-2) before older ones
				std::stable_sort(settings.begin(), settings.end(), [](const auto& a, const auto& b) {
					return ModernSettings::Setting(a).fileName > ModernSettings::Setting(b).fileName;
					});

				// now sort by description (strings presented to the user)
				// and keep relative order of items with the same description
				std::stable_sort(settings.begin(), settings.end(), [](const auto& a, const auto& b) {
					return ModernSettings::Setting(a).description < ModernSettings::Setting(b).description;
				});

				// remove duplicates
				settings.erase(std::unique(settings.begin(), settings.end(), [](const auto& a, const auto& b) {
					return ModernSettings::Setting(a).description == ModernSettings::Setting(b).description;
				}), settings.end());

				// store to file
				{
					auto buffer = SerializeModernSettings(settings);

					File f(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS);
					if (f)
					{
						DWORD written;
						::WriteFile(f, buffer.data(), (DWORD)buffer.size(), &written, nullptr);
					}
				}

				// and try again
				s_settings = std::make_shared<ModernSettings>(path.c_str());
			}
		}
	}

	return s_settings;
}
