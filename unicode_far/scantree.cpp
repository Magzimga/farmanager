/*
scantree.cpp

������������ �������� �������� �, �����������, ������������ ��
������� ���� ������
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

#include "scantree.hpp"
#include "syslog.hpp"
#include "config.hpp"
#include "pathmix.hpp"

struct ScanTree::scantree_item: noncopyable
{
public:
	scantree_item() {}

	scantree_item(scantree_item&& rhs) noexcept { *this = std::move(rhs); }

	MOVE_OPERATOR_BY_SWAP(scantree_item);

	void swap(scantree_item& rhs) noexcept
	{
		using std::swap;
		swap(Flags, rhs.Flags);
		Find.swap(rhs.Find);
		swap(Iterator, rhs.Iterator);
		RealPath.swap(rhs.RealPath);
	}

	FREE_SWAP(scantree_item);

	BitFlags Flags;
	std::unique_ptr<api::fs::enum_file> Find;
	api::fs::enum_file::iterator Iterator;
	string RealPath;
};

ScanTree::ScanTree(bool RetUpDir, bool Recurse, int ScanJunction)
{
	Flags.Change(FSCANTREE_RETUPDIR,RetUpDir);
	Flags.Change(FSCANTREE_RECUR,Recurse);
	Flags.Change(FSCANTREE_SCANSYMLINK,(ScanJunction==-1?(bool)Global->Opt->ScanJunction:ScanJunction!=0));
}

ScanTree::~ScanTree()
{
}

void ScanTree::SetFindPath(const string& Path,const string& Mask, const DWORD NewScanFlags)
{
	ScanItems.clear();
	ScanItems.emplace_back(scantree_item());
	Flags.Clear(FSCANTREE_FILESFIRST);
	strFindMask = Mask;
	strFindPath = Path;
	strFindPathOriginal = strFindPath;
	AddEndSlash(strFindPathOriginal);
	ConvertNameToReal(strFindPath, strFindPath);
	strFindPath = NTPath(strFindPath);
	ScanItems.back().RealPath = strFindPath;
	AddEndSlash(strFindPath);
	strFindPath += strFindMask;
	Flags.Set((Flags.Flags()&0x0000FFFF)|(NewScanFlags&0xFFFF0000));
}

bool ScanTree::GetNextName(api::FAR_FIND_DATA *fdata,string &strFullName)
{
	if (ScanItems.empty())
		return false;

	bool Done=false;
	Flags.Clear(FSCANTREE_SECONDDIRNAME);

	{
		scantree_item& LastItem = ScanItems.back();
		for (;;)
		{
			if (!LastItem.Find)
			{
				LastItem.Find = std::make_unique<api::fs::enum_file>(strFindPath);
				LastItem.Iterator = LastItem.Find->end();
			}

			if (LastItem.Iterator == LastItem.Find->end())
			{
				LastItem.Iterator = LastItem.Find->begin();
			}
			else
			{
				++LastItem.Iterator;
			}

			Done = LastItem.Iterator == LastItem.Find->end();

			if (!Done)
			{
				*fdata = *ScanItems.back().Iterator;
			}

			if (Flags.Check(FSCANTREE_FILESFIRST))
			{
				if (LastItem.Flags.Check(FSCANTREE_SECONDPASS))
				{
					if (!Done && !(fdata->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						continue;
				}
				else
				{
					if (!Done && (fdata->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						continue;

					if (Done)
					{
						LastItem.Find.reset();
						LastItem.Flags.Set(FSCANTREE_SECONDPASS);
						continue;
					}
				}
			}

			break;
		}
	}

	if (Done)
	{
		ScanItems.pop_back();

		if (ScanItems.empty())
			return false;
		else
		{
			if (ScanItems.back().Flags.Check(FSCANTREE_INSIDEJUNCTION))
				Flags.Clear(FSCANTREE_INSIDEJUNCTION);

			CutToSlash(strFindPath,true);
			CutToSlash(strFindPathOriginal,true);

			if (Flags.Check(FSCANTREE_RETUPDIR))
			{
				strFullName = strFindPathOriginal;
				api::GetFindDataEx(strFindPath, *fdata);
			}

			CutToSlash(strFindPath);
			strFindPath += strFindMask;
			_SVS(SysLog(L"1. FullName='%s'",strFullName.data()));

			if (Flags.Check(FSCANTREE_RETUPDIR))
			{
				Flags.Set(FSCANTREE_SECONDDIRNAME);
				return true;
			}

			return GetNextName(fdata,strFullName);
		}
	}
	else
	{
		if ((fdata->dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && Flags.Check(FSCANTREE_RECUR) &&
		        (!(fdata->dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || Flags.Check(FSCANTREE_SCANSYMLINK)))
		{
			string RealPath(ScanItems.back().RealPath);
			AddEndSlash(RealPath);
			RealPath += fdata->strFileName;

			if (fdata->dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
				ConvertNameToReal(RealPath, RealPath);

			//recursive symlinks guard
			if (std::none_of(CONST_RANGE(ScanItems, i) {return i.RealPath == RealPath;}))
			{
				CutToSlash(strFindPath);
				CutToSlash(strFindPathOriginal);
				strFindPath += fdata->strFileName;
				strFindPathOriginal += fdata->strFileName;
				strFullName = strFindPathOriginal;
				AddEndSlash(strFindPathOriginal);
				strFindPath += L"\\";
				strFindPath += strFindMask;
				scantree_item Data;
				Data.Flags = ScanItems.back().Flags; // ��������� ����
				Data.Flags.Clear(FSCANTREE_SECONDPASS);
				Data.RealPath = RealPath;

				if (fdata->dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
				{
					Data.Flags.Set(FSCANTREE_INSIDEJUNCTION);
					Flags.Set(FSCANTREE_INSIDEJUNCTION);
				}
				ScanItems.emplace_back(std::move(Data));

				return true;
			}
		}
	}

	strFullName = strFindPathOriginal;
	CutToSlash(strFullName);
	strFullName += fdata->strFileName;

	return true;
}

void ScanTree::SkipDir()
{
	if (ScanItems.empty())
		return;

	ScanItems.pop_back();

	if (ScanItems.empty())
		return;

	if (!ScanItems.back().Flags.Check(FSCANTREE_INSIDEJUNCTION))
		Flags.Clear(FSCANTREE_INSIDEJUNCTION);

	CutToSlash(strFindPath,true);
	CutToSlash(strFindPathOriginal,true);
	CutToSlash(strFindPath);
	CutToSlash(strFindPathOriginal);
	strFindPath += strFindMask;
}
