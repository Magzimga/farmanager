/*
edit.cpp

������ ���������
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

#include "edit.hpp"
#include "keyboard.hpp"
#include "macroopcode.hpp"
#include "ctrlobj.hpp"
#include "plugins.hpp"
#include "scrbuf.hpp"
#include "interf.hpp"
#include "clipboard.hpp"
#include "xlat.hpp"
#include "shortcuts.hpp"
#include "pathmix.hpp"
#include "panelmix.hpp"
#include "manager.hpp"
#include "editor.hpp"
#include "window.hpp"
#include "colormix.hpp"

void ColorItem::SetOwner(const GUID& Value)
{
	static std::unordered_set<GUID, uuid_hash, uuid_equal> GuidSet;
	Owner = &*GuidSet.emplace(Value).first;
}

void ColorItem::SetColor(const FarColor& Value)
{
	Color = colors::StoreColor(Value);
}

static int Recurse=0;

enum {EOL_NONE,EOL_CR,EOL_LF,EOL_CRLF,EOL_CRCRLF};
static const wchar_t *EOL_TYPE_CHARS[]={L"",L"\r",L"\n",L"\r\n",L"\r\r\n"};

static const wchar_t EDMASK_ANY    = L'X'; // ��������� ������� � ������ ����� ����� ������;
static const wchar_t EDMASK_DSS    = L'#'; // ��������� ������� � ������ ����� �����, ������ � ���� ������;
static const wchar_t EDMASK_DIGIT  = L'9'; // ��������� ������� � ������ ����� ������ �����;
static const wchar_t EDMASK_DIGITS = L'N'; // ��������� ������� � ������ ����� ������ ����� � �������;
static const wchar_t EDMASK_ALPHA  = L'A'; // ��������� ������� � ������ ����� ������ �����.
static const wchar_t EDMASK_HEX    = L'H'; // ��������� ������� � ������ ����� ����������������� �������.

Edit::Edit(window_ptr Owner):
	SimpleScreenObject(Owner),
	m_CurPos(0),
	m_SelStart(-1),
	m_SelEnd(0),
	LeftPos(0),
	EndType(EOL_NONE)
{
	m_Flags.Set(FEDITLINE_EDITBEYONDEND);
	m_Flags.Change(FEDITLINE_DELREMOVESBLOCKS,Global->Opt->EdOpt.DelRemovesBlocks);
	m_Flags.Change(FEDITLINE_PERSISTENTBLOCKS,Global->Opt->EdOpt.PersistentBlocks);
	m_Flags.Change(FEDITLINE_SHOWWHITESPACE,Global->Opt->EdOpt.ShowWhiteSpace!=0);
	m_Flags.Change(FEDITLINE_SHOWLINEBREAK,Global->Opt->EdOpt.ShowWhiteSpace==1);
}

Edit::Edit(Edit&& rhs) noexcept:
	SimpleScreenObject(rhs.GetOwner()),
	m_CurPos(),
	m_SelStart(-1),
	m_SelEnd(),
	LeftPos(),
	EndType(EOL_NONE)
{
	*this = std::move(rhs);
}


void Edit::DisplayObject()
{
	if (m_Flags.Check(FEDITLINE_DROPDOWNBOX))
	{
		m_Flags.Clear(FEDITLINE_CLEARFLAG);  // ��� ����-���� ��� �� ����� �������� unchanged text
		m_SelStart=0;
		m_SelEnd = m_Str.size(); // � ����� ������� ��� ��� �������� -
		//    ���� �� ���������� �� ������� Edit
	}

	//   ���������� ������ ��������� ������� � ������ � ������ Mask.
	int Value=(GetPrevCurPos()>m_CurPos)?-1:1;
	m_CurPos=GetNextCursorPos(m_CurPos,Value);
	FastShow();

	/* $ 26.07.2000 tran
	   ��� DropDownBox ������ ���������
	   �� ���� ���� - ���������� �� �� ����� ������� ����� */
	if (m_Flags.Check(FEDITLINE_DROPDOWNBOX))
		::SetCursorType(0,10);
	else
	{
		if (m_Flags.Check(FEDITLINE_OVERTYPE))
		{
			int NewCursorSize=IsConsoleFullscreen()?
			                  (Global->Opt->CursorSize[3]?(int)Global->Opt->CursorSize[3]:99):
					                  (Global->Opt->CursorSize[2]?(int)Global->Opt->CursorSize[2]:99);
			::SetCursorType(1,GetCursorSize()==-1?NewCursorSize:GetCursorSize());
		}
		else
{
			int NewCursorSize=IsConsoleFullscreen()?
			                  (Global->Opt->CursorSize[1]?(int)Global->Opt->CursorSize[1]:10):
					                  (Global->Opt->CursorSize[0]?(int)Global->Opt->CursorSize[0]:10);
			::SetCursorType(1,GetCursorSize()==-1?NewCursorSize:GetCursorSize());
		}
	}

	MoveCursor(m_X1+GetLineCursorPos()-LeftPos,m_Y1);
}

void Edit::SetCursorType(bool Visible, DWORD Size)
{
	m_Flags.Change(FEDITLINE_CURSORVISIBLE,Visible);
	SetCursorSize(Size);
	::SetCursorType(Visible,Size);
}

void Edit::GetCursorType(bool& Visible, DWORD& Size) const
{
	Visible=m_Flags.Check(FEDITLINE_CURSORVISIBLE);
	Size = GetCursorSize();
}

//   ���������� ������ ��������� ������� � ������ � ������ Mask.
int Edit::GetNextCursorPos(int Position,int Where) const
{
	int Result = Position;
	auto Mask = GetInputMask();

	if (!Mask.empty() && (Where==-1 || Where==1))
	{
		int PosChanged=FALSE;
		const int MaskLen = static_cast<int>(Mask.size());

		for (int i=Position; i<MaskLen && i>=0; i+=Where)
		{
			if (CheckCharMask(Mask[i]))
			{
				Result=i;
				PosChanged=TRUE;
				break;
			}
		}

		if (!PosChanged)
		{
			for (int i=std::min(Position, static_cast<int>(Mask.size())); i>=0; i--)
			{
				if (CheckCharMask(Mask[i]))
				{
					Result=i;
					PosChanged=TRUE;
					break;
				}
			}
		}

		if (!PosChanged)
		{
			const auto It = std::find_if(ALL_CONST_RANGE(Mask), CheckCharMask);
			if (It != Mask.cend())
			{
				Result = It - Mask.cbegin();
			}
		}
	}

	return Result;
}

void Edit::FastShow(const Edit::ShowInfo* Info)
{
	const size_t EditLength=ObjWidth();

	if (!m_Flags.Check(FEDITLINE_EDITBEYONDEND) && !m_Str.empty() && m_CurPos > m_Str.size())
		m_CurPos = m_Str.size();

	if (GetMaxLength()!=-1)
	{
		if (m_Str.size() > GetMaxLength())
		{
			m_Str.resize(GetMaxLength());
		}

		if (m_CurPos>GetMaxLength()-1)
			m_CurPos=GetMaxLength()>0 ? (GetMaxLength()-1):0;
	}

	int TabCurPos=GetTabCurPos();

	/* $ 31.07.2001 KM
	  ! ��� ���������� ������� ����������� ������
	    � ������ �������.
	*/
	if (!m_Flags.Check(FEDITLINE_DROPDOWNBOX))
	{
		FixLeftPos(TabCurPos);
	}

	int FocusedLeftPos = LeftPos, XPos = TabCurPos - LeftPos;
	if(Info)
	{
		FocusedLeftPos = Info->LeftPos;
		XPos = Info->CurTabPos - Info->LeftPos;
	}

	GotoXY(m_X1,m_Y1);
	int TabSelStart=(m_SelStart==-1) ? -1:RealPosToTab(m_SelStart);
	int TabSelEnd=(m_SelEnd<0) ? -1:RealPosToTab(m_SelEnd);

	/* $ 17.08.2000 KM
	   ���� ���� �����, ������� ���������� ������, �� ����
	   ��� "����������" ������� � �����, �� ���������� ����������
	   ������ ��������� �������������� � Str
	*/
	auto Mask = GetInputMask();
	if (!Mask.empty())
		RefreshStrByMask();

	string OutStr, OutStrTmp;
	OutStr.reserve(EditLength);
	OutStrTmp.reserve(EditLength);

	SetLineCursorPos(TabCurPos);
	int RealLeftPos=TabPosToReal(LeftPos);

	OutStrTmp.assign(m_Str.data() + RealLeftPos, std::max(0, std::min(static_cast<int>(EditLength), m_Str.size() - RealLeftPos)));

	{
		auto TrailingSpaces = OutStrTmp.cend();
		if (m_Flags.Check(FEDITLINE_PARENT_SINGLELINE|FEDITLINE_PARENT_MULTILINE) && Mask.empty() && !OutStrTmp.empty())
		{
			TrailingSpaces = std::find_if_not(OutStrTmp.crbegin(), OutStrTmp.crend(), [](wchar_t i) { return IsSpace(i);}).base();
		}

		FOR_RANGE(OutStrTmp, i)
		{
			if ((m_Flags.Check(FEDITLINE_SHOWWHITESPACE) && m_Flags.Check(FEDITLINE_EDITORMODE)) || i >= TrailingSpaces)
			{
				if (*i==L' ') // *p==L'\xA0' ==> NO-BREAK SPACE
				{
					*i=L'\xB7';
				}
			}

			if (*i == L'\t')
			{
				const size_t S = GetTabSize() - ((FocusedLeftPos + OutStr.size()) % GetTabSize());
				OutStr.push_back((((m_Flags.Check(FEDITLINE_SHOWWHITESPACE) && m_Flags.Check(FEDITLINE_EDITORMODE)) || i >= TrailingSpaces) && (!OutStr.empty() || S==GetTabSize()))?L'\x2192':L' ');
				const auto PaddedSize = std::min(OutStr.size() + S - 1, EditLength);
				OutStr.resize(PaddedSize, L' ');
			}
			else
			{
				OutStr.push_back(!*i? L' ' : *i);
			}

			if (OutStr.size() >= EditLength)
			{
				break;
			}
		}

		if (m_Flags.Check(FEDITLINE_PASSWORDMODE))
			OutStr.assign(OutStr.size(), L'*');

		if (m_Flags.Check(FEDITLINE_SHOWLINEBREAK) && m_Flags.Check(FEDITLINE_EDITORMODE) && (m_Str.size() >= RealLeftPos) && (OutStr.size() < EditLength))
		{
			switch(EndType)
			{
			case EOL_CR:
				OutStr.push_back(Oem2Unicode[13]);
				break;
			case EOL_LF:
				OutStr.push_back(Oem2Unicode[10]);
				break;
			case EOL_CRLF:
				OutStr.push_back(Oem2Unicode[13]);
				if(OutStr.size() < EditLength)
				{
					OutStr.push_back(Oem2Unicode[10]);
				}
				break;
			case EOL_CRCRLF:
				OutStr.push_back(Oem2Unicode[13]);
				if(OutStr.size() < EditLength)
				{
					OutStr.push_back(Oem2Unicode[13]);
					if(OutStr.size() < EditLength)
					{
						OutStr.push_back(Oem2Unicode[10]);
					}
				}
				break;
			}
		}

		if (m_Flags.Check(FEDITLINE_SHOWWHITESPACE) && m_Flags.Check(FEDITLINE_EDITORMODE) && (m_Str.size() >= RealLeftPos) && (OutStr.size() < EditLength) && GetEditor()->IsLastLine(this))
		{
			OutStr.push_back(L'\x25a1');
		}
	}

	SetColor(GetNormalColor());

	if (TabSelStart==-1)
	{
		if (m_Flags.Check(FEDITLINE_CLEARFLAG))
		{
			SetColor(GetUnchangedColor());

			if (!Mask.empty())
				RemoveTrailingSpaces(OutStr);

			Global->FS << fmt::LeftAlign() << OutStr;
			SetColor(GetNormalColor());
			size_t BlankLength=EditLength-OutStr.size();

			if (BlankLength > 0)
			{
				Global->FS << fmt::MinWidth(BlankLength)<<L"";
			}
		}
		else
		{
			Global->FS << fmt::LeftAlign()<<fmt::ExactWidth(EditLength)<<OutStr;
		}
	}
	else
	{
		if ((TabSelStart-=LeftPos)<0)
			TabSelStart=0;

		int AllString=(TabSelEnd==-1);

		if (AllString)
			TabSelEnd=static_cast<int>(EditLength);
		else if ((TabSelEnd-=LeftPos)<0)
			TabSelEnd=0;

		OutStr.append(EditLength - OutStr.size(), L' ');

		/* $ 24.08.2000 SVS
		   ! � DropDowList`� ��������� �� ������ ��������� - �� ��� ������� �����
		     ���� ���� ������ ������
		*/
		if (TabSelStart>=static_cast<int>(EditLength) /*|| !AllString && TabSelStart>=StrSize*/ ||
		        TabSelEnd<TabSelStart)
		{
			if (m_Flags.Check(FEDITLINE_DROPDOWNBOX))
			{
				SetColor(GetSelectedColor());
				Global->FS << fmt::MinWidth(m_X2-m_X1+1)<<OutStr;
			}
			else
				Text(OutStr);
		}
		else
		{
			Global->FS << fmt::MaxWidth(TabSelStart)<<OutStr;
			SetColor(GetSelectedColor());

			if (!m_Flags.Check(FEDITLINE_DROPDOWNBOX))
			{
				Global->FS << fmt::MaxWidth(TabSelEnd-TabSelStart) << OutStr.data() + TabSelStart;

				if (TabSelEnd<static_cast<int>(EditLength))
				{
					//SetColor(Flags.Check(FEDITLINE_CLEARFLAG) ? SelColor:Color);
					SetColor(GetNormalColor());
					Text(OutStr.data()+TabSelEnd);
				}
			}
			else
			{
				Global->FS << fmt::MinWidth(m_X2-m_X1+1)<<OutStr;
			}
		}
	}

	/* $ 26.07.2000 tran
	   ��� ����-���� ����� ��� �� ����� */
	if (!m_Flags.Check(FEDITLINE_DROPDOWNBOX))
		ApplyColor(GetSelectedColor(), XPos, FocusedLeftPos);
}

int Edit::RecurseProcessKey(int Key)
{
	Recurse++;
	int RetCode=ProcessKey(Manager::Key(Key));
	Recurse--;
	return RetCode;
}

// ������� ������� ������ ��������� - �� ��������� �� ���� ������
int Edit::ProcessInsPath(int Key,int PrevSelStart,int PrevSelEnd)
{
	int RetCode=FALSE;
	string strPathName;

	if (Key>=KEY_RCTRL0 && Key<=KEY_RCTRL9) // ��������?
	{
		if (Shortcuts().Get(Key-KEY_RCTRL0,&strPathName,nullptr,nullptr,nullptr))
			RetCode=TRUE;
	}
	else // ����/�����?
	{
		RetCode=_MakePath1(Key,strPathName,L"");
	}

	// ���� ���-���� ����������, ������ ��� � ������� (PathName)
	if (RetCode)
	{
		if (m_Flags.Check(FEDITLINE_CLEARFLAG))
		{
			LeftPos=0;
			SetString(L"");
		}

		if (PrevSelStart!=-1)
		{
			m_SelStart=PrevSelStart;
			m_SelEnd=PrevSelEnd;
		}

		if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
			DeleteBlock();

		InsertString(strPathName);
		m_Flags.Clear(FEDITLINE_CLEARFLAG);
	}

	return RetCode;
}

__int64 Edit::VMProcess(int OpCode,void *vParam,__int64 iParam)
{
	switch (OpCode)
	{
		case MCODE_C_EMPTY:
			return !GetLength();
		case MCODE_C_SELECTED:
			return m_SelStart != -1 && m_SelStart < m_SelEnd;
		case MCODE_C_EOF:
			return m_CurPos >= m_Str.size();
		case MCODE_C_BOF:
			return !m_CurPos;
		case MCODE_V_ITEMCOUNT:
			return m_Str.size();
		case MCODE_V_CURPOS:
			return GetLineCursorPos()+1;
		case MCODE_F_EDITOR_SEL:
		{
			int Action=(int)((intptr_t)vParam);
			if (Action) SetClearFlag(0);

			switch (Action)
			{
				case 0:  // Get Param
				{
					switch (iParam)
					{
						case 0:  // return FirstLine
						case 2:  // return LastLine
							return IsSelection()?1:0;
						case 1:  // return FirstPos
							return IsSelection()?m_SelStart+1:0;
						case 3:  // return LastPos
							return IsSelection()?m_SelEnd:0;
						case 4: // return block type (0=nothing 1=stream, 2=column)
							return IsSelection()?1:0;
					}

					break;
				}
				case 1:  // Set Pos
				{
					if (IsSelection())
					{
						switch (iParam)
						{
							case 0: // begin block (FirstLine & FirstPos)
							case 1: // end block (LastLine & LastPos)
							{
								SetTabCurPos(iParam?m_SelEnd:m_SelStart);
								Show();
								return 1;
							}
						}
					}

					break;
				}
				case 2: // Set Stream Selection Edge
				case 3: // Set Column Selection Edge
				{
					switch (iParam)
					{
						case 0:  // selection start
						{
							SetMacroSelectionStart(GetTabCurPos());
							return 1;
						}
						case 1:  // selection finish
						{
							if (GetMacroSelectionStart() != -1)
							{
								if (GetMacroSelectionStart() != GetTabCurPos())
									Select(GetMacroSelectionStart(),GetTabCurPos());
								else
									Select(-1,0);

								Show();
								SetMacroSelectionStart(-1);
								return 1;
							}

							return 0;
						}
					}

					break;
				}
				case 4: // UnMark sel block
				{
					Select(-1,0);
					SetMacroSelectionStart(-1);
					Show();
					return 1;
				}
			}

			break;
		}
	}

	return 0;
}

int Edit::ProcessKey(const Manager::Key& Key)
{
	int LocalKey=Key.FarKey;
	auto Mask = GetInputMask();
	switch (LocalKey)
	{
		case KEY_ADD:
			LocalKey=L'+';
			break;
		case KEY_SUBTRACT:
			LocalKey=L'-';
			break;
		case KEY_MULTIPLY:
			LocalKey=L'*';
			break;
		case KEY_DIVIDE:
			LocalKey=L'/';
			break;
		case KEY_DECIMAL:
			LocalKey=L'.';
			break;
		case KEY_CTRLC:
		case KEY_RCTRLC:
			LocalKey=KEY_CTRLINS;
			break;
		case KEY_CTRLV:
		case KEY_RCTRLV:
			LocalKey=KEY_SHIFTINS;
			break;
		case KEY_CTRLX:
		case KEY_RCTRLX:
			LocalKey=KEY_SHIFTDEL;
			break;
	}

	int PrevSelStart=-1,PrevSelEnd=0;

	if (!m_Flags.Check(FEDITLINE_DROPDOWNBOX) && (LocalKey==KEY_CTRLL || LocalKey==KEY_RCTRLL))
	{
		m_Flags.Swap(FEDITLINE_READONLY);
	}

	if ((((LocalKey==KEY_BS || LocalKey==KEY_DEL || LocalKey==KEY_NUMDEL) && m_Flags.Check(FEDITLINE_DELREMOVESBLOCKS)) || LocalKey==KEY_CTRLD || LocalKey==KEY_RCTRLD) &&
	        !m_Flags.Check(FEDITLINE_EDITORMODE) && m_SelStart!=-1 && m_SelStart<m_SelEnd)
	{
		DeleteBlock();
		Show();
		return TRUE;
	}

	int _Macro_IsExecuting=Global->CtrlObject->Macro.IsExecuting();

	if (!IntKeyState.ShiftPressed && (!_Macro_IsExecuting || (IsNavKey(LocalKey) && _Macro_IsExecuting)) &&
	        !IsShiftKey(LocalKey) && !Recurse &&
	        LocalKey!=KEY_SHIFT && LocalKey!=KEY_CTRL && LocalKey!=KEY_ALT &&
	        LocalKey!=KEY_RCTRL && LocalKey!=KEY_RALT && LocalKey!=KEY_NONE &&
	        LocalKey!=KEY_INS &&
	        LocalKey!=KEY_KILLFOCUS && LocalKey != KEY_GOTFOCUS &&
	        ((LocalKey&(~KEY_CTRLMASK)) != KEY_LWIN && (LocalKey&(~KEY_CTRLMASK)) != KEY_RWIN && (LocalKey&(~KEY_CTRLMASK)) != KEY_APPS)
	   )
	{
		m_Flags.Clear(FEDITLINE_MARKINGBLOCK);

		if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS) && !(LocalKey==KEY_CTRLINS || LocalKey==KEY_RCTRLINS || LocalKey==KEY_CTRLNUMPAD0 || LocalKey==KEY_RCTRLNUMPAD0) &&
		        !(LocalKey==KEY_SHIFTDEL||LocalKey==KEY_SHIFTNUMDEL||LocalKey==KEY_SHIFTDECIMAL) && !m_Flags.Check(FEDITLINE_EDITORMODE) &&
		        (LocalKey != KEY_CTRLQ && LocalKey != KEY_RCTRLQ) &&
		        !(LocalKey == KEY_SHIFTINS || LocalKey == KEY_SHIFTNUMPAD0))
		{
			if (m_SelStart != -1 || m_SelEnd )
			{
				PrevSelStart=m_SelStart;
				PrevSelEnd=m_SelEnd;
				Select(-1,0);
				Show();
			}
		}
	}

	if (((Global->Opt->Dialogs.EULBsClear && LocalKey==KEY_BS) || LocalKey==KEY_DEL || LocalKey==KEY_NUMDEL) && m_Flags.Check(FEDITLINE_CLEARFLAG) && m_CurPos >= m_Str.size())
		LocalKey=KEY_CTRLY;

	if ((LocalKey == KEY_SHIFTDEL || LocalKey == KEY_SHIFTNUMDEL || LocalKey == KEY_SHIFTDECIMAL) && m_Flags.Check(FEDITLINE_CLEARFLAG) && m_CurPos >= m_Str.size() && m_SelStart == -1)
	{
		m_SelStart=0;
		m_SelEnd = m_Str.size();
	}

	if (m_Flags.Check(FEDITLINE_CLEARFLAG) && ((LocalKey <= 0xFFFF && LocalKey!=KEY_BS) || LocalKey==KEY_CTRLBRACKET || LocalKey==KEY_RCTRLBRACKET ||
	        LocalKey==KEY_CTRLBACKBRACKET || LocalKey==KEY_RCTRLBACKBRACKET || LocalKey==KEY_CTRLSHIFTBRACKET || LocalKey==KEY_RCTRLSHIFTBRACKET ||
	        LocalKey==KEY_CTRLSHIFTBACKBRACKET || LocalKey==KEY_RCTRLSHIFTBACKBRACKET || LocalKey==KEY_SHIFTENTER || LocalKey==KEY_SHIFTNUMENTER))
	{
		LeftPos=0;
		DisableCallback();
		SetString(L""); // mantis#0001722
		RevertCallback();
		Show();
	}

	// ����� - ����� ������� ������� �����/������
	if (ProcessInsPath(LocalKey,PrevSelStart,PrevSelEnd))
	{
		Show();
		return TRUE;
	}

	if (m_Flags.Check(FEDITLINE_CLEARFLAG) && LocalKey!=KEY_NONE && LocalKey!=KEY_IDLE && LocalKey!=KEY_SHIFTINS && LocalKey!=KEY_SHIFTNUMPAD0 && (LocalKey!=KEY_CTRLINS && LocalKey!=KEY_RCTRLINS) &&
	        ((unsigned int)LocalKey<KEY_F1 || (unsigned int)LocalKey>KEY_F12) && LocalKey!=KEY_ALT && LocalKey!=KEY_SHIFT &&
	        LocalKey!=KEY_CTRL && LocalKey!=KEY_RALT && LocalKey!=KEY_RCTRL &&
	        !((LocalKey>=KEY_ALT_BASE && LocalKey <= KEY_ALT_BASE+0xFFFF) || (LocalKey>=KEY_RALT_BASE && LocalKey <= KEY_RALT_BASE+0xFFFF)) && // ???? 256 ???
	        !(((unsigned int)LocalKey>=KEY_MACRO_BASE && (unsigned int)LocalKey<=KEY_MACRO_ENDBASE) || ((unsigned int)LocalKey>=KEY_OP_BASE && (unsigned int)LocalKey <=KEY_OP_ENDBASE)) &&
	        (LocalKey!=KEY_CTRLQ && LocalKey!=KEY_RCTRLQ))
	{
		m_Flags.Clear(FEDITLINE_CLEARFLAG);
	}

	switch (LocalKey)
	{
		case KEY_CTRLA: case KEY_RCTRLA:
			{
				Select(0, GetLength());
				Show();
			}
			break;
		case KEY_SHIFTLEFT: case KEY_SHIFTNUMPAD4:
		{
			if (m_CurPos>0)
			{
				AdjustPersistentMark();

				RecurseProcessKey(KEY_LEFT);

				if (!m_Flags.Check(FEDITLINE_MARKINGBLOCK))
				{
					Select(-1,0);
					m_Flags.Set(FEDITLINE_MARKINGBLOCK);
				}

				if (m_SelStart!=-1 && m_SelStart<=m_CurPos)
					Select(m_SelStart,m_CurPos);
				else
				{
					int EndPos=m_CurPos+1;
					int NewStartPos=m_CurPos;

					if (EndPos>m_Str.size())
						EndPos = m_Str.size();

					if (NewStartPos>m_Str.size())
						NewStartPos = m_Str.size();

					AddSelect(NewStartPos,EndPos);
				}

				Show();
			}

			return TRUE;
		}
		case KEY_SHIFTRIGHT: case KEY_SHIFTNUMPAD6:
		{
			AdjustPersistentMark();

			if (!m_Flags.Check(FEDITLINE_MARKINGBLOCK))
			{
				Select(-1,0);
				m_Flags.Set(FEDITLINE_MARKINGBLOCK);
			}

			if ((m_SelStart!=-1 && m_SelEnd==-1) || m_SelEnd>m_CurPos)
			{
				if (m_CurPos+1==m_SelEnd)
					Select(-1,0);
				else
					Select(m_CurPos+1,m_SelEnd);
			}
			else
				AddSelect(m_CurPos,m_CurPos+1);

			RecurseProcessKey(KEY_RIGHT);
			return TRUE;
		}
		case KEY_CTRLSHIFTLEFT:  case KEY_CTRLSHIFTNUMPAD4:
		case KEY_RCTRLSHIFTLEFT: case KEY_RCTRLSHIFTNUMPAD4:
		{
			if (m_CurPos>m_Str.size())
			{
				SetPrevCurPos(m_CurPos);
				m_CurPos = m_Str.size();
			}

			if (m_CurPos>0)
				RecurseProcessKey(KEY_SHIFTLEFT);

			while (m_CurPos>0 && !(!IsWordDiv(WordDiv(), m_Str[m_CurPos]) &&
			                     IsWordDiv(WordDiv(),m_Str[m_CurPos-1]) && !IsSpace(m_Str[m_CurPos])))
			{
				if (!IsSpace(m_Str[m_CurPos]) && (IsSpace(m_Str[m_CurPos-1]) ||
				                              IsWordDiv(WordDiv(), m_Str[m_CurPos-1])))
					break;

				RecurseProcessKey(KEY_SHIFTLEFT);
			}

			Show();
			return TRUE;
		}
		case KEY_CTRLSHIFTRIGHT:  case KEY_CTRLSHIFTNUMPAD6:
		case KEY_RCTRLSHIFTRIGHT: case KEY_RCTRLSHIFTNUMPAD6:
		{
			if (m_CurPos >= m_Str.size())
				return FALSE;

			RecurseProcessKey(KEY_SHIFTRIGHT);

			while (m_CurPos < m_Str.size() && !(IsWordDiv(WordDiv(), m_Str[m_CurPos]) &&
			                           !IsWordDiv(WordDiv(), m_Str[m_CurPos-1])))
			{
				if (!IsSpace(m_Str[m_CurPos]) && (IsSpace(m_Str[m_CurPos-1]) || IsWordDiv(WordDiv(), m_Str[m_CurPos-1])))
					break;

				RecurseProcessKey(KEY_SHIFTRIGHT);

				if (GetMaxLength()!=-1 && m_CurPos==GetMaxLength()-1)
					break;
			}

			Show();
			return TRUE;
		}
		case KEY_SHIFTHOME:  case KEY_SHIFTNUMPAD7:
		{
			Lock();

			while (m_CurPos>0)
				RecurseProcessKey(KEY_SHIFTLEFT);

			Unlock();
			Show();
			return TRUE;
		}
		case KEY_SHIFTEND:  case KEY_SHIFTNUMPAD1:
		{
			Lock();
			int Len;

			if (!Mask.empty())
			{
				string ShortStr(ALL_CONST_RANGE(m_Str));
				Len = static_cast<int>(RemoveTrailingSpaces(ShortStr).size());
			}
			else
				Len = m_Str.size();

			int LastCurPos=m_CurPos;

			while (m_CurPos<Len/*StrSize*/)
			{
				RecurseProcessKey(KEY_SHIFTRIGHT);

				if (LastCurPos==m_CurPos)break;

				LastCurPos=m_CurPos;
			}

			Unlock();
			Show();
			return TRUE;
		}
		case KEY_BS:
		{
			if (m_CurPos<=0)
				return FALSE;

			SetPrevCurPos(m_CurPos);
			do {
				--m_CurPos;
			}
			while (!Mask.empty() && m_CurPos > 0 && !CheckCharMask(Mask[m_CurPos]));

			if (m_CurPos<=LeftPos)
			{
				LeftPos-=15;

				if (LeftPos<0)
					LeftPos=0;
			}

			if (!RecurseProcessKey(KEY_DEL))
				Show();

			return TRUE;
		}
		case KEY_CTRLSHIFTBS:
		case KEY_RCTRLSHIFTBS:
		{
			DisableCallback();

			// BUGBUG
			for (int i=m_CurPos; i>=0; i--)
			{
				RecurseProcessKey(KEY_BS);
			}
			RevertCallback();
			Changed(true);
			Show();
			return TRUE;
		}
		case KEY_CTRLBS:
		case KEY_RCTRLBS:
		{
			if (m_CurPos > m_Str.size())
			{
				SetPrevCurPos(m_CurPos);
				m_CurPos = m_Str.size();
			}

			Lock();

			DisableCallback();

			// BUGBUG
			for (;;)
			{
				int StopDelete=FALSE;

				if (m_CurPos>1 && IsSpace(m_Str[m_CurPos-1])!=IsSpace(m_Str[m_CurPos-2]))
					StopDelete=TRUE;

				RecurseProcessKey(KEY_BS);

				if (!m_CurPos || StopDelete)
					break;

				if (IsWordDiv(WordDiv(),m_Str[m_CurPos-1]))
					break;
			}

			Unlock();
			RevertCallback();
			Changed(true);
			Show();
			return TRUE;
		}
		case KEY_CTRLQ:
		case KEY_RCTRLQ:
		{
			Lock();

			if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS) && (m_SelStart != -1 || m_Flags.Check(FEDITLINE_CLEARFLAG)))
				RecurseProcessKey(KEY_DEL);

			ProcessCtrlQ();
			Unlock();
			Show();
			return TRUE;
		}
		case KEY_OP_SELWORD:
		{
			int OldCurPos=m_CurPos;
			PrevSelStart=m_SelStart;
			PrevSelEnd=m_SelEnd;
#if defined(MOUSEKEY)

			if (m_CurPos >= m_SelStart && m_CurPos <= m_SelEnd)
			{ // �������� ��� ������ ��� ��������� ������� �����
				Select(0,m_StrSize);
			}
			else
#endif
			{
				int SStart, SEnd;

				if (CalcWordFromString(m_Str.data(), m_CurPos, &SStart, &SEnd, WordDiv()))
					Select(SStart, SEnd + (SEnd < m_Str.size()? 1 : 0));
			}

			m_CurPos=OldCurPos; // ���������� �������
			Show();
			return TRUE;
		}
		case KEY_OP_PLAINTEXT:
		{
			if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
			{
				if (m_SelStart != -1 || m_Flags.Check(FEDITLINE_CLEARFLAG)) // BugZ#1053 - ���������� � $Text
					RecurseProcessKey(KEY_DEL);
			}

			ProcessInsPlainText(Global->CtrlObject->Macro.GetStringToPrint());

			Show();
			return TRUE;
		}
		case KEY_CTRLT:
		case KEY_CTRLDEL:
		case KEY_CTRLNUMDEL:
		case KEY_CTRLDECIMAL:
		case KEY_RCTRLT:
		case KEY_RCTRLDEL:
		case KEY_RCTRLNUMDEL:
		case KEY_RCTRLDECIMAL:
		{
			if (m_CurPos >= m_Str.size())
				return FALSE;

			Lock();
			DisableCallback();
			if (!Mask.empty())
			{
				int MaskLen = static_cast<int>(Mask.size());
				int ptr=m_CurPos;

				while (ptr<MaskLen)
				{
					ptr++;

					if (!CheckCharMask(Mask[ptr]) ||
					        (IsSpace(m_Str[ptr]) && !IsSpace(m_Str[ptr+1])) ||
					        (IsWordDiv(WordDiv(), m_Str[ptr])))
						break;
				}

				// BUGBUG
				for (int i=0; i<ptr-m_CurPos; i++)
					RecurseProcessKey(KEY_DEL);
			}
			else
			{
				for (;;)
				{
					int StopDelete=FALSE;

					if (m_CurPos<m_Str.size() - 1 && IsSpace(m_Str[m_CurPos]) && !IsSpace(m_Str[m_CurPos + 1]))
						StopDelete=TRUE;

					RecurseProcessKey(KEY_DEL);

					if (m_CurPos >= m_Str.size() || StopDelete)
						break;

					if (IsWordDiv(WordDiv(), m_Str[m_CurPos]))
						break;
				}
			}

			Unlock();
			RevertCallback();
			Changed(true);
			Show();
			return TRUE;
		}
		case KEY_CTRLY:
		case KEY_RCTRLY:
		{
			if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
				return TRUE;

			SetPrevCurPos(m_CurPos);
			LeftPos=m_CurPos=0;
			clear_and_shrink(m_Str);
			Select(-1,0);
			Changed();
			Show();
			return TRUE;
		}
		case KEY_CTRLK:
		case KEY_RCTRLK:
		{
			if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
				return TRUE;

			if (m_CurPos >= m_Str.size())
				return FALSE;

			if (!m_Flags.Check(FEDITLINE_EDITBEYONDEND))
			{
				if (m_CurPos<m_SelEnd)
					m_SelEnd=m_CurPos;

				if (m_SelEnd<m_SelStart && m_SelEnd!=-1)
				{
					m_SelEnd=0;
					m_SelStart=-1;
				}
			}

			m_Str.resize(m_CurPos);
			Changed();
			Show();
			return TRUE;
		}
		case KEY_HOME:        case KEY_NUMPAD7:
		case KEY_CTRLHOME:    case KEY_CTRLNUMPAD7:
		case KEY_RCTRLHOME:   case KEY_RCTRLNUMPAD7:
		{
			SetPrevCurPos(m_CurPos);
			m_CurPos=0;
			Show();
			return TRUE;
		}
		case KEY_END:           case KEY_NUMPAD1:
		case KEY_CTRLEND:       case KEY_CTRLNUMPAD1:
		case KEY_RCTRLEND:      case KEY_RCTRLNUMPAD1:
		case KEY_CTRLSHIFTEND:  case KEY_CTRLSHIFTNUMPAD1:
		case KEY_RCTRLSHIFTEND: case KEY_RCTRLSHIFTNUMPAD1:
		{
			SetPrevCurPos(m_CurPos);

			if (!Mask.empty())
			{
				string ShortStr(ALL_CONST_RANGE(m_Str));
				m_CurPos = static_cast<int>(RemoveTrailingSpaces(ShortStr).size());
			}
			else
				m_CurPos = m_Str.size();

			Show();
			return TRUE;
		}
		case KEY_LEFT:        case KEY_NUMPAD4:        case KEY_MSWHEEL_LEFT:
		case KEY_CTRLS:       case KEY_RCTRLS:
		{
			if (m_CurPos>0)
			{
				SetPrevCurPos(m_CurPos);
				m_CurPos--;
				Show();
			}

			return TRUE;
		}
		case KEY_RIGHT:       case KEY_NUMPAD6:        case KEY_MSWHEEL_RIGHT:
		case KEY_CTRLD:       case KEY_RCTRLD:
		{
			SetPrevCurPos(m_CurPos);

			if (!Mask.empty())
			{
				string ShortStr(ALL_CONST_RANGE(m_Str));
				int Len = static_cast<int>(RemoveTrailingSpaces(ShortStr).size());

				if (Len>m_CurPos)
					m_CurPos++;
			}
			else
				m_CurPos++;

			Show();
			return TRUE;
		}
		case KEY_INS:         case KEY_NUMPAD0:
		{
			m_Flags.Swap(FEDITLINE_OVERTYPE);
			Show();
			return TRUE;
		}
		case KEY_NUMDEL:
		case KEY_DEL:
		{
			if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
				return TRUE;

			if (m_CurPos >= m_Str.size())
				return FALSE;

			if (m_SelStart!=-1)
			{
				if (m_SelEnd!=-1 && m_CurPos<m_SelEnd)
					m_SelEnd--;

				if (m_CurPos<m_SelStart)
					m_SelStart--;

				if (m_SelEnd!=-1 && m_SelEnd<=m_SelStart)
				{
					m_SelStart=-1;
					m_SelEnd=0;
				}
			}

			if (!Mask.empty())
			{
				const size_t MaskLen = Mask.size();
				size_t j = m_CurPos;
				for (size_t i = m_CurPos; i < MaskLen; ++i)
				{
					if (i+1 < MaskLen && CheckCharMask(Mask[i+1]))
					{
						while (j < MaskLen && !CheckCharMask(Mask[j]))
							j++;

						m_Str[j]=m_Str[i+1];
						j++;
					}
				}

				m_Str[j]=L' ';
			}
			else
			{
				m_Str.erase(m_Str.begin() + m_CurPos);
			}

			Changed(true);
			Show();
			return TRUE;
		}
		case KEY_CTRLLEFT:  case KEY_CTRLNUMPAD4:
		case KEY_RCTRLLEFT: case KEY_RCTRLNUMPAD4:
		{
			SetPrevCurPos(m_CurPos);

			if (m_CurPos > m_Str.size())
				m_CurPos = m_Str.size();

			if (m_CurPos>0)
				m_CurPos--;

			while (m_CurPos>0 && !(!IsWordDiv(WordDiv(), m_Str[m_CurPos]) &&
			                     IsWordDiv(WordDiv(), m_Str[m_CurPos-1]) && !IsSpace(m_Str[m_CurPos])))
			{
				if (!IsSpace(m_Str[m_CurPos]) && IsSpace(m_Str[m_CurPos-1]))
					break;

				m_CurPos--;
			}

			Show();
			return TRUE;
		}
		case KEY_CTRLRIGHT:   case KEY_CTRLNUMPAD6:
		case KEY_RCTRLRIGHT:  case KEY_RCTRLNUMPAD6:
		{
			if (m_CurPos >= m_Str.size())
				return FALSE;

			SetPrevCurPos(m_CurPos);
			int Len;

			if (!Mask.empty())
			{
				string ShortStr(ALL_CONST_RANGE(m_Str));
				Len = static_cast<int>(RemoveTrailingSpaces(ShortStr).size());

				if (Len>m_CurPos)
					m_CurPos++;
			}
			else
			{
				Len = m_Str.size();
				m_CurPos++;
			}

			while (m_CurPos<Len/*StrSize*/ && !(IsWordDiv(WordDiv(),m_Str[m_CurPos]) &&
			                                  !IsWordDiv(WordDiv(), m_Str[m_CurPos-1])))
			{
				if (!IsSpace(m_Str[m_CurPos]) && IsSpace(m_Str[m_CurPos-1]))
					break;

				m_CurPos++;
			}

			Show();
			return TRUE;
		}
		case KEY_SHIFTNUMDEL:
		case KEY_SHIFTDECIMAL:
		case KEY_SHIFTDEL:
		{
			if (m_SelStart==-1 || m_SelStart>=m_SelEnd)
				return FALSE;

			RecurseProcessKey(KEY_CTRLINS);
			DeleteBlock();
			Show();
			return TRUE;
		}
		case KEY_CTRLINS:     case KEY_CTRLNUMPAD0:
		case KEY_RCTRLINS:    case KEY_RCTRLNUMPAD0:
		{
			if (!m_Flags.Check(FEDITLINE_PASSWORDMODE))
			{
				if (m_SelStart==-1 || m_SelStart>=m_SelEnd)
				{
					if (!Mask.empty())
					{
						string ShortStr(ALL_CONST_RANGE(m_Str));
						SetClipboard(RemoveTrailingSpaces(ShortStr));
					}
					else
					{
						SetClipboard(m_Str.data());
					}
				}
				else if (m_SelEnd <= m_Str.size()) // TODO: ���� � ������ ������� �������� "StrSize &&", �� �������� ��� "Ctrl-Ins � ������ ������ ������� ��������"
				{
					int Ch=m_Str[m_SelEnd];
					m_Str[m_SelEnd]=0;
					SetClipboard(m_Str.data() + m_SelStart);
					m_Str[m_SelEnd]=Ch;
				}
			}

			return TRUE;
		}
		case KEY_SHIFTINS:    case KEY_SHIFTNUMPAD0:
		{
			string ClipText;

			if (GetMaxLength()==-1)
			{
				if (!GetClipboard(ClipText))
					return TRUE;
			}
			else
			{
				if (!GetClipboardEx(GetMaxLength(), ClipText))
					return TRUE;
			}

			if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
			{
				DisableCallback();
				DeleteBlock();
				RevertCallback();
			}

			for (size_t i=0; i < ClipText.size(); ++i)
			{
				if (IsEol(ClipText[i]))
				{
					if (i + 1 < ClipText.size() && IsEol(ClipText[i + 1]))
						ClipText.erase(i, 1);

					if (i+1 == ClipText.size())
						ClipText.resize(i);
					else
						ClipText[i] = L' ';
				}
			}

			if (m_Flags.Check(FEDITLINE_CLEARFLAG))
			{
				LeftPos=0;
				m_Flags.Clear(FEDITLINE_CLEARFLAG);
				SetString(ClipText.data());
			}
			else
			{
				InsertString(ClipText.data());
			}

			Show();
			return TRUE;
		}
		case KEY_SHIFTTAB:
		{
			SetPrevCurPos(m_CurPos);
			int Pos = GetLineCursorPos();
			SetLineCursorPos(Pos-((Pos-1) % GetTabSize()+1));

			if (GetLineCursorPos()<0) SetLineCursorPos(0); //CursorPos=0,TabSize=1 case

			SetTabCurPos(GetLineCursorPos());
			Show();
			return TRUE;
		}
		case KEY_SHIFTSPACE:
			LocalKey = KEY_SPACE;
		default:
		{
//      _D(SysLog(L"Key=0x%08X",LocalKey));
			if (LocalKey==KEY_NONE || LocalKey==KEY_IDLE || LocalKey==KEY_ENTER || LocalKey==KEY_NUMENTER || LocalKey>=65536)
				break;

			if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
			{
				if (PrevSelStart!=-1)
				{
					m_SelStart=PrevSelStart;
					m_SelEnd=PrevSelEnd;
				}
				DisableCallback();
				DeleteBlock();
				RevertCallback();
			}

			if (InsertKey(LocalKey))
			{
				int CurWindowType = Global->WindowManager->GetCurrentWindow()->GetType();
				if (CurWindowType == windowtype_dialog || CurWindowType == windowtype_panels)
				{
					Show();
				}
			}
			return TRUE;
		}
	}

	return FALSE;
}

// ��������� Ctrl-Q
int Edit::ProcessCtrlQ()
{
	INPUT_RECORD rec;
	for (;;)
	{
		DWORD Key=GetInputRecord(&rec);

		if (Key!=KEY_NONE && Key!=KEY_IDLE && rec.Event.KeyEvent.uChar.AsciiChar)
			break;

		if (Key==KEY_CONSOLE_BUFFER_RESIZE)
		{
//      int Dis=EditOutDisabled;
//      EditOutDisabled=0;
			Show();
//      EditOutDisabled=Dis;
		}
	}

	/*
	  EditOutDisabled++;
	  if (!Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
	  {
	    DeleteBlock();
	  }
	  else
	    Flags.Clear(FEDITLINE_CLEARFLAG);
	  EditOutDisabled--;
	*/
	return InsertKey(rec.Event.KeyEvent.uChar.AsciiChar);
}

int Edit::ProcessInsPlainText(const wchar_t *str)
{
	if (*str)
	{
		InsertString(str);
		return TRUE;
	}

	return FALSE;
}

int Edit::InsertKey(int Key)
{
	bool changed=false;

	if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
		return TRUE;

	if (Key==KEY_TAB && m_Flags.Check(FEDITLINE_OVERTYPE))
	{
		SetPrevCurPos(m_CurPos);
		int Pos = GetLineCursorPos();
		SetLineCursorPos(static_cast<int>(Pos + (GetTabSize() - (Pos % GetTabSize()))));
		SetTabCurPos(GetLineCursorPos());
		return TRUE;
	}

	auto Mask = GetInputMask();
	if (!Mask.empty())
	{
		int MaskLen = static_cast<int>(Mask.size());

		if (m_CurPos<MaskLen)
		{
			if (KeyMatchedMask(Key, Mask))
			{
				if (!m_Flags.Check(FEDITLINE_OVERTYPE))
				{
					int i=MaskLen-1;

					while (!CheckCharMask(Mask[i]) && i>m_CurPos)
						i--;

					for (int j=i; i>m_CurPos; i--)
					{
						if (CheckCharMask(Mask[i]))
						{
							while (!CheckCharMask(Mask[j-1]))
							{
								if (j<=m_CurPos)
									break;

								j--;
							}

							m_Str[i]=m_Str[j-1];
							j--;
						}
					}
				}

				SetPrevCurPos(m_CurPos);
				m_Str[m_CurPos++]=Key;
				changed=true;
			}
			else
			{
				// ����� ������� ��� "����� ������ �� �����", �������� ��� SetAttr - ����� '.'
				;// char *Ptr=strchr(Mask+CurPos,Key);
			}
		}
		else if (m_CurPos<m_Str.size())
		{
			SetPrevCurPos(m_CurPos);
			m_Str[m_CurPos++]=Key;
			changed=true;
		}
	}
	else
	{
		if (GetMaxLength() == -1 || m_Str.size() + 1 <= GetMaxLength())
		{
			if (m_CurPos > m_Str.size())
			{
				m_Str.resize(m_CurPos, L' ');
			}

			if (Key==KEY_TAB && (GetTabExpandMode()==EXPAND_NEWTABS || GetTabExpandMode()==EXPAND_ALLTABS))
			{
				InsertTab();
				return TRUE;
			}

			SetPrevCurPos(m_CurPos);

			if (!m_Flags.Check(FEDITLINE_OVERTYPE) || m_CurPos >= m_Str.size())
			{
				m_Str.insert(m_Str.begin() + m_CurPos, 1, Key);

				if (m_SelStart!=-1)
				{
					if (m_SelEnd!=-1 && m_CurPos<m_SelEnd)
						m_SelEnd++;

					if (m_CurPos<m_SelStart)
						m_SelStart++;
				}
			}
			else
			{
				m_Str[m_CurPos] = Key;
			}

			++m_CurPos;
			changed=true;
		}
		else
		{
			if (m_CurPos < m_Str.size())
			{
				SetPrevCurPos(m_CurPos);
				m_Str[m_CurPos++]=Key;
				changed=true;
			}
		}
	}

	if (changed)
		Changed();

	return TRUE;
}

void Edit::GetString(string &strStr) const
{
	strStr.assign(ALL_CONST_RANGE(m_Str));
}

const wchar_t* Edit::GetStringAddr() const
{
	return m_Str.data();
}

void Edit::SetHiString(const string& Str)
{
	if (m_Flags.Check(FEDITLINE_READONLY))
		return;

	auto NewStr = HiText2Str(Str);
	Select(-1,0);
	SetBinaryString(NewStr.data(), NewStr.size());
}

void Edit::SetString(const wchar_t *Str, int Length)
{
	if (m_Flags.Check(FEDITLINE_READONLY))
		return;

	Select(-1,0);
	SetBinaryString(Str,Length==-1? StrLength(Str) : Length);
}

void Edit::SetEOL(const wchar_t *EOL)
{
	EndType=EOL_NONE;

	if (EOL && *EOL)
	{
		if (EOL[0]==L'\r')
			if (EOL[1]==L'\n')
				EndType=EOL_CRLF;
			else if (EOL[1]==L'\r' && EOL[2]==L'\n')
				EndType=EOL_CRCRLF;
			else
				EndType=EOL_CR;
		else if (EOL[0]==L'\n')
			EndType=EOL_LF;
	}
}

const wchar_t *Edit::GetEOL() const
{
	return EOL_TYPE_CHARS[EndType];
}

/* $ 25.07.2000 tran
   ����������:
   � ���� ������ DropDownBox �� ��������������
   ��� �� ���������� ������ �� SetString � �� ������ Editor
   � Dialog �� ����� �� ���������� */
void Edit::SetBinaryString(const wchar_t *Str, size_t Length)
{
	if (m_Flags.Check(FEDITLINE_READONLY))
		return;

	// ��������� ������������ �������, ���� ��������� GetMaxLength()
	if (GetMaxLength() != -1 && Length > static_cast<size_t>(GetMaxLength()))
	{
		Length=GetMaxLength(); // ??
	}

	if (Length && !m_Flags.Check(FEDITLINE_PARENT_SINGLELINE))
	{
		if (Str[Length-1]==L'\r')
		{
			EndType=EOL_CR;
			Length--;
		}
		else
		{
			if (Str[Length-1]==L'\n')
			{
				Length--;

				if (Length && Str[Length-1]==L'\r')
				{
					Length--;

					if (Length && Str[Length-1]==L'\r')
					{
						Length--;
						EndType=EOL_CRCRLF;
					}
					else
						EndType=EOL_CRLF;
				}
				else
					EndType=EOL_LF;
			}
			else
				EndType=EOL_NONE;
		}
	}

	m_CurPos=0;

	auto Mask = GetInputMask();
	if (!Mask.empty())
	{
		RefreshStrByMask(TRUE);
		for (size_t i = 0, j = 0, maskLen = Mask.size(); i < maskLen && j < maskLen && j < Length;)
		{
			if (CheckCharMask(Mask[i]))
			{
				int goLoop=FALSE;

				if (KeyMatchedMask(Str[j], Mask))
					InsertKey(Str[j]);
				else
					goLoop=TRUE;

				j++;

				if (goLoop) continue;
			}
			else
			{
				SetPrevCurPos(m_CurPos);
				m_CurPos++;
			}

			i++;
		}

		/* ����� ���������� ������� (!*Str), �.�. ��� ������� ������
		   ������ �������� ����� ����� SetBinaryString("",0)
		   �.�. ����� ������� �� ���������� "�������������" ������ � ������
		*/
		RefreshStrByMask(!*Str);
	}
	else
	{
		m_Str.assign(Str, Length);

		if (GetTabExpandMode() == EXPAND_ALLTABS)
			ReplaceTabs();

		SetPrevCurPos(m_CurPos);
		m_CurPos = m_Str.size();
	}

	Changed();
}

void Edit::GetBinaryString(const wchar_t **Str,const wchar_t **EOL, size_t& Length) const
{
	*Str = m_Str.data();

	if (EOL)
		*EOL=EOL_TYPE_CHARS[EndType];

	Length = m_Str.size(); //???
}

int Edit::GetSelString(wchar_t *Str, int MaxSize)
{
	if (m_SelStart==-1 || (m_SelEnd!=-1 && m_SelEnd<=m_SelStart) ||
		m_SelStart >= m_Str.size())
	{
		*Str=0;
		return FALSE;
	}

	int CopyLength;

	if (m_SelEnd==-1)
		CopyLength=MaxSize;
	else
		CopyLength=std::min(MaxSize,m_SelEnd-m_SelStart+1);

	xwcsncpy(Str, m_Str.data() + m_SelStart, CopyLength);
	return TRUE;
}

int Edit::GetSelString(string &strStr, size_t MaxSize) const
{
	if (m_SelStart==-1 || (m_SelEnd!=-1 && m_SelEnd<=m_SelStart) || m_SelStart >= m_Str.size())
	{
		strStr.clear();
		return FALSE;
	}

	size_t CopyLength;

	if (MaxSize == string::npos)
		MaxSize = m_Str.size();

	if (m_SelEnd==-1)
		CopyLength=MaxSize;
	else
		CopyLength=std::min(MaxSize, static_cast<size_t>(m_SelEnd-m_SelStart+1));

	strStr.assign(m_Str.data() + m_SelStart, CopyLength);
	return TRUE;
}

void Edit::AppendString(const wchar_t *Str)
{
	int LastPos = m_CurPos;
	m_CurPos = GetLength();
	InsertString(Str);
	m_CurPos = LastPos;
}

void Edit::InsertString(const string& Str)
{
	if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
		return;

	if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
		DeleteBlock();

	InsertBinaryString(Str.data(), Str.size());
}

void Edit::InsertBinaryString(const wchar_t *Str, size_t Length)
{
	if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
		return;

	m_Flags.Clear(FEDITLINE_CLEARFLAG);

	auto Mask = GetInputMask();
	if (!Mask.empty())
	{
		const size_t Pos = m_CurPos;
		const size_t MaskLen = Mask.size();

		if (Pos<MaskLen)
		{
			//_SVS(SysLog(L"InsertBinaryString ==> Str='%s' (Length=%d) Mask='%s'",Str,Length,Mask+Pos));
			const size_t StrLen = (MaskLen - Pos > Length) ? Length : MaskLen - Pos;

			/* $ 15.11.2000 KM
			   ������� ����������� ��� ���������� ������ PasteFromClipboard
			   � ������ � ������
			*/
			for (size_t i = Pos, j = 0; j < StrLen + Pos;)
			{
				if (CheckCharMask(Mask[i]))
				{
					int goLoop=FALSE;

					if (j < Length && KeyMatchedMask(Str[j], Mask))
					{
						InsertKey(Str[j]);
						//_SVS(SysLog(L"InsertBinaryString ==> InsertKey(Str[%d]='%c');",j,Str[j]));
					}
					else
						goLoop=TRUE;

					j++;

					if (goLoop) continue;
				}
				else
				{
					if(Mask[j] == Str[j])
					{
						j++;
					}
					SetPrevCurPos(m_CurPos);
					m_CurPos++;
				}

				i++;
			}
		}

		RefreshStrByMask();
		//_SVS(SysLog(L"InsertBinaryString ==> this->Str='%s'",this->Str));
	}
	else
	{
		if (GetMaxLength() != -1 && m_Str.size() + Length > static_cast<size_t>(GetMaxLength()))
		{
			// ��������� ������������ �������, ���� ��������� GetMaxLength()
			if (m_Str.size() < GetMaxLength())
			{
				Length = GetMaxLength() - m_Str.size();
			}
		}

		if (GetMaxLength() == -1 || m_Str.size() + Length <= static_cast<size_t>(GetMaxLength()))
		{
			if (m_CurPos > m_Str.size())
			{
				m_Str.resize(m_CurPos, L' ');
			}

			m_Str.insert(m_CurPos, Str, Length);

			SetPrevCurPos(m_CurPos);
			m_CurPos += static_cast<int>(Length);

			if (GetTabExpandMode() == EXPAND_ALLTABS)
				ReplaceTabs();

			Changed();
		}
	}
}

int Edit::GetLength() const
{
	return m_Str.size();
}

int Edit::ProcessMouse(const MOUSE_EVENT_RECORD *MouseEvent)
{
	if (!(MouseEvent->dwButtonState & 3))
		return FALSE;

	if (MouseEvent->dwMousePosition.X<m_X1 || MouseEvent->dwMousePosition.X>m_X2 ||
	        MouseEvent->dwMousePosition.Y!=m_Y1)
		return FALSE;

	//SetClearFlag(0); // ����� ������ ��� ��������� � ������ clear-������?
	SetTabCurPos(MouseEvent->dwMousePosition.X - m_X1 + LeftPos);

	if (!m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS))
		Select(-1,0);

	if (MouseEvent->dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)
	{
		static clock_t PrevDoubleClick = 0;
		static COORD PrevPosition={};

		const auto CurrentTime = clock();

		if (static_cast<unsigned long>((CurrentTime - PrevDoubleClick) / CLOCKS_PER_SEC * 1000) <= GetDoubleClickTime() && MouseEvent->dwEventFlags != MOUSE_MOVED &&
		        PrevPosition.X == MouseEvent->dwMousePosition.X && PrevPosition.Y == MouseEvent->dwMousePosition.Y)
		{
			Select(0, m_Str.size());
			PrevDoubleClick=0;
			PrevPosition.X=0;
			PrevPosition.Y=0;
		}

		if (MouseEvent->dwEventFlags==DOUBLE_CLICK)
		{
			ProcessKey(Manager::Key(KEY_OP_SELWORD));
			PrevDoubleClick = CurrentTime;
			PrevPosition=MouseEvent->dwMousePosition;
		}
		else
		{
			PrevDoubleClick=0;
			PrevPosition.X=0;
			PrevPosition.Y=0;
		}
	}

	Show();
	return TRUE;
}

/* $ 03.08.2000 KM
   ������� ������� �������� ��-�� �������������
   ���������� ������ ����� ����.
*/
int Edit::Search(const string& Str, const string &UpperStr, const string &LowerStr, RegExp &re, RegExpMatch *pm, MatchHash* hm, string& ReplaceStr, int Position, int Case, int WholeWords, int Reverse, int Regexp, int PreserveStyle, int *SearchLength)
{
	return SearchString(m_Str.data(), m_Str.size(), Str, UpperStr, LowerStr, re, pm, hm, ReplaceStr, m_CurPos, Position, Case, WholeWords, Reverse, Regexp, PreserveStyle, SearchLength, WordDiv().data());
}

void Edit::InsertTab()
{
	if (m_Flags.Check(FEDITLINE_READONLY))
		return;

	const auto Pos = m_CurPos;
	const auto S = static_cast<int>(GetTabSize() - (RealPosToTab(Pos) % GetTabSize()));

	if (m_SelStart!=-1)
	{
		if (Pos<=m_SelStart)
		{
			m_SelStart+=S-(Pos==m_SelStart?0:1);
		}

		if (m_SelEnd!=-1 && Pos<m_SelEnd)
		{
			m_SelEnd+=S;
		}
	}

	m_Str.insert(m_Str.begin() + Pos, S, L' ');
	m_CurPos += S;
	Changed();
}

bool Edit::ReplaceTabs()
{
	if (m_Flags.Check(FEDITLINE_READONLY))
		return false;

	int Pos = 0;
	bool changed = false;

	auto TabPtr = m_Str.end();
	while ((TabPtr = std::find(m_Str.begin() + Pos, m_Str.end(), L'\t')) != m_Str.end())
	{
		changed=true;
		Pos=(int)(TabPtr - m_Str.begin());
		const auto S = static_cast<int>(GetTabSize() - (Pos % GetTabSize()));

		if (m_SelStart!=-1)
		{
			if (Pos<=m_SelStart)
			{
				m_SelStart+=S-(Pos==m_SelStart?0:1);
			}

			if (m_SelEnd!=-1 && Pos<m_SelEnd)
			{
				m_SelEnd+=S-1;
			}
		}

		*TabPtr = L' ';
		m_Str.insert(TabPtr, S - 1, L' ');

		if (m_CurPos>Pos)
			m_CurPos+=S-1;
	}

	if (changed) Changed();
	return changed;
}

int Edit::GetTabCurPos() const
{
	return RealPosToTab(m_CurPos);
}

void Edit::SetTabCurPos(int NewPos)
{
	auto Mask = GetInputMask();
	if (!Mask.empty())
	{
		string ShortStr(ALL_CONST_RANGE(m_Str));
		int Pos = static_cast<int>(RemoveTrailingSpaces(ShortStr).size());

		if (NewPos>Pos)
			NewPos=Pos;
	}

	m_CurPos=TabPosToReal(NewPos);
}

int Edit::RealPosToTab(int Pos) const
{
	return RealPosToTab(0, 0, Pos, nullptr);
}

int Edit::RealPosToTab(int PrevLength, int PrevPos, int Pos, int* CorrectPos) const
{
	// ������������� �����
	bool bCorrectPos = CorrectPos && *CorrectPos;

	if (CorrectPos)
		*CorrectPos = 0;

	// ���� � ��� ��� ���� ������������� � �������, �� ������ ��������� ����������
	if (GetTabExpandMode() == EXPAND_ALLTABS)
		return PrevLength+Pos-PrevPos;

	// ������������� �������������� ����� ���������� ���������
	int TabPos = PrevLength;

	// ���� ���������� ������� �� ������ ������, �� ����� ��� ����� ��� �
	// ��������� ����� ������ �� ����, ����� ���������� ����������
	if (PrevPos >= m_Str.size())
		TabPos += Pos-PrevPos;
	else
	{
		// �������� ���������� � ���������� �������
		int Index = PrevPos;

		// �������� �� ���� �������� �� ������� ������, ���� ��� ��� � �������� ������,
		// ���� �� ����� ������, ���� ������� ������ �� ��������� ������
		for (; Index < std::min(Pos, m_Str.size()); Index++)

			// ������������ ����
			if (m_Str[Index] == L'\t')
			{
				// ���� ���� ������������� ������ ������������� ����� � ��� �������������
				// ��� �� �����������, �� ����������� ����� �������������� ������ �� �������
				if (bCorrectPos)
				{
					++Pos;
					*CorrectPos = 1;
					bCorrectPos = false;
				}

				// ������������ ����� ���� � ������ �������� � ������� ������� � ������
				TabPos += static_cast<int>(GetTabSize() - (TabPos%GetTabSize()));
			}
		// ������������ ��� ��������� �������
			else
				TabPos++;

		// ���� ������� ��������� �� ��������� ������, �� ��� ����� ��� ����� � �� ������
		if (Pos >= m_Str.size())
			TabPos += Pos-Index;
	}

	return TabPos;
}

int Edit::TabPosToReal(int Pos) const
{
	if (GetTabExpandMode() == EXPAND_ALLTABS)
		return Pos;

	int Index = 0;

	for (int TabPos = 0; TabPos < Pos; Index++)
	{
		if (Index == m_Str.size())
		{
			Index += Pos-TabPos;
			break;
		}

		if (m_Str[Index] == L'\t')
		{
			const auto NewTabPos = static_cast<int>(TabPos + GetTabSize() - (TabPos%GetTabSize()));

			if (NewTabPos > Pos)
				break;

			TabPos = NewTabPos;
		}
		else
		{
			TabPos++;
		}
	}

	return Index;
}

void Edit::Select(int Start,int End)
{
	m_SelStart=Start;
	m_SelEnd=End;

	if (m_SelEnd<m_SelStart && m_SelEnd!=-1)
	{
		m_SelStart=-1;
		m_SelEnd=0;
	}

	if (m_SelStart==-1 && m_SelEnd==-1)
	{
		m_SelStart=-1;
		m_SelEnd=0;
	}
}

void Edit::AddSelect(int Start,int End)
{
	if (Start<m_SelStart || m_SelStart==-1)
		m_SelStart=Start;

	if (End==-1 || (End>m_SelEnd && m_SelEnd!=-1))
		m_SelEnd=End;

	if (m_SelEnd>m_Str.size())
		m_SelEnd = m_Str.size();

	if (m_SelEnd<m_SelStart && m_SelEnd!=-1)
	{
		m_SelStart=-1;
		m_SelEnd=0;
	}
}

void Edit::GetSelection(intptr_t &Start,intptr_t &End) const
{
	Start = m_SelStart;
	End = m_SelEnd;

	if (End > m_Str.size())
		End = -1;

	if (Start > m_Str.size())
		Start = m_Str.size();
}

void Edit::AdjustMarkBlock()
{
	bool mark = false;
	if (m_SelStart >= 0)
	{
		int end = m_SelEnd > m_Str.size() || m_SelEnd == -1 ? m_Str.size() : m_SelEnd;
		mark = end > m_SelStart && (m_CurPos==m_SelStart || m_CurPos==end);
	}
	m_Flags.Change(FEDITLINE_MARKINGBLOCK, mark);
}

void Edit::AdjustPersistentMark()
{
	if (m_SelStart < 0 || m_Flags.Check(FEDITLINE_MARKINGBLOCK | FEDITLINE_PARENT_EDITOR))
		return;

	bool persistent;
	if (m_Flags.Check(FEDITLINE_PARENT_SINGLELINE))
		persistent = m_Flags.Check(FEDITLINE_PERSISTENTBLOCKS); // dlgedit
	else if (!m_Flags.Check(FEDITLINE_PARENT_MULTILINE))
		persistent = Global->Opt->CmdLine.EditBlock;				// cmdline
	else
		persistent = false;

	if (!persistent)
		return;

	int end = m_SelEnd > m_Str.size() || m_SelEnd == -1? m_Str.size() : m_SelEnd;
	if (end > m_SelStart && (m_CurPos==m_SelStart || m_CurPos==end))
		m_Flags.Set(FEDITLINE_MARKINGBLOCK);
}

void Edit::GetRealSelection(intptr_t &Start,intptr_t &End) const
{
	Start=m_SelStart;
	End=m_SelEnd;
}

void Edit::DeleteBlock()
{
	if (m_Flags.Check(FEDITLINE_READONLY|FEDITLINE_DROPDOWNBOX))
		return;

	if (m_SelStart==-1 || m_SelStart>=m_SelEnd)
		return;

	SetPrevCurPos(m_CurPos);

	auto Mask = GetInputMask();
	if (!Mask.empty())
	{
		for (auto i = m_SelStart; i != m_SelEnd; ++i)
		{
			if (CheckCharMask(Mask[i]))
			{
				m_Str[i] = L' ';
			}
		}
		m_CurPos=m_SelStart;
	}
	else
	{
		const auto From = std::min(m_SelStart, m_Str.size());
		const auto To = std::min(m_SelEnd, m_Str.size());

		m_Str.erase(m_Str.begin() + From, m_Str.begin() + To);

		if (m_CurPos>From)
		{
			if (m_CurPos<To)
				m_CurPos=From;
			else
				m_CurPos-=To-From;
		}
	}

	m_SelStart=-1;
	m_SelEnd=0;
	m_Flags.Clear(FEDITLINE_MARKINGBLOCK);

	// OT: �������� �� ������������ ��������� ������ ��� �������� � �������
	if (m_Flags.Check((FEDITLINE_PARENT_SINGLELINE|FEDITLINE_PARENT_MULTILINE)))
	{
		LeftPos = std::min(LeftPos, m_CurPos);
	}

	Changed(true);
}

void Edit::AddColor(const ColorItem& col,bool skipsort)
{
	if (skipsort && !ColorListFlags.Check(ECLF_NEEDSORT) && !ColorList.empty() && ColorList.back().Priority > col.Priority)
	{
		ColorListFlags.Set(ECLF_NEEDSORT);
	}

	ColorList.emplace_back(col);

	if (!skipsort)
	{
		std::stable_sort(ALL_RANGE(ColorList));
	}
}

void Edit::SortColorUnlocked()
{
	if (ColorListFlags.Check(ECLF_NEEDFREE))
	{
		ColorListFlags.Clear(ECLF_NEEDFREE);
		if (ColorList.empty())
		{
			clear_and_shrink(ColorList);
		}
	}

	if (ColorListFlags.Check(ECLF_NEEDSORT))
	{
		ColorListFlags.Clear(ECLF_NEEDSORT);
		std::stable_sort(ALL_RANGE(ColorList));
	}
}

void Edit::DeleteColor(const delete_color_condition& Condition, bool skipfree)
{
	if (!ColorList.empty())
	{
		ColorList.erase(std::remove_if(ALL_RANGE(ColorList), Condition), ColorList.end());

		if (ColorList.empty())
		{
			if (skipfree)
			{
				ColorListFlags.Set(ECLF_NEEDFREE);
			}
			else
			{
				clear_and_shrink(ColorList);
			}
		}
	}
}

bool Edit::GetColor(ColorItem& col, size_t Item) const
{
	if (Item >= ColorList.size())
		return false;

	col = ColorList[Item];
	return true;
}

void Edit::ApplyColor(const FarColor& SelColor, int XPos, int FocusedLeftPos)
{
	// ��� ����������� ��������� ����������� ������� ����� ���������� �����
	int Pos = INT_MIN, TabPos = INT_MIN, TabEditorPos = INT_MIN;

	// ������������ �������� ���������
	FOR(const auto& CurItem, ColorList)
	{
		// ���������� �������� � ������� ������ ������ �����
		if (CurItem.StartPos > CurItem.EndPos)
			continue;

		int Width = ObjWidth();

		int Length = CurItem.EndPos-CurItem.StartPos+1;

		if (CurItem.StartPos + Length >= m_Str.size())
			Length = m_Str.size() - CurItem.StartPos;

		// �������� ��������� �������
		int RealStart, Start;

		// ���� ���������� ������� ����� �������, �� ������ �� ���������
		// � ����� ���� ����� ����������� ��������
		if (Pos == CurItem.StartPos)
		{
			RealStart = TabPos;
			Start = TabEditorPos;
		}
		// ���� ���������� ��� ������ ��� ��� ���������� ������� ������ �������,
		// �� ���������� ���������� � ������ ������
		else if (Pos == INT_MIN || CurItem.StartPos < Pos)
		{
			RealStart = RealPosToTab(CurItem.StartPos);
			Start = RealStart-FocusedLeftPos;
		}
		// ��� ����������� ������ ���������� ������������ ���������� �������
		else
		{
			RealStart = RealPosToTab(TabPos, Pos, CurItem.StartPos, nullptr);
			Start = RealStart-FocusedLeftPos;
		}

		// ���������� ����������� �������� ��� �� ����������� ���������� �������������
		Pos = CurItem.StartPos;
		TabPos = RealStart;
		TabEditorPos = Start;

		// ���������� �������� ��������� � ������� ��������� ������� �� �������
		if (Start >= Width)
			continue;

		// ������������� ������������ ����� (�����������, ���� ������������ ���� ECF_TABMARKFIRST)
		int CorrectPos = CurItem.Flags & ECF_TABMARKFIRST ? 0 : 1;

		// �������� �������� �������
		int EndPos = CurItem.EndPos;
		int RealEnd, End;

		bool TabMarkCurrent=false;

		// ������������ ������, ����� ���������� ������� ����� �������, �� ����
		// ����� �������������� ������ ����� 1
		if (Pos == EndPos)
		{
			// ���� ���������� ������ ������������� ������������ ����� � ������������
			// ������ ������ -- ��� ���, �� ������ ������ � ������ �������������,
			// ����� ������ �� ��������� � ���� ������ ��������
			if (CorrectPos && EndPos < m_Str.size() && m_Str[EndPos] == L'\t')
			{
				RealEnd = RealPosToTab(TabPos, Pos, ++EndPos, nullptr);
				End = RealEnd-FocusedLeftPos;
				TabMarkCurrent = (CurItem.Flags & ECF_TABMARKCURRENT) && XPos>=Start && XPos<End;
			}
			else
			{
				RealEnd = TabPos;
				CorrectPos = 0;
				End = TabEditorPos;
			}
		}
		// ���� ���������� ������� ������ �������, �� ���������� ����������
		// � ������ ������ (� ������ ������������� ������������ �����)
		else if (EndPos < Pos)
		{
			// TODO: �������� ��� �� ����� ��������� � ������ ����� (�� ������� Mantis#0001718)
			RealEnd = RealPosToTab(0, 0, EndPos, &CorrectPos);
			EndPos += CorrectPos;
			End = RealEnd-FocusedLeftPos;
		}
		// ��� ����������� ������ ���������� ������������ ���������� ������� (� ������
		// ������������� ������������ �����)
		else
		{
			// Mantis#0001718: ���������� ECF_TABMARKFIRST �� ������ ��������� ������������
			// ��������� � ������ ���������� ����
			if (CorrectPos && EndPos < m_Str.size() && m_Str[EndPos] == L'\t')
				RealEnd = RealPosToTab(TabPos, Pos, ++EndPos, nullptr);
			else
			{
				RealEnd = RealPosToTab(TabPos, Pos, EndPos, &CorrectPos);
				EndPos += CorrectPos;
			}
			End = RealEnd-FocusedLeftPos;
		}

		// ���������� ����������� �������� ��� �� ����������� ���������� �������������
		Pos = EndPos;
		TabPos = RealEnd;
		TabEditorPos = End;

		if(TabMarkCurrent)
		{
			Start = XPos;
			End = XPos;
		}
		else
		{
			// ���������� �������� ��������� � ������� �������� ������� ������ ����� ������� ������
			if (End < 0)
				continue;

			// �������� ��������� �������� �� ������
			if (Start < 0)
				Start = 0;

			if (End >= Width)
				End = Width-1;
			else
				End -= CorrectPos;
		}

		// ������������ �������, ���� ���� ��� ������������
		if (End >= Start)
		{
			Global->ScrBuf->ApplyColor(
			    m_X1+Start,
			    m_Y1,
			    m_X1+End,
			    m_Y1,
			    CurItem.GetColor(),
			    // �� ������������ ���������
			    SelColor,
			    true
			);
		}
	}
}

/* $ 24.09.2000 SVS $
  ������� Xlat - ������������� �� �������� QWERTY <-> ������
*/
void Edit::Xlat(bool All)
{
	//   ��� CmdLine - ���� ��� ���������, ����������� ��� ������
	if (All && m_SelStart == -1 && !m_SelEnd)
	{
		::Xlat(m_Str.data(), 0, m_Str.size(), Global->Opt->XLat.Flags);
		Changed();
		Show();
		return;
	}

	if (m_SelStart != -1 && m_SelStart != m_SelEnd)
	{
		if (m_SelEnd == -1)
			m_SelEnd = m_Str.size();

		::Xlat(m_Str.data(), m_SelStart, m_SelEnd, Global->Opt->XLat.Flags);
		Changed();
		Show();
	}
	/* $ 25.11.2000 IS
	 ���� ��� ���������, �� ���������� ������� �����. ����� ������������ ��
	 ������ ����������� ������ ������������.
	*/
	else
	{
		/* $ 10.12.2000 IS
		   ������������ ������ �� �����, �� ������� ����� ������, ��� �� �����, ���
		   ��������� ����� ������� ������� �� 1 ������
		*/
		int start = m_CurPos, StrSize = m_Str.size();
		bool DoXlat=true;

		if (IsWordDiv(Global->Opt->XLat.strWordDivForXlat,m_Str[start]))
		{
			if (start) start--;

			DoXlat=(!IsWordDiv(Global->Opt->XLat.strWordDivForXlat,m_Str[start]));
		}

		if (DoXlat)
		{
			while (start>=0 && !IsWordDiv(Global->Opt->XLat.strWordDivForXlat,m_Str[start]))
				start--;

			start++;
			int end=start+1;

			while (end<StrSize && !IsWordDiv(Global->Opt->XLat.strWordDivForXlat,m_Str[end]))
				end++;

			::Xlat(m_Str.data(), start, end, Global->Opt->XLat.Flags);
			Changed();
			Show();
		}
	}
}

/* $ 15.11.2000 KM
   ���������: �������� �� ������ � �����������
   �������� ��������, ������������ ������
*/
int Edit::KeyMatchedMask(int Key, const string& Mask) const
{
	int Inserted=FALSE;

	if (Mask[m_CurPos]==EDMASK_ANY)
		Inserted=TRUE;
	else if (Mask[m_CurPos]==EDMASK_DSS && (iswdigit(Key) || Key==L' ' || Key==L'-'))
		Inserted=TRUE;
	else if (Mask[m_CurPos]==EDMASK_DIGITS && (iswdigit(Key) || Key==L' '))
		Inserted=TRUE;
	else if (Mask[m_CurPos]==EDMASK_DIGIT && (iswdigit(Key)))
		Inserted=TRUE;
	else if (Mask[m_CurPos]==EDMASK_ALPHA && IsAlpha(Key))
		Inserted=TRUE;
	else if (Mask[m_CurPos]==EDMASK_HEX && (iswdigit(Key) || (ToUpper(Key)>=L'A' && ToUpper(Key)<=L'F') || (ToUpper(Key)>=L'a' && ToUpper(Key)<=L'f')))
		Inserted=TRUE;

	return Inserted;
}

int Edit::CheckCharMask(wchar_t Chr)
{
	return Chr==EDMASK_ANY || Chr==EDMASK_DIGIT || Chr==EDMASK_DIGITS || Chr==EDMASK_DSS || Chr==EDMASK_ALPHA || Chr==EDMASK_HEX;
}

void Edit::SetDialogParent(DWORD Sets)
{
	if ((Sets&(FEDITLINE_PARENT_SINGLELINE|FEDITLINE_PARENT_MULTILINE)) == (FEDITLINE_PARENT_SINGLELINE|FEDITLINE_PARENT_MULTILINE) ||
	        !(Sets&(FEDITLINE_PARENT_SINGLELINE|FEDITLINE_PARENT_MULTILINE)))
		m_Flags.Clear(FEDITLINE_PARENT_SINGLELINE|FEDITLINE_PARENT_MULTILINE);
	else if (Sets&FEDITLINE_PARENT_SINGLELINE)
	{
		m_Flags.Clear(FEDITLINE_PARENT_MULTILINE);
		m_Flags.Set(FEDITLINE_PARENT_SINGLELINE);
	}
	else if (Sets&FEDITLINE_PARENT_MULTILINE)
	{
		m_Flags.Clear(FEDITLINE_PARENT_SINGLELINE);
		m_Flags.Set(FEDITLINE_PARENT_MULTILINE);
	}
}

void Edit::FixLeftPos(int TabCurPos)
{
	if (TabCurPos<0) TabCurPos=GetTabCurPos(); //�����������, ����� ��� ���� �� ������
	if (TabCurPos-LeftPos>ObjWidth()-1)
		LeftPos=TabCurPos-ObjWidth()+1;

	if (TabCurPos<LeftPos)
		LeftPos=TabCurPos;
}

const FarColor& Edit::GetNormalColor() const
{
	return GetEditor()->GetNormalColor();
}

const FarColor& Edit::GetSelectedColor() const
{
	return GetEditor()->GetSelectedColor();
}

const FarColor& Edit::GetUnchangedColor() const
{
	return GetNormalColor();
}

size_t Edit::GetTabSize() const
{
	return GetEditor()->GetTabSize();
}

EXPAND_TABS Edit::GetTabExpandMode() const
{
	return GetEditor()->GetConvertTabs();
}

const string& Edit::WordDiv() const
{
	return GetEditor()->GetWordDiv();
}

int Edit::GetCursorSize() const
{
	return -1;
}

int Edit::GetMacroSelectionStart() const
{
	return GetEditor()->GetMacroSelectionStart();
}

void Edit::SetMacroSelectionStart(int Value)
{
	GetEditor()->SetMacroSelectionStart(Value);
}

int Edit::GetLineCursorPos() const
{
	return GetEditor()->GetLineCursorPos();
}

void Edit::SetLineCursorPos(int Value)
{
	return GetEditor()->SetLineCursorPos(Value);
}

Editor* Edit::GetEditor(void)const
{
	auto owner=dynamic_cast<EditorContainer*>(GetOwner().get());
	if (owner) return owner->GetEditor();
	return nullptr;
}
