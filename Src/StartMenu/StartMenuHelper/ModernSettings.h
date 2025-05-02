// Modern settings helper

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Blob
{
	const void* data = nullptr;
	size_t size = 0;
};

class File
{
public:
	File(const WCHAR* fileName, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition = OPEN_EXISTING, DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL)
	{
		m_handle = ::CreateFile(fileName, desiredAccess, shareMode, nullptr, creationDisposition, flagsAndAttributes, nullptr);
	}

	~File()
	{
		if (m_handle != INVALID_HANDLE_VALUE)
			::CloseHandle(m_handle);
	}

	File(const File&) = delete;
	File& operator=(const File&) = delete;

	explicit operator bool() const
	{
		return (m_handle != INVALID_HANDLE_VALUE);
	}

	operator HANDLE() const
	{
		return m_handle;
	}

	uint64_t size() const
	{
		LARGE_INTEGER li = {};
		return ::GetFileSizeEx(m_handle, &li) ? li.QuadPart : (uint64_t)-1;
	}

private:
	HANDLE m_handle;
};

class MappedFile
{
public:
	MappedFile(const WCHAR* fileName) : m_file(fileName, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_DELETE)
	{
		if (m_file)
		{
			auto mapping = ::CreateFileMapping(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
			if (mapping)
			{
				m_view.data = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
				if (m_view.data)
					m_view.size = (size_t)m_file.size();

				::CloseHandle(mapping);
			}
		}
	}

	~MappedFile()
	{
		if (m_view.data)
			::UnmapViewOfFile(m_view.data);
	}

	MappedFile(const MappedFile&) = delete;
	MappedFile& operator=(const MappedFile&) = delete;

	explicit operator bool() const
	{
		return (m_view.data != nullptr);
	}

	Blob get() const
	{
		return m_view;
	}

private:
	File m_file;
	Blob m_view;
};

class ModernSettings
{
public:
	ModernSettings(const wchar_t* fname);

	size_t size() const
	{
		return m_settings.size();
	}

	struct Setting
	{
		std::wstring_view fileName;

		std::wstring_view deepLink;
		std::wstring_view icon;
		std::wstring_view glyph;

		std::wstring_view pageId;
		std::wstring_view hostId;
		std::wstring_view groupId;
		std::wstring_view settingId;
		std::wstring_view description;
		std::wstring_view keywords;

		Setting() = default;
		Setting(const Blob& blob);
		Setting(const std::vector<uint8_t>& blob) : Setting(Blob{ blob.data(), blob.size() }) {}

		explicit operator bool() const
		{
			return !fileName.empty();
		}
	};

	std::vector<std::wstring_view> enumerate() const;
	Setting get(const std::wstring_view& name) const;

private:
	MappedFile m_storage;
	std::map<std::wstring_view, Blob> m_settings;
};

// retrieve actual instance of ModernSettings
std::shared_ptr<ModernSettings> GetModernSettings();
