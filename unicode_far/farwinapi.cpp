/*
farwinapi.cpp

������� ������ ��������� WinAPI �������
*/
/*
Copyright � 1996 Eugene Roshal
Copyright � 2000 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "headers.hpp"
#pragma hdrstop

#include "flink.hpp"
#include "imports.hpp"
#include "pathmix.hpp"
#include "mix.hpp"
#include "ctrlobj.hpp"
#include "elevation.hpp"
#include "config.hpp"
#include "plugins.hpp"
#include "datetime.hpp"

namespace api
{

struct pseudo_handle
{
	pseudo_handle():
		NextOffset(),
		BufferSize(),
		Extended(),
		ReadDone()
	{
	}

	fs::file Object;
	block_ptr<BYTE> BufferBase;
	block_ptr<BYTE> Buffer2;
	ULONG NextOffset;
	ULONG BufferSize;
	bool Extended;
	bool ReadDone;
};

static HANDLE FindFirstFileInternal(const string& Name, FAR_FIND_DATA& FindData)
{
	HANDLE Result = INVALID_HANDLE_VALUE;
	if(!Name.empty() && !IsSlash(Name.back()))
	{
		auto Handle = new pseudo_handle;

			string strDirectory(Name);
			CutToSlash(strDirectory);
			if(Handle->Object.Open(strDirectory, FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING))
			{

				// for network paths buffer size must be <= 64k
				Handle->BufferSize = 0x10000;
				Handle->BufferBase.reset(Handle->BufferSize);
				if (Handle->BufferBase)
				{
					LPCWSTR NamePtr = PointToName(Name);
					Handle->Extended = true;

					bool QueryResult = Handle->Object.NtQueryDirectoryFile(Handle->BufferBase.get(), Handle->BufferSize, FileIdBothDirectoryInformation, false, NamePtr, true);
					if (QueryResult) // try next read immediately to avoid M#2128 bug
					{
						Handle->Buffer2.reset(Handle->BufferSize);
						bool QueryResult2 = Handle->Object.NtQueryDirectoryFile(Handle->Buffer2.get(), Handle->BufferSize, FileIdBothDirectoryInformation, false, NamePtr, false);
						if (!QueryResult2)
						{
							Handle->Buffer2.reset();
							if (GetLastError() != ERROR_INVALID_LEVEL)
								Handle->ReadDone = true;
							else
								QueryResult = false;
						}
					}

					if(!QueryResult)
					{
						Handle->Extended = false;

						// re-create handle to avoid weird bugs with some network emulators
						Handle->Object.Close();
						if(Handle->Object.Open(strDirectory, FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING))
						{
							QueryResult = Handle->Object.NtQueryDirectoryFile(Handle->BufferBase.get(), Handle->BufferSize, FileBothDirectoryInformation, false, NamePtr, true);
						}
					}
					if(QueryResult)
					{
						auto DirectoryInfo = reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(Handle->BufferBase.get());
						FindData.dwFileAttributes = DirectoryInfo->FileAttributes;
						FindData.ftCreationTime = UI64ToFileTime(DirectoryInfo->CreationTime.QuadPart);
						FindData.ftLastAccessTime = UI64ToFileTime(DirectoryInfo->LastAccessTime.QuadPart);
						FindData.ftLastWriteTime = UI64ToFileTime(DirectoryInfo->LastWriteTime.QuadPart);
						FindData.ftChangeTime = UI64ToFileTime(DirectoryInfo->ChangeTime.QuadPart);
						FindData.nFileSize = DirectoryInfo->EndOfFile.QuadPart;
						FindData.nAllocationSize = DirectoryInfo->AllocationSize.QuadPart;
						FindData.dwReserved0 = FindData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT?DirectoryInfo->EaSize:0;

						if(Handle->Extended)
						{
							FindData.FileId = DirectoryInfo->FileId.QuadPart;
							FindData.strFileName.assign(DirectoryInfo->FileName,DirectoryInfo->FileNameLength/sizeof(WCHAR));
							FindData.strAlternateFileName.assign(DirectoryInfo->ShortName,DirectoryInfo->ShortNameLength/sizeof(WCHAR));
						}
						else
						{
							FindData.FileId = 0;
							auto DirectoryInfoSimple = reinterpret_cast<PFILE_BOTH_DIR_INFORMATION>(DirectoryInfo);
							FindData.strFileName.assign(DirectoryInfoSimple->FileName,DirectoryInfoSimple->FileNameLength/sizeof(WCHAR));
							FindData.strAlternateFileName.assign(DirectoryInfoSimple->ShortName,DirectoryInfoSimple->ShortNameLength/sizeof(WCHAR));
						}

						// Bug in SharePoint: FileName is zero-terminated and FileNameLength INCLUDES this zero.
						if(!FindData.strFileName.empty() && !FindData.strFileName.back())
						{
							FindData.strFileName.pop_back();
						}
						if(!FindData.strAlternateFileName.empty() && !FindData.strAlternateFileName.back())
						{
							FindData.strAlternateFileName.pop_back();
						}

						Handle->NextOffset = DirectoryInfo->NextEntryOffset;
						Result = static_cast<HANDLE>(Handle);
					}
					else
					{
						Handle->BufferBase.reset();
					}
				}
			}
			else
			{
				// fix error code if we looking for FILE(S) in non-existent directory, not directory itself
				if(GetLastError() == ERROR_FILE_NOT_FOUND && *PointToName(Name))
				{
					SetLastError(ERROR_PATH_NOT_FOUND);
				}
			}
			if(Result == INVALID_HANDLE_VALUE)
			{
				delete Handle;
			}
	}
	return Result;
}

static bool FindNextFileInternal(HANDLE Find, FAR_FIND_DATA& FindData)
{
	bool Result = false;
	auto Handle = static_cast<pseudo_handle*>(Find);
	bool Status = true, set_errcode = true;
	auto DirectoryInfo = reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(Handle->BufferBase.get());
	if(Handle->NextOffset)
	{
		DirectoryInfo = reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(reinterpret_cast<LPBYTE>(DirectoryInfo)+Handle->NextOffset);
	}
	else
	{
		if (Handle->ReadDone)
		{
			Status = false;
		}
		else
		{
			if (Handle->Buffer2)
			{
				Handle->BufferBase.reset();
				Handle->BufferBase.swap(Handle->Buffer2);
				DirectoryInfo = reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(Handle->BufferBase.get());
			}
			else
			{
				Status = Handle->Object.NtQueryDirectoryFile(Handle->BufferBase.get(), Handle->BufferSize, Handle->Extended ? FileIdBothDirectoryInformation : FileBothDirectoryInformation, false, nullptr, false);
				set_errcode = false;
			}
		}
	}

	if(Status)
	{
		FindData.dwFileAttributes = DirectoryInfo->FileAttributes;
		FindData.ftCreationTime = UI64ToFileTime(DirectoryInfo->CreationTime.QuadPart);
		FindData.ftLastAccessTime = UI64ToFileTime(DirectoryInfo->LastAccessTime.QuadPart);
		FindData.ftLastWriteTime = UI64ToFileTime(DirectoryInfo->LastWriteTime.QuadPart);
		FindData.ftChangeTime = UI64ToFileTime(DirectoryInfo->ChangeTime.QuadPart);
		FindData.nFileSize = DirectoryInfo->EndOfFile.QuadPart;
		FindData.nAllocationSize = DirectoryInfo->AllocationSize.QuadPart;
		FindData.dwReserved0 = FindData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT?DirectoryInfo->EaSize:0;

		if(Handle->Extended)
		{
			FindData.FileId = DirectoryInfo->FileId.QuadPart;
			FindData.strFileName.assign(DirectoryInfo->FileName,DirectoryInfo->FileNameLength/sizeof(WCHAR));
			FindData.strAlternateFileName.assign(DirectoryInfo->ShortName,DirectoryInfo->ShortNameLength/sizeof(WCHAR));
		}
		else
		{
			FindData.FileId = 0;
			auto DirectoryInfoSimple = reinterpret_cast<PFILE_BOTH_DIR_INFORMATION>(DirectoryInfo);
			FindData.strFileName.assign(DirectoryInfoSimple->FileName,DirectoryInfoSimple->FileNameLength/sizeof(WCHAR));
			FindData.strAlternateFileName.assign(DirectoryInfoSimple->ShortName,DirectoryInfoSimple->ShortNameLength/sizeof(WCHAR));
		}

		// Bug in SharePoint: FileName is zero-terminated and FileNameLength INCLUDES this zero.
		if(!FindData.strFileName.empty() && !FindData.strFileName.back())
		{
			FindData.strFileName.pop_back();
		}
		if(!FindData.strAlternateFileName.empty() && !FindData.strAlternateFileName.back())
		{
			FindData.strAlternateFileName.pop_back();
		}

		Handle->NextOffset = DirectoryInfo->NextEntryOffset?Handle->NextOffset+DirectoryInfo->NextEntryOffset:0;
		Result = true;
	}

	if (set_errcode)
		SetLastError(Result ? ERROR_SUCCESS : ERROR_NO_MORE_FILES);

	return Result;
}

static bool FindCloseInternal(HANDLE Find)
{
	delete static_cast<pseudo_handle*>(Find);
	return true;
}

//-------------------------------------------------------------------------
namespace fs
{
	file_status::file_status():
		m_Data(INVALID_FILE_ATTRIBUTES)
	{
	}

	file_status::file_status(const string& Object) :
		m_Data(api::GetFileAttributes(Object))
	{

	}

	file_status::file_status(const wchar_t* Object):
		m_Data(api::GetFileAttributes(Object))
	{

	}

	bool file_status::check(DWORD Data)
	{
		return m_Data != INVALID_FILE_ATTRIBUTES && m_Data & Data;
	}

	bool exists(file_status Status)
	{
		return Status.check(~0);
	}

	bool is_file(file_status Status)
	{
		return exists(Status) && !is_directory(Status);
	}

	bool is_directory(file_status Status)
	{
		return Status.check(FILE_ATTRIBUTE_DIRECTORY);
	}

	bool is_not_empty_directory(const string& Object)
	{
		api::fs::enum_file Find(Object + L"\\*");
		return Find.begin() != Find.end();
	}

enum_file::enum_file(const string& Object, bool ScanSymLink):
	m_Object(NTPath(Object)),
	m_Handle(INVALID_HANDLE_VALUE),
	m_ScanSymLink(ScanSymLink)
{
	bool Root = false;
	const auto Type = ParsePath(m_Object, nullptr, &Root);
	if(Root && (Type == PATH_DRIVELETTER || Type == PATH_DRIVELETTERUNC || Type == PATH_VOLUMEGUID))
	{
		AddEndSlash(m_Object);
	}
	else
	{
		DeleteEndSlash(m_Object);
	}
}

enum_file::~enum_file()
{
	if(m_Handle != INVALID_HANDLE_VALUE)
	{
		FindCloseInternal(m_Handle);
	}
}

bool enum_file::get(size_t index, FAR_FIND_DATA& FindData)
{
	bool Result = false;
	if (!index)
	{
		if(m_Handle != INVALID_HANDLE_VALUE)
		{
			FindCloseInternal(m_Handle);
		}

		// temporary disable elevation to try "real" name first
		{
			SCOPED_ACTION(elevation::suppress);
			m_Handle = FindFirstFileInternal(m_Object, FindData);
		}

		if (m_Handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED)
		{
			if(m_ScanSymLink)
			{
				string strReal(m_Object);
				// only links in path should be processed, not the object name itself
				CutToSlash(strReal);
				ConvertNameToReal(strReal, strReal);
				AddEndSlash(strReal);
				strReal+=PointToName(m_Object);
				strReal = NTPath(strReal);
				m_Handle = FindFirstFileInternal(strReal, FindData);
			}

			if (m_Handle == INVALID_HANDLE_VALUE && ElevationRequired(ELEVATION_READ_REQUEST))
			{
				m_Handle = FindFirstFileInternal(m_Object, FindData);
			}
		}
		Result = m_Handle != INVALID_HANDLE_VALUE;
	}
	else
	{
		if (m_Handle != INVALID_HANDLE_VALUE)
		{
			Result = FindNextFileInternal(m_Handle, FindData);
		}
	}

	// skip ".." & "."
	if(Result && FindData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY && FindData.strFileName[0] == L'.' &&
		((FindData.strFileName.size() == 2 && FindData.strFileName[1] == L'.') || FindData.strFileName.size() == 1) &&
		// ������ ������ - � ����������� ����� �� ������ SFN, � ������� ��. (UPD: ��� ������, �� ����� ��)
		(FindData.strAlternateFileName.empty() || FindData.strAlternateFileName == FindData.strFileName))
	{
		// index not important here, anything but 0 is fine
		Result = get(1, FindData);
	}
	return Result;
}



//-------------------------------------------------------------------------
file::file():
	Handle(INVALID_HANDLE_VALUE),
	Pointer(0),
	NeedSyncPointer(false),
	share_mode(0)
{
}

file::~file()
{
	Close();
}

bool file::Open(const string& Object, DWORD DesiredAccess, DWORD ShareMode, LPSECURITY_ATTRIBUTES SecurityAttributes, DWORD CreationDistribution, DWORD FlagsAndAttributes, file* TemplateFile, bool ForceElevation)
{
	assert(Handle == INVALID_HANDLE_VALUE);
	HANDLE TemplateFileHandle = TemplateFile? TemplateFile->Handle : nullptr;
	Handle = CreateFile(Object, DesiredAccess, ShareMode, SecurityAttributes, CreationDistribution, FlagsAndAttributes, TemplateFileHandle, ForceElevation);
	bool ok =  Handle != INVALID_HANDLE_VALUE;
	if (ok)
	{
		name = Object;
		share_mode = ShareMode;
	}
	else
	{
		name.clear();
		share_mode = 0;
	}
	return ok;
}

inline void file::SyncPointer()
{
	if(NeedSyncPointer)
	{
		if (SetFilePointerEx(Handle, *reinterpret_cast<PLARGE_INTEGER>(&Pointer), reinterpret_cast<PLARGE_INTEGER>(&Pointer), FILE_BEGIN))
			NeedSyncPointer = false;
	}
}


bool file::Read(LPVOID Buffer, size_t NumberOfBytesToRead, size_t& NumberOfBytesRead, LPOVERLAPPED Overlapped)
{
	assert(NumberOfBytesToRead <= std::numeric_limits<DWORD>::max());

	SyncPointer();
	DWORD BytesRead = 0;
	bool Result = ReadFile(Handle, Buffer, static_cast<DWORD>(NumberOfBytesToRead), &BytesRead, Overlapped) != FALSE;
	NumberOfBytesRead = BytesRead;
	if(Result)
	{
		Pointer += NumberOfBytesRead;
	}
	return Result;
}

bool file::Write(LPCVOID Buffer, size_t NumberOfBytesToWrite, size_t& NumberOfBytesWritten, LPOVERLAPPED Overlapped)
{
	assert(NumberOfBytesToWrite <= std::numeric_limits<DWORD>::max());

	SyncPointer();
	DWORD BytesWritten = 0;
	bool Result = WriteFile(Handle, Buffer, static_cast<DWORD>(NumberOfBytesToWrite), &BytesWritten, Overlapped) != FALSE;
	NumberOfBytesWritten = BytesWritten;
	if(Result)
	{
		Pointer += NumberOfBytesWritten;
	}
	return Result;
}

bool file::SetPointer(INT64 DistanceToMove, PINT64 NewFilePointer, DWORD MoveMethod)
{
	INT64 OldPointer = Pointer;
	switch (MoveMethod)
	{
	case FILE_BEGIN:
		Pointer = DistanceToMove;
		break;
	case FILE_CURRENT:
		Pointer+=DistanceToMove;
		break;
	case FILE_END:
		{
			UINT64 Size=0;
			GetSize(Size);
			Pointer = Size+DistanceToMove;
		}
		break;
	}
	if(OldPointer != Pointer)
	{
		NeedSyncPointer = true;
	}
	if(NewFilePointer)
	{
		*NewFilePointer = Pointer;
	}
	return true;
}

bool file::SetEnd()
{
	SyncPointer();
	bool ok = SetEndOfFile(Handle) != FALSE;
	if (!ok && !name.empty() && GetLastError() == ERROR_INVALID_PARAMETER) // OSX buggy SMB workaround
	{
		INT64 fsize = GetPointer();
		Close();
		if (Open(name, GENERIC_WRITE, share_mode, nullptr, OPEN_EXISTING, 0))
		{
			SetPointer(fsize, nullptr, FILE_BEGIN);
			SyncPointer();
			ok = SetEndOfFile(Handle) != FALSE;
		}
	}
	return ok;
}

bool file::GetTime(LPFILETIME CreationTime, LPFILETIME LastAccessTime, LPFILETIME LastWriteTime, LPFILETIME ChangeTime)
{
	return GetFileTimeEx(Handle, CreationTime, LastAccessTime, LastWriteTime, ChangeTime);
}

bool file::SetTime(const FILETIME* CreationTime, const FILETIME* LastAccessTime, const FILETIME* LastWriteTime, const FILETIME* ChangeTime)
{
	return SetFileTimeEx(Handle, CreationTime, LastAccessTime, LastWriteTime, ChangeTime);
}

bool file::GetSize(UINT64& Size)
{
	return GetFileSizeEx(Handle, Size);
}

bool file::FlushBuffers()
{
	return FlushFileBuffers(Handle) != FALSE;
}

bool file::GetInformation(BY_HANDLE_FILE_INFORMATION& info)
{
	return GetFileInformationByHandle(Handle, &info) != FALSE;
}

bool file::IoControl(DWORD IoControlCode, LPVOID InBuffer, DWORD InBufferSize, LPVOID OutBuffer, DWORD OutBufferSize, LPDWORD BytesReturned, LPOVERLAPPED Overlapped)
{
	return ::DeviceIoControl(Handle, IoControlCode, InBuffer, InBufferSize, OutBuffer, OutBufferSize, BytesReturned, Overlapped) != FALSE;
}

bool file::GetStorageDependencyInformation(GET_STORAGE_DEPENDENCY_FLAG Flags, ULONG StorageDependencyInfoSize, PSTORAGE_DEPENDENCY_INFO StorageDependencyInfo, PULONG SizeUsed)
{
	DWORD Result = Imports().GetStorageDependencyInformation(Handle, Flags, StorageDependencyInfoSize, StorageDependencyInfo, SizeUsed);
	SetLastError(Result);
	return Result == ERROR_SUCCESS;
}

bool file::NtQueryDirectoryFile(PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, bool ReturnSingleEntry, LPCWSTR FileName, bool RestartScan, NTSTATUS* Status)
{
	IO_STATUS_BLOCK IoStatusBlock;
	PUNICODE_STRING pNameString = nullptr;
	UNICODE_STRING NameString;
	if(FileName && *FileName)
	{
		NameString.Buffer = const_cast<LPWSTR>(FileName);
		NameString.Length = static_cast<USHORT>(StrLength(FileName)*sizeof(WCHAR));
		NameString.MaximumLength = NameString.Length;
		pNameString = &NameString;
	}
	auto di = reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(FileInformation);
	di->NextEntryOffset = 0xffffffffUL;

	NTSTATUS Result = Imports().NtQueryDirectoryFile(Handle, nullptr, nullptr, nullptr, &IoStatusBlock, FileInformation, Length, FileInformationClass, ReturnSingleEntry, pNameString, RestartScan);
	SetLastError(Imports().RtlNtStatusToDosError(Result));
	if(Status)
	{
		*Status = Result;
	}

	return (Result == STATUS_SUCCESS) && (di->NextEntryOffset != 0xffffffffUL);
}

bool file::NtQueryInformationFile(PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, NTSTATUS* Status)
{
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Result = Imports().NtQueryInformationFile(Handle, &IoStatusBlock, FileInformation, Length, FileInformationClass);
	SetLastError(Imports().RtlNtStatusToDosError(Result));
	if(Status)
	{
		*Status = Result;
	}
	return Result == STATUS_SUCCESS;
}

bool file::Close()
{
	bool Result=true;
	if(Handle!=INVALID_HANDLE_VALUE)
	{
		Result = ::CloseHandle(Handle) != FALSE;
		Handle = INVALID_HANDLE_VALUE;
	}
	Pointer = 0;
	NeedSyncPointer = false;
	return Result;
}

bool file::Eof()
{
	INT64 Ptr = GetPointer();
	UINT64 Size=0;
	GetSize(Size);
	return static_cast<UINT64>(Ptr) >= Size;
}
//-------------------------------------------------------------------------
file_walker::file_walker():
	FileSize(0),
	AllocSize(0),
	ProcessedSize(0),
	CurrentChunk(ChunkList.begin()),
	ChunkSize(0),
	Sparse(false)
{
}

bool file_walker::InitWalk(size_t BlockSize)
{
	bool Result = false;
	ChunkSize = static_cast<DWORD>(BlockSize);
	if(GetSize(FileSize) && FileSize)
	{
		BY_HANDLE_FILE_INFORMATION bhfi;
		Sparse = GetInformation(bhfi) && bhfi.dwFileAttributes&FILE_ATTRIBUTE_SPARSE_FILE;

		if(Sparse)
		{
			FILE_ALLOCATED_RANGE_BUFFER QueryRange = {};
			QueryRange.Length.QuadPart = FileSize;
			static FILE_ALLOCATED_RANGE_BUFFER Ranges[1024];
			DWORD BytesReturned;
			for(;;)
			{
				bool QueryResult = IoControl(FSCTL_QUERY_ALLOCATED_RANGES, &QueryRange, sizeof(QueryRange), Ranges, sizeof(Ranges), &BytesReturned);
				if((QueryResult || GetLastError() == ERROR_MORE_DATA) && BytesReturned)
				{
					for(size_t i = 0; i < BytesReturned/sizeof(FILE_ALLOCATED_RANGE_BUFFER); ++i)
					{
						AllocSize += Ranges[i].Length.QuadPart;
						UINT64 RangeEndOffset = Ranges[i].FileOffset.QuadPart + Ranges[i].Length.QuadPart;
						for(UINT64 j = Ranges[i].FileOffset.QuadPart; j < RangeEndOffset; j+=ChunkSize)
						{
							ChunkList.emplace_back(Chunk(j, std::min(static_cast<DWORD>(RangeEndOffset - j), ChunkSize)));
						}
					}
					QueryRange.FileOffset.QuadPart = ChunkList.back().Offset+ChunkList.back().Size;
					QueryRange.Length.QuadPart = FileSize - QueryRange.FileOffset.QuadPart;
				}
				else
				{
					break;
				}
			}
			Result = !ChunkList.empty();
		}
		else
		{
			AllocSize = FileSize;
			ChunkList.emplace_back(Chunk(0, static_cast<DWORD>(std::min(static_cast<UINT64>(BlockSize), FileSize))));
			Result = true;
		}
		CurrentChunk = ChunkList.begin();
	}
	return Result;
}


bool file_walker::Step()
{
	bool Result = false;
	if(Sparse)
	{
		++CurrentChunk;
		if(CurrentChunk != ChunkList.end())
		{
			SetPointer(CurrentChunk->Offset, nullptr, FILE_BEGIN);
			ProcessedSize += CurrentChunk->Size;
			Result = true;
		}
	}
	else
	{
		UINT64 NewOffset = (!CurrentChunk->Size)? 0 : CurrentChunk->Offset + ChunkSize;
		if(NewOffset < FileSize)
		{
			CurrentChunk->Offset = NewOffset;
			UINT64 rest = FileSize - NewOffset;
			CurrentChunk->Size = (rest>=ChunkSize)?ChunkSize:rest;
			ProcessedSize += CurrentChunk->Size;
			Result = true;
		}
	}
	return Result;
}

UINT64 file_walker::GetChunkOffset() const
{
	return CurrentChunk->Offset;
}

DWORD file_walker::GetChunkSize() const
{
	return CurrentChunk->Size;
}

int file_walker::GetPercent() const
{
	return AllocSize? (ProcessedSize) * 100 / AllocSize : 0;
}

} // fs
//-------------------------------------------------------------------------

NTSTATUS GetLastNtStatus()
{
	return Imports().RtlGetLastNtStatus? Imports().RtlGetLastNtStatus() : STATUS_SUCCESS;
}

BOOL DeleteFile(const string& FileName)
{
	NTPath strNtName(FileName);
	BOOL Result = ::DeleteFile(strNtName.data());
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
	{
		Result = Global->Elevation->fDeleteFile(strNtName);
	}

	if (!Result && !api::fs::exists(strNtName))
	{
		// Someone deleted it already,
		// but job is done, no need to report error.
		Result = true;
	}

	return Result;
}

BOOL RemoveDirectory(const string& DirName)
{
	NTPath strNtName(DirName);
	BOOL Result = ::RemoveDirectory(strNtName.data());
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
	{
		Result = Global->Elevation->fRemoveDirectory(strNtName);
	}

	if (!Result && !api::fs::exists(strNtName))
	{
		// Someone deleted it already,
		// but job is done, no need to report error.
		Result = true;
	}

	return Result;
}

HANDLE CreateFile(const string& Object, DWORD DesiredAccess, DWORD ShareMode, LPSECURITY_ATTRIBUTES SecurityAttributes, DWORD CreationDistribution, DWORD FlagsAndAttributes, HANDLE TemplateFile, bool ForceElevation)
{
	NTPath strObject(Object);
	FlagsAndAttributes|=FILE_FLAG_BACKUP_SEMANTICS|(CreationDistribution==OPEN_EXISTING?FILE_FLAG_POSIX_SEMANTICS:0);

	HANDLE Handle=::CreateFile(strObject.data(), DesiredAccess, ShareMode, SecurityAttributes, CreationDistribution, FlagsAndAttributes, TemplateFile);
	if(Handle == INVALID_HANDLE_VALUE)
	{
		DWORD Error=::GetLastError();
		if(Error==ERROR_FILE_NOT_FOUND||Error==ERROR_PATH_NOT_FOUND)
		{
			FlagsAndAttributes&=~FILE_FLAG_POSIX_SEMANTICS;
			Handle = ::CreateFile(strObject.data(), DesiredAccess, ShareMode, SecurityAttributes, CreationDistribution, FlagsAndAttributes, TemplateFile);
		}
		if (STATUS_STOPPED_ON_SYMLINK == GetLastNtStatus() && ERROR_STOPPED_ON_SYMLINK != GetLastError())
			SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	}

	if((Handle == INVALID_HANDLE_VALUE && ElevationRequired(DesiredAccess&(GENERIC_ALL|GENERIC_WRITE|WRITE_OWNER|WRITE_DAC|DELETE|FILE_WRITE_DATA|FILE_ADD_FILE|FILE_APPEND_DATA|FILE_ADD_SUBDIRECTORY|FILE_CREATE_PIPE_INSTANCE|FILE_WRITE_EA|FILE_DELETE_CHILD|FILE_WRITE_ATTRIBUTES)?ELEVATION_MODIFY_REQUEST:ELEVATION_READ_REQUEST)) || ForceElevation)
	{
		if(ForceElevation && Handle!=INVALID_HANDLE_VALUE)
		{
			::CloseHandle(Handle);
		}
		Handle = Global->Elevation->fCreateFile(strObject, DesiredAccess, ShareMode, SecurityAttributes, CreationDistribution, FlagsAndAttributes, TemplateFile);
	}
	return Handle;
}

BOOL CopyFileEx(
    const string& ExistingFileName,
    const string& NewFileName,
    LPPROGRESS_ROUTINE lpProgressRoutine,
    LPVOID lpData,
    LPBOOL pbCancel,
    DWORD dwCopyFlags
)
{
	NTPath strFrom(ExistingFileName), strTo(NewFileName);
	if(IsSlash(strTo.back()))
	{
		strTo += PointToName(strFrom);
	}
	BOOL Result = ::CopyFileEx(strFrom.data(), strTo.data(), lpProgressRoutine, lpData, pbCancel, dwCopyFlags);
	if(!Result)
	{
		if (STATUS_STOPPED_ON_SYMLINK == GetLastNtStatus() && ERROR_STOPPED_ON_SYMLINK != GetLastError())
			::SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

		else if (STATUS_FILE_IS_A_DIRECTORY == GetLastNtStatus())
			::SetLastError(ERROR_FILE_EXISTS);

		else if (ElevationRequired(ELEVATION_MODIFY_REQUEST)) //BUGBUG, really unknown
			Result = Global->Elevation->fCopyFileEx(strFrom, strTo, lpProgressRoutine, lpData, pbCancel, dwCopyFlags);
	}
	return Result;
}

BOOL MoveFile(
    const string& ExistingFileName, // address of name of the existing file
    const string& NewFileName   // address of new name for the file
)
{
	NTPath strFrom(ExistingFileName), strTo(NewFileName);
	if(IsSlash(strTo.back()))
	{
		strTo += PointToName(strFrom);
	}
	BOOL Result = ::MoveFile(strFrom.data(), strTo.data());

	if(!Result)
	{
		if (STATUS_STOPPED_ON_SYMLINK == GetLastNtStatus() && ERROR_STOPPED_ON_SYMLINK != GetLastError())
			::SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

		else if (ElevationRequired(ELEVATION_MODIFY_REQUEST)) //BUGBUG, really unknown
			Result = Global->Elevation->fMoveFileEx(strFrom, strTo, 0);
	}
	return Result;
}

BOOL MoveFileEx(
    const string& ExistingFileName, // address of name of the existing file
    const string& NewFileName,   // address of new name for the file
    DWORD dwFlags   // flag to determine how to move file
)
{
	NTPath strFrom(ExistingFileName), strTo(NewFileName);
	if(IsSlash(strTo.back()))
	{
		strTo += PointToName(strFrom);
	}
	BOOL Result = ::MoveFileEx(strFrom.data(), strTo.data(), dwFlags);
	if(!Result)
	{
		if (STATUS_STOPPED_ON_SYMLINK == GetLastNtStatus() && ERROR_STOPPED_ON_SYMLINK != GetLastError())
			::SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

		else if (ElevationRequired(ELEVATION_MODIFY_REQUEST)) //BUGBUG, really unknown
		{
			// exclude fake elevation request for: move file over existing directory with same name
			DWORD f = GetFileAttributes(strFrom);
			DWORD t = GetFileAttributes(strTo);

			if (f!=INVALID_FILE_ATTRIBUTES && t!=INVALID_FILE_ATTRIBUTES && 0==(f & FILE_ATTRIBUTE_DIRECTORY) && 0!=(t & FILE_ATTRIBUTE_DIRECTORY))
				::SetLastError(ERROR_FILE_EXISTS); // existing directory name == moved file name
			else
				Result = Global->Elevation->fMoveFileEx(strFrom, strTo, dwFlags);
		}
	}
	return Result;
}

string& strCurrentDirectory()
{
	static string strCurrentDirectory;
	return strCurrentDirectory;
}

void InitCurrentDirectory()
{
	//get real curdir:
	WCHAR Buffer[MAX_PATH];
	DWORD Size=::GetCurrentDirectory(ARRAYSIZE(Buffer),Buffer);
	if(Size)
	{
		string strInitCurDir;
		if(Size < ARRAYSIZE(Buffer))
		{
			strInitCurDir.assign(Buffer, Size);
		}
		else
		{
			std::vector<wchar_t> vBuffer(Size);
			::GetCurrentDirectory(Size, vBuffer.data());
			strInitCurDir.assign(vBuffer.data(), Size);
		}
		//set virtual curdir:
		SetCurrentDirectory(strInitCurDir);
	}
}

DWORD GetCurrentDirectory(string &strCurDir)
{
	//never give outside world a direct pointer to our internal string
	//who knows what they gonna do
	strCurDir.assign(strCurrentDirectory().data(),strCurrentDirectory().size());
	return static_cast<DWORD>(strCurDir.size());
}

BOOL SetCurrentDirectory(const string& PathName, bool Validate)
{
	// correct path to our standard
	string strDir=PathName;
	ReplaceSlashToBSlash(strDir);
	bool Root = false;
	const auto Type = ParsePath(strDir, nullptr, &Root);
	if(Root && (Type == PATH_DRIVELETTER || Type == PATH_DRIVELETTERUNC || Type == PATH_VOLUMEGUID))
	{
		AddEndSlash(strDir);
	}
	else
	{
		DeleteEndSlash(strDir);
	}

	if (strDir == strCurrentDirectory())
		return TRUE;

	if (Validate)
	{
		string TestDir=PathName;
		AddEndSlash(TestDir);
		TestDir += L"*";
		FAR_FIND_DATA fd;
		if (!GetFindDataEx(TestDir, fd))
		{
			DWORD LastError = ::GetLastError();
			if(!(LastError == ERROR_FILE_NOT_FOUND || LastError == ERROR_NO_MORE_FILES))
				return FALSE;
		}
	}

	strCurrentDirectory()=strDir;

#ifndef NO_WRAPPER
	// try to synchronize far cur dir with process cur dir
	if(Global->CtrlObject && Global->CtrlObject->Plugins->OemPluginsPresent())
	{
		::SetCurrentDirectory(strCurrentDirectory().data());
	}
#endif // NO_WRAPPER
	return TRUE;
}

DWORD GetTempPath(string &strBuffer)
{
	WCHAR Buffer[MAX_PATH];
	DWORD Size = ::GetTempPath(ARRAYSIZE(Buffer), Buffer);
	if(Size)
	{
		if(Size < ARRAYSIZE(Buffer))
		{
			strBuffer.assign(Buffer, Size);
		}
		else
		{
			std::vector<wchar_t> vBuffer(Size);
			Size = ::GetTempPath(Size, vBuffer.data());
			strBuffer.assign(vBuffer.data(), Size);
		}
	}
	return Size;
};


DWORD GetModuleFileName(HMODULE hModule, string &strFileName)
{
	return GetModuleFileNameEx(nullptr, hModule, strFileName);
}

DWORD GetModuleFileNameEx(HANDLE hProcess, HMODULE hModule, string &strFileName)
{
	DWORD Size = 0;
	DWORD BufferSize = MAX_PATH;
	wchar_t_ptr FileName;

	do
	{
		BufferSize *= 2;
		FileName.reset(BufferSize);
		if (hProcess)
		{
			if (Imports().QueryFullProcessImageNameW && !hModule)
			{
				DWORD sz = BufferSize;
				Size = 0;
				if (Imports().QueryFullProcessImageNameW(hProcess, 0, FileName.get(), &sz))
				{
					Size = sz;
				}
			}
			else
			{
				Size = ::GetModuleFileNameEx(hProcess, hModule, FileName.get(), BufferSize);
			}
		}
		else
		{
			Size = ::GetModuleFileName(hModule, FileName.get(), BufferSize);
		}
	}
	while ((Size >= BufferSize) || (!Size && GetLastError() == ERROR_INSUFFICIENT_BUFFER));

	if (Size)
		strFileName.assign(FileName.get(), Size);

	return Size;
}

DWORD WNetGetConnection(const string& LocalName, string &RemoteName)
{
	DWORD dwRemoteNameSize = 0;
	DWORD dwResult = ::WNetGetConnection(LocalName.data(), nullptr, &dwRemoteNameSize);

	if (dwResult == ERROR_SUCCESS || dwResult == ERROR_MORE_DATA)
	{
		std::vector<wchar_t> Buffer(dwRemoteNameSize);
		dwResult = ::WNetGetConnection(LocalName.data(), Buffer.data(), &dwRemoteNameSize);
		RemoteName.assign(Buffer.data(), dwRemoteNameSize);
	}

	return dwResult;
}

BOOL GetVolumeInformation(
    const string& RootPathName,
    string *pVolumeName,
    LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength,
    LPDWORD lpFileSystemFlags,
    string *pFileSystemName
)
{
	wchar_t_ptr VolumeNameBuffer, FileSystemNameBuffer;
	if (pVolumeName)
	{
		VolumeNameBuffer.reset(MAX_PATH + 1);
		VolumeNameBuffer[0] = L'\0';
	}
	if (pFileSystemName)
	{
		FileSystemNameBuffer.reset(MAX_PATH + 1);
		FileSystemNameBuffer[0] = L'\0';
	}
	BOOL bResult = ::GetVolumeInformation(RootPathName.data(), VolumeNameBuffer.get(), static_cast<DWORD>(VolumeNameBuffer.size()), lpVolumeSerialNumber,
		lpMaximumComponentLength, lpFileSystemFlags, FileSystemNameBuffer.get(), static_cast<DWORD>(FileSystemNameBuffer.size()));

	if (pVolumeName)
		*pVolumeName = VolumeNameBuffer.get();

	if (pFileSystemName)
		*pFileSystemName = FileSystemNameBuffer.get();

	return bResult;
}

bool GetFindDataEx(const string& FileName, FAR_FIND_DATA& FindData,bool ScanSymLink)
{
	fs::enum_file Find(FileName, ScanSymLink);
	auto ItemIterator = Find.begin();
	if(ItemIterator != Find.end())
	{
		FindData = std::move(*ItemIterator);
		return true;
	}
	else
	{
		size_t DirOffset = 0;
		ParsePath(FileName, &DirOffset);
		if (FileName.find_first_of(L"*?", DirOffset) == string::npos)
		{
			DWORD dwAttr=GetFileAttributes(FileName);

			if (dwAttr!=INVALID_FILE_ATTRIBUTES)
			{
				// ���, ������ ���� ���� ����. �������� ��������� �������.
				FindData.Clear();
				FindData.dwFileAttributes=dwAttr;
				fs::file file;
				if(file.Open(FileName, FILE_READ_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING))
				{
					file.GetTime(&FindData.ftCreationTime,&FindData.ftLastAccessTime,&FindData.ftLastWriteTime,&FindData.ftChangeTime);
					file.GetSize(FindData.nFileSize);
					file.Close();
				}

				if (FindData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
				{
					string strTmp;
					GetReparsePointInfo(FileName,strTmp,&FindData.dwReserved0); //MSDN
				}
				else
				{
					FindData.dwReserved0=0;
				}

				FindData.strFileName=PointToName(FileName);
				ConvertNameToShort(FileName,FindData.strAlternateFileName);
				return true;
			}
		}
	}
	FindData.Clear();
	FindData.dwFileAttributes=INVALID_FILE_ATTRIBUTES; //BUGBUG

	return false;
}

bool GetFileSizeEx(HANDLE hFile, UINT64 &Size)
{
	bool Result=false;

	if (::GetFileSizeEx(hFile,reinterpret_cast<PLARGE_INTEGER>(&Size)))
	{
		Result=true;
	}
	else
	{
		GET_LENGTH_INFORMATION gli;
		DWORD BytesReturned;

		if (::DeviceIoControl(hFile,IOCTL_DISK_GET_LENGTH_INFO,nullptr,0,&gli,sizeof(gli),&BytesReturned,nullptr))
		{
			Size=gli.Length.QuadPart;
			Result=true;
		}
	}

	return Result;
}

BOOL IsDiskInDrive(const string& Root)
{
	string strVolName;
	string strDrive;
	DWORD  MaxComSize;
	DWORD  Flags;
	string strFS;
	strDrive = Root;
	AddEndSlash(strDrive);
	BOOL Res = GetVolumeInformation(strDrive, &strVolName, nullptr, &MaxComSize, &Flags, &strFS);
	return Res;
}

int GetFileTypeByName(const string& Name)
{
	HANDLE hFile=CreateFile(Name,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0);

	if (hFile==INVALID_HANDLE_VALUE)
		return FILE_TYPE_UNKNOWN;

	int Type=::GetFileType(hFile);
	CloseHandle(hFile);
	return Type;
}

bool GetDiskSize(const string& Path,unsigned __int64 *TotalSize, unsigned __int64 *TotalFree, unsigned __int64 *UserFree)
{
	bool Result = false;
	NTPath strPath(Path);
	AddEndSlash(strPath);

	ULARGE_INTEGER FreeBytesAvailableToCaller, TotalNumberOfBytes, TotalNumberOfFreeBytes;

	Result = GetDiskFreeSpaceEx(strPath.data(), &FreeBytesAvailableToCaller, &TotalNumberOfBytes, &TotalNumberOfFreeBytes) != FALSE;
	if(!Result && ElevationRequired(ELEVATION_READ_REQUEST))
	{
		Result = Global->Elevation->fGetDiskFreeSpaceEx(strPath.data(), &FreeBytesAvailableToCaller, &TotalNumberOfBytes, &TotalNumberOfFreeBytes);
	}

	if (Result)
	{
		if (TotalSize)
			*TotalSize = TotalNumberOfBytes.QuadPart;

		if (TotalFree)
			*TotalFree = TotalNumberOfFreeBytes.QuadPart;

		if (UserFree)
			*UserFree = FreeBytesAvailableToCaller.QuadPart;
	}
	return Result;
}

HANDLE FindFirstFileName(const string& FileName, DWORD dwFlags, string& LinkName)
{
	HANDLE hRet=INVALID_HANDLE_VALUE;
	DWORD StringLength=0;
	NTPath NtFileName(FileName);
	if (Imports().FindFirstFileNameW(NtFileName.data(), 0, &StringLength, nullptr)==INVALID_HANDLE_VALUE && GetLastError()==ERROR_MORE_DATA)
	{
		wchar_t_ptr Buffer(StringLength);
		hRet=Imports().FindFirstFileNameW(NtFileName.data(), 0, &StringLength, Buffer.get());
		LinkName = Buffer.get();
	}
	return hRet;
}

BOOL FindNextFileName(HANDLE hFindStream, string& LinkName)
{
	BOOL Ret=FALSE;
	DWORD StringLength=0;
	if (!Imports().FindNextFileNameW(hFindStream, &StringLength, nullptr) && GetLastError()==ERROR_MORE_DATA)
	{
		wchar_t_ptr Buffer(StringLength);
		Ret = Imports().FindNextFileNameW(hFindStream, &StringLength, Buffer.get());
		LinkName = Buffer.get();
	}
	return Ret;
}

BOOL CreateDirectory(const string& PathName,LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	return CreateDirectoryEx(L"", PathName, lpSecurityAttributes);
}

BOOL CreateDirectoryEx(const string& TemplateDirectory, const string& NewDirectory, LPSECURITY_ATTRIBUTES SecurityAttributes)
{
	NTPath NtTemplateDirectory(TemplateDirectory);
	NTPath NtNewDirectory(NewDirectory);
	BOOL Result = FALSE;
	for(size_t i = 0; i < 2 && !Result; ++i)
	{
		Result = NtTemplateDirectory.empty()? ::CreateDirectory(NtNewDirectory.data(), SecurityAttributes) : ::CreateDirectoryEx(NtTemplateDirectory.data(), NtNewDirectory.data(), SecurityAttributes);
		if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
		{
			Result = Global->Elevation->fCreateDirectoryEx(NtTemplateDirectory, NtNewDirectory, SecurityAttributes);
		}
		if(!Result)
		{
			// CreateDirectoryEx may fail on some FS, try to create anyway.
			NtTemplateDirectory.clear();
		}
	}
	return Result;
}

DWORD GetFileAttributes(const string& FileName)
{
	NTPath NtName(FileName);
	DWORD Result = ::GetFileAttributes(NtName.data());
	if(Result == INVALID_FILE_ATTRIBUTES && ElevationRequired(ELEVATION_READ_REQUEST))
	{
		Result = Global->Elevation->fGetFileAttributes(NtName);
	}
	return Result;
}

BOOL SetFileAttributes(const string& FileName,DWORD dwFileAttributes)
{
	NTPath NtName(FileName);
	BOOL Result = ::SetFileAttributes(NtName.data(), dwFileAttributes);
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
	{
		Result = Global->Elevation->fSetFileAttributes(NtName, dwFileAttributes);
	}
	return Result;

}

bool CreateSymbolicLinkInternal(const string& Object, const string& Target, DWORD dwFlags)
{
	return Imports().CreateSymbolicLinkW?
		(Imports().CreateSymbolicLink(Object.data(), Target.data(), dwFlags) != FALSE) :
		CreateReparsePoint(Target, Object, dwFlags&SYMBOLIC_LINK_FLAG_DIRECTORY?RP_SYMLINKDIR:RP_SYMLINKFILE);
}

bool CreateSymbolicLink(const string& SymlinkFileName, const string& TargetFileName,DWORD dwFlags)
{
	NTPath NtSymlinkFileName(SymlinkFileName);
	bool Result = CreateSymbolicLinkInternal(NtSymlinkFileName, TargetFileName, dwFlags);
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
	{
		Result=Global->Elevation->fCreateSymbolicLink(NtSymlinkFileName, TargetFileName, dwFlags);
	}
	return Result;
}

bool SetFileEncryptionInternal(const wchar_t* Name, bool Encrypt)
{
	return Encrypt? EncryptFile(Name)!=FALSE : DecryptFile(Name, 0)!=FALSE;
}

bool SetFileEncryption(const string& Name, bool Encrypt)
{
	NTPath NtName(Name);
	bool Result = SetFileEncryptionInternal(NtName.data(), Encrypt);
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST, false)) // Encryption implemented in adv32, NtStatus not affected
	{
		Result=Global->Elevation->fSetFileEncryption(NtName, Encrypt);
	}
	return Result;
}

bool CreateHardLinkInternal(const string& Object, const string& Target,LPSECURITY_ATTRIBUTES SecurityAttributes)
{
	bool Result = ::CreateHardLink(Object.data(), Target.data(), SecurityAttributes) != FALSE;
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
	{
		Result = Global->Elevation->fCreateHardLink(Object, Target, SecurityAttributes);
	}
	return Result;
}

BOOL CreateHardLink(const string& FileName, const string& ExistingFileName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	BOOL Result = CreateHardLinkInternal(NTPath(FileName),NTPath(ExistingFileName), lpSecurityAttributes);
	//bug in win2k: \\?\ fails
	if (!Result && !IsWindowsXPOrGreater())
	{
		Result = CreateHardLinkInternal(FileName, ExistingFileName, lpSecurityAttributes);
	}
	return Result;
}

HANDLE FindFirstStream(const string& FileName,STREAM_INFO_LEVELS InfoLevel,LPVOID lpFindStreamData,DWORD dwFlags)
{
	HANDLE Ret=INVALID_HANDLE_VALUE;
	if(Imports().FindFirstStreamW)
	{
		Ret=Imports().FindFirstStreamW(NTPath(FileName).data(),InfoLevel,lpFindStreamData,dwFlags);
	}
	else
	{
		if (InfoLevel==FindStreamInfoStandard)
		{
			auto Handle=new pseudo_handle;

				if (Handle->Object.Open(FileName, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING))
				{
					// for network paths buffer size must be <= 64k
					// we double it in a first loop, so starting value is 32k
					Handle->BufferSize = 0x8000;
					NTSTATUS Result = STATUS_SEVERITY_ERROR;
					do
					{
						Handle->BufferSize *= 2;
						Handle->BufferBase.reset(Handle->BufferSize);
						if (Handle->BufferBase)
						{
							// sometimes for directories NtQueryInformationFile returns STATUS_SUCCESS but doesn't fill the buffer
							auto StreamInfo = reinterpret_cast<PFILE_STREAM_INFORMATION>(Handle->BufferBase.get());
							StreamInfo->StreamNameLength = 0;
							Handle->Object.NtQueryInformationFile(Handle->BufferBase.get(), Handle->BufferSize, FileStreamInformation, &Result);
						}
					}
					while(Result == STATUS_BUFFER_OVERFLOW || Result == STATUS_BUFFER_TOO_SMALL);

					if (Result == STATUS_SUCCESS)
					{
						auto pFsd=static_cast<PWIN32_FIND_STREAM_DATA>(lpFindStreamData);
						auto StreamInfo = reinterpret_cast<PFILE_STREAM_INFORMATION>(Handle->BufferBase.get());
						Handle->NextOffset = StreamInfo->NextEntryOffset;
						if (StreamInfo->StreamNameLength)
						{
							std::copy_n(StreamInfo->StreamName, StreamInfo->StreamNameLength/sizeof(wchar_t), pFsd->cStreamName);
							pFsd->cStreamName[StreamInfo->StreamNameLength / sizeof(wchar_t)] = L'\0';
							pFsd->StreamSize=StreamInfo->StreamSize;
							Ret=Handle;
						}
					}

					Handle->Object.Close();
				}
				if (Ret == INVALID_HANDLE_VALUE)
				{
					delete Handle;
				}
		}
	}

	return Ret;
}

BOOL FindNextStream(HANDLE hFindStream,LPVOID lpFindStreamData)
{
	BOOL Ret=FALSE;
	if(Imports().FindFirstStreamW)
	{
		Ret=Imports().FindNextStreamW(hFindStream,lpFindStreamData);
	}
	else
	{
		auto Handle = static_cast<pseudo_handle*>(hFindStream);

		if (Handle->NextOffset)
		{
			auto pStreamInfo=reinterpret_cast<PFILE_STREAM_INFORMATION>(Handle->BufferBase.get() + Handle->NextOffset);
			auto pFsd=static_cast<PWIN32_FIND_STREAM_DATA>(lpFindStreamData);
			Handle->NextOffset = pStreamInfo->NextEntryOffset?Handle->NextOffset+pStreamInfo->NextEntryOffset:0;
			if (pStreamInfo->StreamNameLength && pStreamInfo->StreamNameLength < sizeof(pFsd->cStreamName))
			{
				std::copy_n(pStreamInfo->StreamName, pStreamInfo->StreamNameLength / sizeof(wchar_t), pFsd->cStreamName);
				pFsd->cStreamName[pStreamInfo->StreamNameLength / sizeof(wchar_t)] = L'\0';
				pFsd->StreamSize=pStreamInfo->StreamSize;
				Ret=TRUE;
			}
		}
	}

	return Ret;
}

BOOL FindStreamClose(HANDLE hFindStream)
{
	BOOL Ret=FALSE;

	if(Imports().FindFirstStreamW)
	{
		Ret=::FindClose(hFindStream);
	}
	else
	{
		delete static_cast<pseudo_handle*>(hFindStream);
		Ret=TRUE;
	}

	return Ret;
}

std::vector<string> GetLogicalDriveStrings()
{
	FN_RETURN_TYPE(GetLogicalDriveStrings) Result;
	wchar_t Buffer[MAX_PATH];
	DWORD Size = ::GetLogicalDriveStrings(ARRAYSIZE(Buffer), Buffer);

	if (Size)
	{
		const wchar_t* Ptr;
		wchar_t_ptr vBuffer;

		if (Size < ARRAYSIZE(Buffer))
		{
			Ptr = Buffer;
		}
		else
		{
			vBuffer.reset(Size);
			Size = ::GetLogicalDriveStrings(Size, vBuffer.get());
			Ptr = vBuffer.get();
		}

		while(*Ptr)
		{
			Result.emplace_back(Ptr);
			Ptr += wcslen(Ptr) + 1;
		}
	}
	return Result;
}

bool internalNtQueryGetFinalPathNameByHandle(HANDLE hFile, string& FinalFilePath)
{
	ULONG RetLen;
	NTSTATUS Res = STATUS_SUCCESS;
	string NtPath;

	{
		ULONG BufSize = NT_MAX_PATH;
		block_ptr<OBJECT_NAME_INFORMATION> oni(BufSize);
		Res = Imports().NtQueryObject(hFile, ObjectNameInformation, oni.get(), BufSize, &RetLen);

		if (Res == STATUS_BUFFER_OVERFLOW || Res == STATUS_BUFFER_TOO_SMALL)
		{
			oni.reset(BufSize = RetLen);
			Res = Imports().NtQueryObject(hFile, ObjectNameInformation, oni.get(), BufSize, &RetLen);
		}

		if (Res == STATUS_SUCCESS)
		{
			NtPath.assign(oni->Name.Buffer, oni->Name.Length / sizeof(WCHAR));
		}
	}

	FinalFilePath.clear();

	if (Res == STATUS_SUCCESS)
	{
		// simple way to handle network paths
		if (NtPath.compare(0, 24, L"\\Device\\LanmanRedirector") == 0)
			FinalFilePath = NtPath.replace(0, 24, 1, L'\\');

		if (FinalFilePath.empty())
		{
			// try to convert NT path (\Device\HarddiskVolume1) to drive letter
			auto Strings = GetLogicalDriveStrings();
			std::any_of(CONST_RANGE(Strings, i) -> bool
			{
				int Len = MatchNtPathRoot(NtPath, i);

				if (Len)
				{
					if (NtPath.compare(0, 14, L"\\Device\\WinDfs") == 0)
						FinalFilePath = NtPath.replace(0, Len, 1, L'\\');
					else
						FinalFilePath = NtPath.replace(0, Len, i);
					return true;
				}
				return false;
			});
		}

		if (FinalFilePath.empty())
		{
			// try to convert NT path (\Device\HarddiskVolume1) to \\?\Volume{...} path
			wchar_t VolumeName[MAX_PATH];
			HANDLE hEnum = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
			BOOL Result = hEnum != INVALID_HANDLE_VALUE;

			while (Result)
			{
				DeleteEndSlash(VolumeName);
				int Len = MatchNtPathRoot(NtPath, VolumeName + 4 /* w/o prefix */);

				if (Len)
				{
					FinalFilePath = NtPath.replace(0, Len, VolumeName);
					break;
				}

				Result = FindNextVolumeW(hEnum, VolumeName, ARRAYSIZE(VolumeName));
			}

			if (hEnum != INVALID_HANDLE_VALUE)
				FindVolumeClose(hEnum);
		}
	}

	return !FinalFilePath.empty();
}

bool GetFinalPathNameByHandle(HANDLE hFile, string& FinalFilePath)
{
	if (Imports().GetFinalPathNameByHandleW)
	{
		wchar_t Buffer[MAX_PATH];
		size_t Size = Imports().GetFinalPathNameByHandle(hFile, Buffer, ARRAYSIZE(Buffer), VOLUME_NAME_GUID);
		if (Size < ARRAYSIZE(Buffer))
		{
			FinalFilePath.assign(Buffer, Size);
		}
		else
		{
			wchar_t_ptr vBuffer(Size);
			Size = Imports().GetFinalPathNameByHandle(hFile, vBuffer.get(), static_cast<DWORD>(vBuffer.size()), VOLUME_NAME_GUID);
			FinalFilePath.assign(vBuffer.get(), Size);
		}

		return Size != 0;
	}

	return internalNtQueryGetFinalPathNameByHandle(hFile, FinalFilePath);
}

bool SearchPath(const wchar_t *Path, const string& FileName, const wchar_t *Extension, string &strDest)
{
	DWORD dwSize = ::SearchPath(Path,FileName.data(),Extension,0,nullptr,nullptr);

	if (dwSize)
	{
		wchar_t_ptr Buffer(dwSize);
		dwSize = ::SearchPath(Path, FileName.data(), Extension, dwSize, Buffer.get(), nullptr);
		strDest.assign(Buffer.get(), dwSize);
		return true;
	}

	return false;
}

bool QueryDosDevice(const string& DeviceName, string &Path)
{
	SetLastError(NO_ERROR);
	wchar_t Buffer[MAX_PATH];
	DWORD Size = ::QueryDosDevice(DeviceName.data(), Buffer, ARRAYSIZE(Buffer));
	if (Size)
	{
		Path = Buffer; // don't copy trailing '\0'
	}
	else if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		wchar_t_ptr vBuffer(NT_MAX_PATH);
		SetLastError(NO_ERROR);
		Size = ::QueryDosDevice(DeviceName.data(), vBuffer.get(), static_cast<DWORD>(vBuffer.size()));
		if (Size)
		{
			Path = vBuffer.get();
		}
	}

	return Size && ::GetLastError() == NO_ERROR;
}

bool GetVolumeNameForVolumeMountPoint(const string& VolumeMountPoint,string& VolumeName)
{
	bool Result=false;
	WCHAR VolumeNameBuffer[50];
	NTPath strVolumeMountPoint(VolumeMountPoint);
	AddEndSlash(strVolumeMountPoint);
	if(::GetVolumeNameForVolumeMountPoint(strVolumeMountPoint.data(),VolumeNameBuffer,ARRAYSIZE(VolumeNameBuffer)))
	{
		VolumeName=VolumeNameBuffer;
		Result=true;
	}
	return Result;
}

void EnableLowFragmentationHeap()
{
	if (Imports().HeapSetInformation)
	{
		std::vector<HANDLE> Heaps(10);
		DWORD ActualNumHeaps = ::GetProcessHeaps(static_cast<DWORD>(Heaps.size()), Heaps.data());
		if(ActualNumHeaps > Heaps.size())
		{
			Heaps.resize(ActualNumHeaps);
			ActualNumHeaps = ::GetProcessHeaps(static_cast<DWORD>(Heaps.size()), Heaps.data());
		}
		Heaps.resize(ActualNumHeaps);
		std::for_each(CONST_RANGE(Heaps, i)
		{
			ULONG HeapFragValue = 2;
			Imports().HeapSetInformation(i, HeapCompatibilityInformation, &HeapFragValue, sizeof(HeapFragValue));
		});
	}
}

bool GetFileTimeSimple(const string &FileName, LPFILETIME CreationTime, LPFILETIME LastAccessTime, LPFILETIME LastWriteTime, LPFILETIME ChangeTime)
{
	fs::file dir;
	if (dir.Open(FileName,FILE_READ_ATTRIBUTES,FILE_SHARE_DELETE|FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING))
	{
		return dir.GetTime(CreationTime,LastAccessTime,LastWriteTime,ChangeTime);
	}
	return false;
}

bool GetFileTimeEx(HANDLE Object, LPFILETIME CreationTime, LPFILETIME LastAccessTime, LPFILETIME LastWriteTime, LPFILETIME ChangeTime)
{
	bool Result = false;
	const ULONG Length = 40;
	BYTE Buffer[Length] = {};
	auto fbi = reinterpret_cast<PFILE_BASIC_INFORMATION>(Buffer);
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Status = Imports().NtQueryInformationFile(Object, &IoStatusBlock, fbi, Length, FileBasicInformation);
	::SetLastError(Imports().RtlNtStatusToDosError(Status));
	if (Status == STATUS_SUCCESS)
	{
		if(CreationTime)
		{
			*CreationTime = UI64ToFileTime(fbi->CreationTime.QuadPart);
		}
		if(LastAccessTime)
		{
			*LastAccessTime = UI64ToFileTime(fbi->LastAccessTime.QuadPart);
		}
		if(LastWriteTime)
		{
			*LastWriteTime = UI64ToFileTime(fbi->LastWriteTime.QuadPart);
		}
		if(ChangeTime)
		{
			*ChangeTime = UI64ToFileTime(fbi->ChangeTime.QuadPart);
		}
		Result = true;
	}
	return Result;
}

bool SetFileTimeEx(HANDLE Object, const FILETIME* CreationTime, const FILETIME* LastAccessTime, const FILETIME* LastWriteTime, const FILETIME* ChangeTime)
{
	bool Result = false;
	const ULONG Length = 40;
	BYTE Buffer[Length] = {};
	auto fbi = reinterpret_cast<PFILE_BASIC_INFORMATION>(Buffer);
	if(CreationTime)
	{
		fbi->CreationTime.QuadPart = FileTimeToUI64(*CreationTime);
	}
	if(LastAccessTime)
	{
		fbi->LastAccessTime.QuadPart = FileTimeToUI64(*LastAccessTime);
	}
	if(LastWriteTime)
	{
		fbi->LastWriteTime.QuadPart = FileTimeToUI64(*LastWriteTime);
	}
	if(ChangeTime)
	{
		fbi->ChangeTime.QuadPart = FileTimeToUI64(*ChangeTime);
	}
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Status = Imports().NtSetInformationFile(Object, &IoStatusBlock, fbi, Length, FileBasicInformation);
	::SetLastError(Imports().RtlNtStatusToDosError(Status));
	Result = Status == STATUS_SUCCESS;
	return Result;
}

bool GetFileSecurity(const string& Object, SECURITY_INFORMATION RequestedInformation, FAR_SECURITY_DESCRIPTOR& SecurityDescriptor)
{
	bool Result = false;
	NTPath NtObject(Object);
	DWORD LengthNeeded = 0;
	::GetFileSecurity(NtObject.data(), RequestedInformation, nullptr, 0, &LengthNeeded);
	if(GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		SecurityDescriptor.reset(LengthNeeded);
		Result = ::GetFileSecurity(NtObject.data(), RequestedInformation, SecurityDescriptor.get(), LengthNeeded, &LengthNeeded) != FALSE;
	}
	return Result;
}

bool SetFileSecurity(const string& Object, SECURITY_INFORMATION RequestedInformation, const FAR_SECURITY_DESCRIPTOR& SecurityDescriptor)
{
	return ::SetFileSecurity(NTPath(Object).data(), RequestedInformation, SecurityDescriptor.get()) != FALSE;
}

bool DetachVirtualDiskInternal(const string& Object, VIRTUAL_STORAGE_TYPE& VirtualStorageType)
{
	HANDLE Handle;
	DWORD Result = Imports().OpenVirtualDisk(&VirtualStorageType, Object.data(), VIRTUAL_DISK_ACCESS_DETACH, OPEN_VIRTUAL_DISK_FLAG_NONE, nullptr, &Handle);
	if (Result == ERROR_SUCCESS)
	{
		Result = Imports().DetachVirtualDisk(Handle, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
		if (Result != ERROR_SUCCESS)
		{
			SetLastError(Result);
		}
	}
	else
	{
		SetLastError(Result);
	}
	return Result == ERROR_SUCCESS;
}

bool DetachVirtualDisk(const string& Object, VIRTUAL_STORAGE_TYPE& VirtualStorageType)
{
	NTPath NtObject(Object);
	bool Result = DetachVirtualDiskInternal(NtObject, VirtualStorageType);
	if(!Result && ElevationRequired(ELEVATION_MODIFY_REQUEST))
	{
		Result = Global->Elevation->fDetachVirtualDisk(NtObject, VirtualStorageType);
	}
	return Result;
}

string GetPrivateProfileString(const string& AppName, const string& KeyName, const string& Default, const string& FileName)
{
	wchar_t_ptr Buffer(NT_MAX_PATH);
	DWORD size = ::GetPrivateProfileString(AppName.data(), KeyName.data(), Default.data(), Buffer.get(), static_cast<DWORD>(Buffer.size()), FileName.data());
	return string(Buffer.get(), size);
}

DWORD GetAppPathsRedirectionFlag()
{
	DWORD RedirectionFlag = 0;
	static bool Checked = false;
	if (!Checked)
	{
		// App Paths key is shared in Windows 7 and above
		if (!IsWindows7OrGreater())
		{
#ifdef _WIN64
			RedirectionFlag = KEY_WOW64_32KEY;
#else
			BOOL Wow64Process = FALSE;
			if (Imports().IsWow64Process(GetCurrentProcess(), &Wow64Process) && Wow64Process)
			{
				RedirectionFlag = KEY_WOW64_64KEY;
			}
#endif
		}
		Checked = true;
	}
	return RedirectionFlag;
}

	namespace reg
	{
		key::key(HKEY RootKey, const wchar_t* SubKey, REGSAM Sam):
			m_Key(),
			m_Opened()
		{
			m_Opened = RegOpenKeyEx(RootKey, SubKey, 0, Sam, &m_Key) == ERROR_SUCCESS;
		}

		key::~key()
		{
			if (m_Opened)
				RegCloseKey(m_Key);
		}

		static bool QueryValue(HKEY Key, const wchar_t* Name, DWORD& Type, std::vector<char>& Value)
		{
			bool Result = false;
			DWORD Size = 0;
			if (RegQueryValueEx(Key, Name, nullptr, nullptr, nullptr, &Size) == ERROR_SUCCESS)
			{
				Value.resize(Size);
				Result = RegQueryValueEx(Key, Name, nullptr, &Type, reinterpret_cast<LPBYTE>(Value.data()), &Size) == ERROR_SUCCESS;
			}
			return Result;
		}

		bool EnumKey(HKEY Key, size_t Index, string& Name)
		{
			LONG ExitCode = ERROR_MORE_DATA;

			for (DWORD Size = 512; ExitCode == ERROR_MORE_DATA; Size *= 2)
			{
				wchar_t_ptr Buffer(Size);
				DWORD RetSize = Size;
				ExitCode = RegEnumKeyEx(Key, static_cast<DWORD>(Index), Buffer.get(), &RetSize, nullptr, nullptr, nullptr, nullptr);
				if (ExitCode == ERROR_SUCCESS)
				{
					Name.assign(Buffer.get(), RetSize);
				}
			}
			return ExitCode == ERROR_SUCCESS;
		}

		bool EnumValue(HKEY Key, size_t Index, value& Value)
		{
			LONG ExitCode = ERROR_MORE_DATA;

			for (DWORD Size = 512; ExitCode == ERROR_MORE_DATA; Size *= 2)
			{
				wchar_t_ptr Buffer(Size);
				DWORD RetSize = Size;
				ExitCode = RegEnumValue(Key, static_cast<DWORD>(Index), Buffer.get(), &RetSize, nullptr, &Value.m_Type, nullptr, nullptr);
				if (ExitCode == ERROR_SUCCESS)
				{
					Value.m_Name.assign(Buffer.get(), RetSize);
					Value.m_Key = Key;
				}
			}

			return ExitCode == ERROR_SUCCESS;
		}

		bool GetValue(HKEY Key, const wchar_t* Name, string& Value)
		{
			bool Result = false;
			std::vector<char> Buffer;
			DWORD Type;
			if (QueryValue(Key, Name, Type, Buffer) && IsStringType(Type))
			{
				Value = string(reinterpret_cast<const wchar_t*>(Buffer.data()), Buffer.size() / sizeof(wchar_t));
				if (!Value.empty() && Value.back() == L'\0')
				{
					Value.pop_back();
				}
				Result = true;
			}
			return Result;
		}

		bool GetValue(HKEY Key, const wchar_t* Name, unsigned int& Value)
		{
			bool Result = false;
			std::vector<char> Buffer;
			DWORD Type;
			if (QueryValue(Key, Name, Type, Buffer) && Type == REG_DWORD)
			{
				Value = 0;
				memcpy(&Value, Buffer.data(), std::min(Buffer.size(), sizeof(Value)));
				Result = true;
			}
			return Result;
		}

		bool GetValue(HKEY Key, const wchar_t* Name, uint64_t& Value)
		{
			bool Result = false;
			std::vector<char> Buffer;
			DWORD Type;
			if (QueryValue(Key, Name, Type, Buffer) && Type == REG_QWORD)
			{
				Value = 0;
				memcpy(&Value, Buffer.data(), std::min(Buffer.size(), sizeof(Value)));
				Result = true;
			}
			return Result;
		}

		string value::GetString() const
		{
			if (!IsStringType(m_Type))
				throw std::runtime_error("bad value type");

			string Result;
			GetValue(m_Key, m_Name.data(), Result);
			return Result;
		}

		unsigned int value::GetUnsigned() const
		{
			if (m_Type != REG_DWORD)
				throw std::runtime_error("bad value type");

			unsigned int Result = 0;
			GetValue(m_Key, m_Name.data(), Result);
			return Result;
		}

		uint64_t value::GetUnsigned64() const
		{
			if (m_Type != REG_QWORD)
				throw std::runtime_error("bad value type");

			uint64_t Result = 0;
			GetValue(m_Key, m_Name.data(), Result);
			return Result;
		}
	}


	namespace env
	{
		enum_strings::enum_strings():
			m_Environment(GetEnvironmentStrings())
		{
		}

		enum_strings::~enum_strings()
		{
			FreeEnvironmentStrings(m_Environment);
		}

		bool enum_strings::get(size_t index, const wchar_t*& value)
		{
			return *(value = index? value + wcslen(value) + 1 : m_Environment) != L'\0';
		}

		bool get_variable(const wchar_t* Name, string& strBuffer)
		{
			WCHAR Buffer[MAX_PATH];
			DWORD Size = ::GetEnvironmentVariable(Name, Buffer, ARRAYSIZE(Buffer));

			if (Size)
			{
				if (Size < ARRAYSIZE(Buffer))
				{
					strBuffer.assign(Buffer, Size);
				}
				else
				{
					std::vector<wchar_t> vBuffer(Size);
					Size = ::GetEnvironmentVariable(Name, vBuffer.data(), Size);
					strBuffer.assign(vBuffer.data(), Size);
				}
			}

			return Size != 0;
		}

		bool set_variable(const wchar_t* Name, const wchar_t* Value)
		{
			return ::SetEnvironmentVariable(Name, Value) != FALSE;
		}

		bool delete_variable(const wchar_t* Name)
		{
			return ::SetEnvironmentVariable(Name, nullptr) != FALSE;
		}

		string expand_strings(const wchar_t* str)
		{
			WCHAR Buffer[MAX_PATH];
			DWORD Size = ::ExpandEnvironmentStrings(str, Buffer, ARRAYSIZE(Buffer));
			if (Size)
			{
				if (Size <= ARRAYSIZE(Buffer))
				{
					return string(Buffer, Size - 1);
				}
				else
				{
					std::vector<wchar_t> vBuffer(Size);
					Size = ::ExpandEnvironmentStrings(str, vBuffer.data(), Size);
					return string(vBuffer.data(), Size - 1);
				}
			}
			return str;
		}
	}

	namespace rtdl
	{
		module::~module()
		{
			if (m_loaded)
			{
				FreeLibrary(m_module);
			}
		}

		HMODULE module::get_module() const
		{
			if (!m_tried && !m_module)
			{
				m_tried = true;
				m_module = LoadLibrary(m_name);

				if (!m_module && m_AlternativeLoad && IsAbsolutePath(m_name))
				{
					m_module = LoadLibraryEx(m_name, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
				}
				// TODO: log if nullptr
				m_loaded = m_module != nullptr;
			}
			return m_module;
		}
	}
}
