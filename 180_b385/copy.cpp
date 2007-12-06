/*
copy.cpp

����������� ������
*/
/*
Copyright (c) 1996 Eugene Roshal
Copyright (c) 2000 Far Group
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

#include "copy.hpp"
#include "global.hpp"
#include "lang.hpp"
#include "keys.hpp"
#include "colors.hpp"
#include "fn.hpp"
#include "flink.hpp"
#include "dialog.hpp"
#include "ctrlobj.hpp"
#include "filepanels.hpp"
#include "panel.hpp"
#include "filelist.hpp"
#include "foldtree.hpp"
#include "treelist.hpp"
#include "chgprior.hpp"
#include "scantree.hpp"
#include "savescr.hpp"
#include "manager.hpp"
#include "constitle.hpp"
#include "lockscrn.hpp"
#include "filefilter.hpp"

/* ����� ����� �������� ������������ */
extern long WaitUserTime;
/* ��� ����, ��� �� ����� ��� �������� ������������ ������, � remaining/speed ��� */
static long OldCalcTime;

/* �������� ��� ���������� ��������-����. */
#define COPY_TIMEOUT 200

// ������ � ������ �������
#define DLG_HEIGHT 16
#define DLG_WIDTH 76

#define SDDATA_SIZE   64000

enum {COPY_BUFFER_SIZE  = 0x10000};

enum {
  COPY_RULE_NUL    = 0x0001,
  COPY_RULE_FILES  = 0x0002,
};

static int TotalFilesToProcess;

static int ShowCopyTime;
static clock_t CopyStartTime;
static clock_t LastShowTime;

static int OrigScrX,OrigScrY;

static DWORD WINAPI CopyProgressRoutine(LARGE_INTEGER TotalFileSize,
       LARGE_INTEGER TotalBytesTransferred,LARGE_INTEGER StreamSize,
       LARGE_INTEGER StreamBytesTransferred,DWORD dwStreamNumber,
       DWORD dwCallbackReason,HANDLE hSourceFile,HANDLE hDestinationFile,
       LPVOID lpData);


static int BarX,BarY,BarLength;

static unsigned __int64 TotalCopySize, TotalCopiedSize; // ����� ��������� �����������
static unsigned __int64 CurCopiedSize;                  // ������� ��������� �����������
static unsigned __int64 TotalSkippedSize;               // ����� ������ ����������� ������
static unsigned __int64 TotalCopiedSizeEx;
static int   CountTarget;                    // ����� �����.
static int CopySecurityCopy=-1;
static int CopySecurityMove=-1;
static bool ShowTotalCopySize;
static int StaticMove;
static string strTotalCopySizeText;
static ConsoleTitle *StaticCopyTitle=NULL;
static BOOL NT5, NT;

static FileFilter *Filter;
static int UseFilter=FALSE;

struct CopyDlgParam {
  ShellCopy *thisClass;
  int AltF10;
  int FileAttr;
  int SelCount;
  int FolderPresent;
  int FilesPresent;
  int OnlyNewerFiles;
  int CopySecurity;
  BOOL FSysNTFS;
  string strPluginFormat;
  DWORD FileSystemFlagsSrc;
  int IsDTSrcFixed;
  int IsDTDstFixed;

  void Clear()
  {
	thisClass=NULL;
	AltF10=0;
	FileAttr=0;
	SelCount=0;
	FolderPresent=0;
	FilesPresent=0;
	OnlyNewerFiles=0;
	CopySecurity=0;
	FSysNTFS=FALSE;
	strPluginFormat=L"";
	FileSystemFlagsSrc=0;
	IsDTSrcFixed=0;
	IsDTDstFixed=0;
  }
};

enum enumShellCopy {
  ID_SC_TITLE=0,
  ID_SC_TARGETTITLE,
  ID_SC_TARGETEDIT,
  ID_SC_SEPARATOR1,
  ID_SC_ACTITLE,
  ID_SC_ACLEAVE,
  ID_SC_ACCOPY,
  ID_SC_ACINHERIT,
  ID_SC_SEPARATOR2,
  ID_SC_ONLYNEWER,
  ID_SC_COPYSYMLINK,
  ID_SC_MULTITARGET,
  ID_SC_SEPARATOR3,
  ID_SC_USEFILTER,
  ID_SC_SEPARATOR4,
  ID_SC_BTNCOPY,
  ID_SC_BTNTREE,
  ID_SC_BTNFILTER,
  ID_SC_BTNCANCEL,
  ID_SC_SOURCEFILENAME,
};

ShellCopy::ShellCopy(Panel *SrcPanel,        // �������� ������ (��������)
                     int Move,               // =1 - �������� Move
                     int Link,               // =1 - Sym/Hard Link
                     int CurrentOnly,        // =1 - ������ ������� ����, ��� ��������
                     int Ask,                // =1 - �������� ������?
                     int &ToPlugin,          // =?
                     const wchar_t *PluginDestPath)
{
  DestList.SetParameters(0,0,ULF_UNIQUE);
  /* IS $ */
  struct CopyDlgParam CDP;

  string strCopyStr;
  string strSelNameShort, strSelName;

  int DestPlugin;
  int AddSlash=FALSE;

  Filter=NULL;
  sddata=NULL;

  // ***********************************************************************
  // *** ��������������� ��������
  // ***********************************************************************
  // ����� ��������� ������ OS

  NT=WinVer.dwPlatformId==VER_PLATFORM_WIN32_NT;
  NT5=NT && WinVer.dwMajorVersion >= 5;

  CopyBuffer=NULL;

  if(Link && !NT) // �������� ������ ������ ��� NT
  {
    Message(MSG_DOWN|MSG_WARNING,1,UMSG(MWarning),
              UMSG(MCopyNotSupportLink1),
              UMSG(MCopyNotSupportLink2),
              UMSG(MOk));
    return;
  }

  CDP.Clear();
  CDP.IsDTSrcFixed=-1;
  CDP.IsDTDstFixed=-1;

  if ((CDP.SelCount=SrcPanel->GetSelCount())==0)
    return;

  if (CDP.SelCount==1)
  {
    SrcPanel->GetSelName(NULL,CDP.FileAttr); //????
    SrcPanel->GetSelName(&strSelName,CDP.FileAttr);
    if (TestParentFolderName(strSelName))
      return;
  }

  // �������� ������ �������
  Filter=new FileFilter(SrcPanel, FFT_COPY);

  sddata=new char[SDDATA_SIZE]; // Security 16000?

  // $ 26.05.2001 OT ��������� ����������� ������� �� ����� �����������
  _tran(SysLog(L"call (*FrameManager)[0]->LockRefresh()"));
  (*FrameManager)[0]->Lock();

  // ������ ������ ������� �� �������
  GetRegKey(L"System", L"CopyBufferSize", CopyBufferSize, 0);
  if (CopyBufferSize == 0)
    CopyBufferSize = COPY_BUFFER_SIZE; //����. ������ 64�
  if (CopyBufferSize < COPY_BUFFER_SIZE)
    CopyBufferSize = COPY_BUFFER_SIZE;

  CDP.thisClass=this;
  CDP.AltF10=0;
  CDP.FolderPresent=0;
  CDP.FilesPresent=0;

  ShellCopy::Flags=0;
  ShellCopy::Flags|=Move?FCOPY_MOVE:0;
  ShellCopy::Flags|=Link?FCOPY_LINK:0;
  ShellCopy::Flags|=CurrentOnly?FCOPY_CURRENTONLY:0;

  ShowTotalCopySize=Opt.CMOpt.CopyShowTotal != 0;

  strTotalCopySizeText=L"";

  SelectedFolderNameLength=0;
  DestPlugin=ToPlugin;
  ToPlugin=FALSE;
  strSrcFSName=L"";
  SrcDriveType=0;
  StaticMove=Move;

  ShellCopy::SrcPanel=SrcPanel;
  DestPanel=CtrlObject->Cp()->GetAnotherPanel(SrcPanel);
  DestPanelMode=DestPlugin ? DestPanel->GetMode():NORMAL_PANEL;
  SrcPanelMode=SrcPanel->GetMode();

  int SizeBuffer=2048;
  if(DestPanelMode == PLUGIN_PANEL)
  {
    struct OpenPluginInfo Info;
    DestPanel->GetOpenPluginInfo(&Info);
    int LenCurDir=StrLength(NullToEmpty(Info.CurDir));
    if(SizeBuffer < LenCurDir)
      SizeBuffer=LenCurDir;
  }
  SizeBuffer+=NM; // ������� :-)

  /* $ 03.08.2001 IS
       CopyDlgValue - � ���� ���������� ������ �������� ������� �� �������,
       ������ ��� ���������� �������� ����������, � CopyDlg[2].Data �� �������.
  */
  string strCopyDlgValue;

  string strInitDestDir;
  string strDestDir;
  string strSrcDir;

  // ***********************************************************************
  // *** Prepare Dialog Controls
  // ***********************************************************************
  const wchar_t *HistoryName=L"Copy";
  /* $ 03.08.2001 IS ������� ����� �����: ����������������� */
  static struct DialogDataEx CopyDlgData[]={
  /* 00 */  DI_DOUBLEBOX,   3, 1,DLG_WIDTH-4,DLG_HEIGHT-2,0,0,0,0,(wchar_t *)MCopyDlgTitle,
  /* 01 */  DI_TEXT,        5, 2, 0, 2,0,0,0,0,(wchar_t *)MCMLTargetTO,
  /* 02 */  DI_EDIT,        5, 3,70, 3,1,(DWORD_PTR)HistoryName,DIF_HISTORY|DIF_EDITEXPAND|DIF_USELASTHISTORY/*|DIF_EDITPATH*/,0,L"",
  /* 03 */  DI_TEXT,        3, 4, 0, 4,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,L"",
  /* 04 */  DI_TEXT,        5, 5, 0, 5,0,0,0,0,(wchar_t *)MCopySecurity,
  /* 05 */  DI_RADIOBUTTON, 5, 5, 0, 5,0,0,DIF_GROUP,0,(wchar_t *)MCopySecurityLeave,
  /* 06 */  DI_RADIOBUTTON, 5, 5, 0, 5,0,0,0,0,(wchar_t *)MCopySecurityCopy,
  /* 07 */  DI_RADIOBUTTON, 5, 5, 0, 5,0,0,0,0,(wchar_t *)MCopySecurityInherit,
  /* 08 */  DI_TEXT,        3, 6, 0, 6,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,L"",
  /* 09 */  DI_CHECKBOX,    5, 7, 0, 7,0,0,0,0,(wchar_t *)MCopyOnlyNewerFiles,
  /* 10 */  DI_CHECKBOX,    5, 8, 0, 8,0,0,0,0,(wchar_t *)MCopySymLinkContents,
  /* 11 */  DI_CHECKBOX,    5, 9, 0, 9,0,0,0,0,(wchar_t *)MCopyMultiActions,
  /* 12 */  DI_TEXT,        3,10, 0,10,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,L"",
  /* 13 */  DI_CHECKBOX,    5,11, 0,11,0,0,0,0,(wchar_t *)MCopyUseFilter,
  /* 14 */  DI_TEXT,        3,12, 0,12,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,L"",
  /* 15 */  DI_BUTTON,      0,13, 0,13,0,0,DIF_CENTERGROUP,1,(wchar_t *)MCopyDlgCopy,
  /* 16 */  DI_BUTTON,      0,13, 0,13,0,0,DIF_CENTERGROUP|DIF_BTNNOCLOSE,0,(wchar_t *)MCopyDlgTree,
  /* 17 */  DI_BUTTON,      0,13, 0,13,0,0,DIF_CENTERGROUP|DIF_BTNNOCLOSE,0,(wchar_t *)MCopySetFilter,
  /* 18 */  DI_BUTTON,      0,13, 0,13,0,0,DIF_CENTERGROUP,0,(wchar_t *)MCopyDlgCancel,
  /* 19 */  DI_TEXT,        5, 2, 0, 2,0,0,DIF_SHOWAMPERSAND,0,L"",
  };
  MakeDialogItemsEx(CopyDlgData,CopyDlg);

  CopyDlg[ID_SC_MULTITARGET].Selected=Opt.CMOpt.MultiCopy;

  // ������������ ������. KM
  CopyDlg[ID_SC_USEFILTER].Selected=UseFilter;

  {
    const wchar_t *Str = UMSG(MCopySecurity);
    CopyDlg[ID_SC_ACLEAVE].X1 = CopyDlg[ID_SC_ACTITLE].X1 + StrLength(Str) - (wcschr(Str, L'&')?1:0) + 1;
    Str = UMSG(MCopySecurityLeave);
    CopyDlg[ID_SC_ACCOPY].X1 = CopyDlg[ID_SC_ACLEAVE].X1 + StrLength(Str) - (wcschr(Str, L'&')?1:0) + 5;
    Str = UMSG(MCopySecurityCopy);
    CopyDlg[ID_SC_ACINHERIT].X1 = CopyDlg[ID_SC_ACCOPY].X1 + StrLength(Str) - (wcschr(Str, L'&')?1:0) + 5;
  }

  if(Link)
  {
    CopyDlg[ID_SC_COPYSYMLINK].Selected=0;
    CopyDlg[ID_SC_COPYSYMLINK].Flags|=DIF_DISABLE;
    CDP.CopySecurity=1;
  }
  else if(Move)  // ������ ��� �������
  {
    //   2 - Default
    //   1 - Copy access rights
    //   0 - Inherit access rights
    CDP.CopySecurity=2;

    // ������� ����� "Inherit access rights"?
    // CSO_MOVE_SETINHERITSECURITY - ���������� ����
    if((Opt.CMOpt.CopySecurityOptions&CSO_MOVE_SETINHERITSECURITY) == CSO_MOVE_SETINHERITSECURITY)
      CDP.CopySecurity=0;
    else if (Opt.CMOpt.CopySecurityOptions&CSO_MOVE_SETCOPYSECURITY)
      CDP.CopySecurity=1;

    // ������ ���������� �����������?
    if(CopySecurityMove != -1 && (Opt.CMOpt.CopySecurityOptions&CSO_MOVE_SESSIONSECURITY))
      CDP.CopySecurity=CopySecurityMove;
    else
      CopySecurityMove=CDP.CopySecurity;
  }
  else // ������ ��� �����������
  {
    //   2 - Default
    //   1 - Copy access rights
    //   0 - Inherit access rights
    CDP.CopySecurity=2;

    // ������� ����� "Inherit access rights"?
    // CSO_COPY_SETINHERITSECURITY - ���������� ����
    if((Opt.CMOpt.CopySecurityOptions&CSO_COPY_SETINHERITSECURITY) == CSO_COPY_SETINHERITSECURITY)
      CDP.CopySecurity=0;
    else if (Opt.CMOpt.CopySecurityOptions&CSO_COPY_SETCOPYSECURITY)
      CDP.CopySecurity=1;

    // ������ ���������� �����������?
    if(CopySecurityCopy != -1 && Opt.CMOpt.CopySecurityOptions&CSO_COPY_SESSIONSECURITY)
      CDP.CopySecurity=CopySecurityCopy;
    else
      CopySecurityCopy=CDP.CopySecurity;
  }

  // ��� ������ ����������
  if(CDP.CopySecurity)
  {
    if(CDP.CopySecurity == 1)
    {
      ShellCopy::Flags|=FCOPY_COPYSECURITY;
      CopyDlg[ID_SC_ACCOPY].Selected=1;
    }
    else
    {
      ShellCopy::Flags|=FCOPY_LEAVESECURITY;
      CopyDlg[ID_SC_ACLEAVE].Selected=1;
    }
  }
  else
  {
    ShellCopy::Flags&=~(FCOPY_COPYSECURITY|FCOPY_LEAVESECURITY);
    CopyDlg[ID_SC_ACINHERIT].Selected=1;
  }

  if (CDP.SelCount==1)
  { // SelName & FileAttr ��� ��������� (��. � ����� ������ �������)

    // ���� ������� � �� ����, �� ������������, ��� ����� ������� �������
    if(Link && NT5 && (CDP.FileAttr&FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY && DestPanelMode == NORMAL_PANEL)
    {
      CDP.OnlyNewerFiles=CopyDlg[ID_SC_ONLYNEWER].Selected=1;
      CDP.FolderPresent=TRUE;
    }
    else
      CDP.OnlyNewerFiles=CopyDlg[ID_SC_ONLYNEWER].Selected=0;

    if (SrcPanel->GetType()==TREE_PANEL)
    {
      string strNewDir;
      wchar_t *ChPtr;
      strNewDir = strSelName;

      ChPtr = strNewDir.GetBuffer ();

      if ((ChPtr=wcsrchr(ChPtr,L'\\'))!=0)
      {
        *ChPtr=0;

        strNewDir.ReleaseBuffer ();

        if (ChPtr==(const wchar_t*)strNewDir || *(ChPtr-1)==L':')
          strNewDir += L"\\";
        FarChDir(strNewDir);
      }
      else
        strNewDir.ReleaseBuffer ();
    }

    strSelNameShort = strSelName;
    TruncPathStr(strSelNameShort,33);
    // ������� ��� �� �������� �������.
    strCopyStr.Format (
            UMSG(Move?MMoveFile:(Link?MLinkFile:MCopyFile)),
            (const wchar_t*)strSelNameShort);

    // ���� �������� ��������� ����, �� ��������� ������������ ������
    if (!(CDP.FileAttr&FILE_ATTRIBUTE_DIRECTORY))
    {
      CopyDlg[ID_SC_USEFILTER].Selected=0;
      CopyDlg[ID_SC_USEFILTER].Flags|=DIF_DISABLE;
    }
  }
  else // �������� ���������!
  {
    int NOper=MCopyFiles;
         if (Move) NOper=MMoveFiles;
    else if (Link) NOper=MLinkFiles;

    // ��������� ����� - ��� ���������
    char StrItems[32];
    itoa(CDP.SelCount,StrItems,10);
    int LenItems=(int)strlen(StrItems);
    int NItems=MCMLItemsA;
    if((LenItems >= 2 && StrItems[LenItems-2] == '1') ||
        StrItems[LenItems-1] >= '5' ||
        StrItems[LenItems-1] == '0')
      NItems=MCMLItemsS;
    else if(StrItems[LenItems-1] == '1')
      NItems=MCMLItems0;
    strCopyStr.Format (UMSG(NOper),CDP.SelCount,UMSG(NItems));
  }

    CopyDlg[ID_SC_SOURCEFILENAME].strData.Format (L"%.65s", (const wchar_t*)strCopyStr);

    CopyDlg[ID_SC_ONLYNEWER].strData = UMSG(Link?MCopySymLink:MCopyOnlyNewerFiles);
    CopyDlg[ID_SC_TITLE].strData = UMSG(Move?MMoveDlgTitle :(Link?MLinkDlgTitle:MCopyDlgTitle));
    CopyDlg[ID_SC_BTNCOPY].strData = UMSG(Move?MCopyDlgRename:(Link?MCopyDlgLink:MCopyDlgCopy));

  if (Link)
  {
    // ���������� ����� ��� �������, ���� OS < NT2000.
    if(!NT5 || ((CurrentOnly || CDP.SelCount==1) && !(CDP.FileAttr&FILE_ATTRIBUTE_DIRECTORY)))
    {
      CopyDlg[ID_SC_ONLYNEWER].Flags|=DIF_DISABLE;
      CDP.OnlyNewerFiles=CopyDlg[ID_SC_ONLYNEWER].Selected=0;
    }
    // ���������� ����� ��� ����������� �����.
    CopyDlg[ID_SC_ACCOPY].Flags|=DIF_DISABLE;
    CopyDlg[ID_SC_ACINHERIT].Flags|=DIF_DISABLE;
    CopyDlg[ID_SC_ACLEAVE].Flags|=DIF_DISABLE;
  }
  else if(DestPanelMode == PLUGIN_PANEL)
  {
    // ���� ��������������� ������ - ������, �� �������� OnlyNewer //?????
    CDP.CopySecurity=2;
    CDP.OnlyNewerFiles=0;
    CopyDlg[ID_SC_ONLYNEWER].Selected=0;
    CopyDlg[ID_SC_ACCOPY].Selected=0;
    CopyDlg[ID_SC_ACINHERIT].Selected=0;
    CopyDlg[ID_SC_ACLEAVE].Selected=1;
    CopyDlg[ID_SC_ONLYNEWER].Flags|=DIF_DISABLE;
    CopyDlg[ID_SC_ACCOPY].Flags|=DIF_DISABLE;
    CopyDlg[ID_SC_ACINHERIT].Flags|=DIF_DISABLE;
    CopyDlg[ID_SC_ACLEAVE].Flags|=DIF_DISABLE;
  }

  DestPanel->GetCurDir(strDestDir);
  SrcPanel->GetCurDir(strSrcDir);

  CopyDlg[ID_SC_TARGETEDIT].nMaxLength = 520; //!!!!!

  if (CurrentOnly)
  {
    //   ��� ����������� ������ �������� ��� �������� ����� ��� ��� � �������, ���� ��� �������� �����������.
    CopyDlg[ID_SC_TARGETEDIT].strData = strSelName;
    if(wcspbrk(CopyDlg[ID_SC_TARGETEDIT].strData,L",;"))
    {
      Unquote(CopyDlg[ID_SC_TARGETEDIT].strData);     // ������ ��� ������ �������
      InsertQuote(CopyDlg[ID_SC_TARGETEDIT].strData); // ������� � �������, �.�. ����� ���� �����������
    }
  }
  else
    switch(DestPanelMode)
    {
      case NORMAL_PANEL:
        if (( strDestDir.IsEmpty () || !DestPanel->IsVisible() || !StrCmpI(strSrcDir,strDestDir)) && CDP.SelCount==1)
          CopyDlg[ID_SC_TARGETEDIT].strData = strSelName;
        else
        {
          CopyDlg[ID_SC_TARGETEDIT].strData = strDestDir;
          AddEndSlash(CopyDlg[ID_SC_TARGETEDIT].strData);
        }
        /* $ 19.07.2003 IS
           ���� ���� �������� �����������, �� ������� �� � �������, ���� �� ��������
           ������ ��� F5, Enter � �������, ����� ������������ ������� MultiCopy
        */
        if(wcspbrk(CopyDlg[ID_SC_TARGETEDIT].strData,L",;"))
        {
          Unquote(CopyDlg[ID_SC_TARGETEDIT].strData);     // ������ ��� ������ �������
          InsertQuote(CopyDlg[ID_SC_TARGETEDIT].strData); // ������� � �������, �.�. ����� ���� �����������
        }
        break;
      case PLUGIN_PANEL:
        {
          struct OpenPluginInfo Info;
          DestPanel->GetOpenPluginInfo(&Info);

          string strFormat = NullToEmpty(Info.Format);

          CopyDlg[ID_SC_TARGETEDIT].strData = strFormat+L":";
          while (CopyDlg[ID_SC_TARGETEDIT].strData.GetLength ()<2)
            CopyDlg[ID_SC_TARGETEDIT].strData += L":";

          CDP.strPluginFormat = CopyDlg[ID_SC_TARGETEDIT].strData;
          CDP.strPluginFormat.Upper();
        }
        break;
    }

  strInitDestDir = CopyDlg[ID_SC_TARGETEDIT].strData;

  // ��� �������
  FAR_FIND_DATA_EX fd;

  SrcPanel->GetSelName(NULL,CDP.FileAttr);

  while(SrcPanel->GetSelName(&strSelName,CDP.FileAttr,NULL,&fd))
  {
    if (UseFilter)
    {
      if (!Filter->FileInFilter(&fd))
        continue;
    }

    if(CDP.FileAttr & FILE_ATTRIBUTE_DIRECTORY)
    {
      CDP.FolderPresent=TRUE;
      AddSlash=TRUE;
//      break;
    }
    else
      CDP.FilesPresent=TRUE;
  }

  if(Link) // ������ �� ������ ������ (���������������!)
  {
    int SelectedSymLink=CopyDlg[ID_SC_ONLYNEWER].Selected;
    if(CDP.SelCount > 1 && !CDP.FilesPresent && CDP.FolderPresent)
      SelectedSymLink=1;
    if(!LinkRules(&CopyDlg[ID_SC_BTNCOPY].Flags,
                  &CopyDlg[ID_SC_ONLYNEWER].Flags,
                  &SelectedSymLink,
                  strSrcDir,
                  CopyDlg[ID_SC_TARGETEDIT].strData,
                  &CDP))
      return;

    CopyDlg[ID_SC_ONLYNEWER].Selected=SelectedSymLink;
  }

  RemoveTrailingSpaces(CopyDlg[ID_SC_SOURCEFILENAME].strData);
  // ����������� ������� " to"
  CopyDlg[ID_SC_TARGETTITLE].X1=CopyDlg[ID_SC_TARGETTITLE].X2=CopyDlg[ID_SC_SOURCEFILENAME].X1+(int)CopyDlg[ID_SC_SOURCEFILENAME].strData.GetLength();

  /* $ 15.06.2002 IS
     ��������� ����������� ������ - � ���� ������ ������ �� ������������,
     �� ���������� ��� ����� ����������������. ���� ���������� ���������
     ���������� ������ �����, �� ������� ������.
  */
  if(!Ask)
  {
    strCopyDlgValue = CopyDlg[ID_SC_TARGETEDIT].strData;
    Unquote(strCopyDlgValue);
    InsertQuote(strCopyDlgValue);
    if(!DestList.Set(strCopyDlgValue))
      Ask=TRUE;
  }

  // ***********************************************************************
  // *** ����� � ��������� �������
  // ***********************************************************************
  if (Ask)
  {
    Dialog Dlg(CopyDlg,countof(CopyDlg),CopyDlgProc,(LONG_PTR)&CDP);
    Dlg.SetHelp(Link?L"HardSymLink":L"CopyFiles");
    Dlg.SetPosition(-1,-1,DLG_WIDTH,DLG_HEIGHT);

//    Dlg.Show();
    // $ 02.06.2001 IS + �������� ������ ����� � �������� �������, ���� �� �������� ������
    int DlgExitCode;
    for(;;)
    {
      Dlg.ClearDone();
      Dlg.Process();

      DlgExitCode=Dlg.GetExitCode();
      if(DlgExitCode == ID_SC_BTNCOPY)
      {
        /* $ 03.08.2001 IS
           �������� ������� �� ������� � �������� �� ������ � ����������� ��
           ��������� ����� �����������������
        */
        strCopyDlgValue = CopyDlg[ID_SC_TARGETEDIT].strData;
        Opt.CMOpt.MultiCopy=CopyDlg[ID_SC_MULTITARGET].Selected;
        if(!Opt.CMOpt.MultiCopy || !wcspbrk(CopyDlg[ID_SC_TARGETEDIT].strData,L",;")) // ��������� multi*
        {
           // ������ �������, ������ �������
           RemoveTrailingSpaces(CopyDlg[ID_SC_TARGETEDIT].strData);
           Unquote(CopyDlg[ID_SC_TARGETEDIT].strData);
           RemoveTrailingSpaces(strCopyDlgValue);
           Unquote(strCopyDlgValue);

           // ������� �������, ����� "������" ������ ��������������� ���
           // ����������� �� ������� ������������ � ����
           InsertQuote(strCopyDlgValue);
        }

        if(DestList.Set(strCopyDlgValue) && !wcspbrk(strCopyDlgValue,ReservedFilenameSymbols))
        {
          // ��������� ������� ������������� �������. KM
          UseFilter=CopyDlg[ID_SC_USEFILTER].Selected;
          break;
        }
        else
          Message(MSG_DOWN|MSG_WARNING,1,UMSG(MWarning),UMSG(MCopyIncorrectTargetList), UMSG(MOk));
      }
      else
        break;
    }

    if(DlgExitCode == ID_SC_BTNCANCEL || DlgExitCode < 0 || (CopyDlg[ID_SC_BTNCOPY].Flags&DIF_DISABLE))
    {
      if (DestPlugin)
        ToPlugin=-1;
      return;
    }
  }
  // ***********************************************************************
  // *** ������ ���������� ������ ����� �������
  // ***********************************************************************
  ShellCopy::Flags&=~FCOPY_COPYPARENTSECURITY;
  if(CopyDlg[ID_SC_ACCOPY].Selected)
  {
    ShellCopy::Flags|=FCOPY_COPYSECURITY;
  }
  else if(CopyDlg[ID_SC_ACINHERIT].Selected)
  {
    ShellCopy::Flags&=~(FCOPY_COPYSECURITY|FCOPY_LEAVESECURITY);
  }
  else
  {
    ShellCopy::Flags|=FCOPY_LEAVESECURITY;
  }

  if(Opt.CMOpt.UseSystemCopy)
    ShellCopy::Flags|=FCOPY_USESYSTEMCOPY;
  else
    ShellCopy::Flags&=~FCOPY_USESYSTEMCOPY;

  if(!(ShellCopy::Flags&(FCOPY_COPYSECURITY|FCOPY_LEAVESECURITY)))
    ShellCopy::Flags|=FCOPY_COPYPARENTSECURITY;

  CDP.CopySecurity=ShellCopy::Flags&FCOPY_COPYSECURITY?1:(ShellCopy::Flags&FCOPY_LEAVESECURITY?2:0);

  // � ����� ������ ��������� ���������� ����������� (�� ��� Link, �.�. ��� Link ��������� ��������� - "������!")
  if(!Link)
  {
    if(Move)
      CopySecurityMove=CDP.CopySecurity;
    else
      CopySecurityCopy=CDP.CopySecurity;
  }

  ShellCopy::Flags|=CopyDlg[ID_SC_ONLYNEWER].Selected?FCOPY_ONLYNEWERFILES:0;
  ShellCopy::Flags|=CopyDlg[ID_SC_COPYSYMLINK].Selected?FCOPY_COPYSYMLINKCONTENTS:0;

  if (DestPlugin && !StrCmp(CopyDlg[ID_SC_TARGETEDIT].strData,strInitDestDir))
  {
    ToPlugin=1;
    return;
  }

  int WorkMove=Move;

  if(CheckNulOrCon(CopyDlg[ID_SC_TARGETEDIT].strData))
    ShellCopy::Flags|=FCOPY_COPYTONUL;
  if(ShellCopy::Flags&FCOPY_COPYTONUL)
  {
    ShellCopy::Flags&=~FCOPY_MOVE;
    StaticMove=WorkMove=0;
  }

  if(CDP.SelCount==1 || (ShellCopy::Flags&FCOPY_COPYTONUL))
    AddSlash=FALSE; //???

  if (DestPlugin==2)
  {
    if (PluginDestPath)
      CopyDlg[ID_SC_TARGETEDIT].strData = PluginDestPath;
    return;
  }

  if (Opt.Diz.UpdateMode==DIZ_UPDATE_IF_DISPLAYED && SrcPanel->IsDizDisplayed() ||
      Opt.Diz.UpdateMode==DIZ_UPDATE_ALWAYS)
  {
    CtrlObject->Cp()->LeftPanel->ReadDiz();
    CtrlObject->Cp()->RightPanel->ReadDiz();
  }

  CopyBuffer=new char[CopyBufferSize];
  DestPanel->CloseFile();
  strDestDizPath=L"";
  SrcPanel->SaveSelection();

  wchar_t *lpwszCopyDlgValue = strCopyDlgValue.GetBuffer ();

  // TODO: Posix - bugbug
  for (int I=0;lpwszCopyDlgValue[I]!=0;I++)
    if (lpwszCopyDlgValue[I]==L'/')
      lpwszCopyDlgValue[I]=L'\\';

  strCopyDlgValue.ReleaseBuffer ();

  // ����� �� ���������� ����� �����������?
  ShowCopyTime = Opt.CMOpt.CopyTimeRule & ((ShellCopy::Flags&FCOPY_COPYTONUL)?COPY_RULE_NUL:COPY_RULE_FILES);

  // ***********************************************************************
  // **** ����� ��� ���������������� �������� ���������, ����� ����������
  // **** � �������� Copy/Move/Link
  // ***********************************************************************

  int NeedDizUpdate=FALSE;
  int NeedUpdateAPanel=FALSE;

  // ����! ������������� �������� ����������.
  // � ����������� ���� ���� ����� ������������ � ShellCopy::CheckUpdatePanel()
  ShellCopy::Flags|=FCOPY_UPDATEPPANEL;

  /*
     ���� ������� � �������� ����������� �����, �������� ';',
     �� ����� ������� CopyDlgValue �� ������� MultiCopy �
     �������� CopyFileTree ������ ���������� ���.
  */
  {
    ShellCopy::Flags&=~FCOPY_MOVE;
    if(DestList.Set(strCopyDlgValue)) // ���� ������ ������� "���������������"
    {
        const wchar_t *NamePtr;
        string strNameTmp;

        // ������������������ ���������� � ����� ������ (BugZ#171)
//        CopyBufSize = COPY_BUFFER_SIZE; // �������� � 1�
        CopyBufSize = CopyBufferSize;
        ReadOnlyDelMode=ReadOnlyOvrMode=OvrMode=SkipEncMode=SkipMode=-1;

        // ��������� ���������� �����.
        CountTarget=DestList.GetTotal();

        DestList.Reset();
        TotalFiles=0;
        TotalCopySize=TotalCopiedSize=TotalSkippedSize=0;
        // �������� ����� ������
        if (ShowCopyTime)
        {
          CopyStartTime = clock();
          WaitUserTime = OldCalcTime = 0;
        }

        CopyTitle = new ConsoleTitle(NULL);
        StaticCopyTitle=CopyTitle;

        if(CountTarget > 1)
          StaticMove=WorkMove=0;

        while(NULL!=(NamePtr=DestList.GetNext()))
        {
          CurCopiedSize=0;
          CopyTitle->Set(Move ? UMSG(MCopyMovingTitle):UMSG(MCopyCopyingTitle));

          strNameTmp = NamePtr;

          if ( (strNameTmp.GetLength() == 2) && IsAlpha (strNameTmp.At(0)) && (strNameTmp.At(1) == L':'))
            PrepareDiskPath(strNameTmp,true);

          if(!StrCmp(strNameTmp,L"..") && IsLocalRootPath(strSrcDir))
          {
            if(Message(MSG_WARNING,2,UMSG(MError),UMSG((!Move?MCannotCopyToTwoDot:MCannotMoveToTwoDot)),UMSG(MCannotCopyMoveToTwoDot),UMSG(MCopySkip),UMSG(MCopyCancel)) == 0)
              continue;
            break;
          }

          if(CheckNulOrCon(strNameTmp))
            ShellCopy::Flags|=FCOPY_COPYTONUL;
          else
            ShellCopy::Flags&=~FCOPY_COPYTONUL;

          if(ShellCopy::Flags&FCOPY_COPYTONUL)
          {
            ShellCopy::Flags&=~FCOPY_MOVE;
            StaticMove=WorkMove=0;
          }
//          else
//            StaticMove=WorkMove=Move;

          if(DestList.IsEmpty()) // ����� ������ ������� ��������� � ��������� Move.
          {
            StaticMove=WorkMove=Move;
            ShellCopy::Flags|=WorkMove?FCOPY_MOVE:0; // ������ ��� ��������� ��������
            ShellCopy::Flags|=FCOPY_COPYLASTTIME;
          }

          // ���� ���������� ��������� ������ 1 � ����� ��� ���� �������, �� ������
          // ������ ���, ����� �� ����� ��� '\\'
          // ������� ��� �� ������, � ������ ����� NameTmp �� �������� ������.
          if (AddSlash && wcspbrk(strNameTmp,L"*?")==NULL)
            AddEndSlash(strNameTmp);

          // ��� ����������� ������ ������� ������ ������� "������"
          if (CDP.SelCount==1 && WorkMove && wcspbrk(strNameTmp,L":\\")==NULL)
            ShowTotalCopySize=FALSE;

          if(WorkMove) // ��� ����������� "�����" ��� �� ����������� ��� "���� �� �����"
          {
            if(IsSameDisk(strSrcDir,strNameTmp))
              ShowTotalCopySize=FALSE;

            if(CDP.SelCount==1 && CDP.FolderPresent && CheckUpdateAnotherPanel(SrcPanel,strSelName))
            {
              NeedUpdateAPanel=TRUE;
            }
          }

          // ������� ���� ��� ����
          strDestDizPath=L"";
          ShellCopy::Flags&=~FCOPY_DIZREAD;

          // �������� ���������
          SrcPanel->SaveSelection();

          strDestFSName=L"";

          int OldCopySymlinkContents=ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS;
          // ���������� - ���� ������ �����������
          SetPreRedrawFunc(ShellCopy::PR_ShellCopyMsg);

          // Mantis#45: ���������� ������� ����������� ������ �� ����� � NTFS �� FAT � ����� ��������� ����
          {
            string strFileSysName;
            string strRootDir;
            ConvertNameToFull(strNameTmp,strRootDir);
            GetPathRoot(strRootDir, strRootDir);
            if (apiGetVolumeInformation (strRootDir,NULL,NULL,NULL,NULL,&strFileSysName))
              if(StrCmpI(strFileSysName,L"NTFS"))
                ShellCopy::Flags|=FCOPY_COPYSYMLINKCONTENTS;
          }
          int I=CopyFileTree(strNameTmp);
          SetPreRedrawFunc(NULL);

          if(OldCopySymlinkContents)
            ShellCopy::Flags|=FCOPY_COPYSYMLINKCONTENTS;
          else
            ShellCopy::Flags&=~FCOPY_COPYSYMLINKCONTENTS;

          if(I == COPY_CANCEL)
          {
            NeedDizUpdate=TRUE;
            break;
          }

          // ���� "���� ����� � ������������" - ����������� ���������
          if(!DestList.IsEmpty())
            SrcPanel->RestoreSelection();

          // ����������� � �����.
          if (!(ShellCopy::Flags&FCOPY_COPYTONUL) && !strDestDizPath.IsEmpty())
          {
            string strDestDizName;
            DestDiz.GetDizName(strDestDizName);
            DWORD Attr=GetFileAttributesW(strDestDizName);
            int DestReadOnly=(Attr!=0xffffffff && (Attr & FA_RDONLY));
            if(DestList.IsEmpty()) // ��������� ������ �� ����� ��������� Op.
              if (WorkMove && !DestReadOnly)
                SrcPanel->FlushDiz();
            DestDiz.Flush(strDestDizPath);
          }
        }
        StaticCopyTitle=NULL;
        delete CopyTitle;
    }
    _LOGCOPYR(else SysLog(L"Error: DestList.Set(CopyDlgValue) return FALSE"));
  }

  // ***********************************************************************
  // *** �������������� ������ ��������
  // *** ���������������/�����/��������
  // ***********************************************************************

  if(NeedDizUpdate) // ��� ����������������� ����� ���� �����, �� ��� ���
  {                 // ����� ����� ��������� ����!
    if (!(ShellCopy::Flags&FCOPY_COPYTONUL) && !strDestDizPath.IsEmpty() )
    {
      string strDestDizName;
      DestDiz.GetDizName(strDestDizName);
      DWORD Attr=GetFileAttributesW(strDestDizName);
      int DestReadOnly=(Attr!=0xffffffff && (Attr & FA_RDONLY));
      if (Move && !DestReadOnly)
        SrcPanel->FlushDiz();
      DestDiz.Flush(strDestDizPath);
    }
  }

#if 1
  SrcPanel->Update(UPDATE_KEEP_SELECTION);
  if (CDP.SelCount==1 && !strRenamedName.IsEmpty() )
    SrcPanel->GoToFile(strRenamedName);
#if 1
  if(NeedUpdateAPanel && CDP.FileAttr != -1 && (CDP.FileAttr&FILE_ATTRIBUTE_DIRECTORY) && DestPanelMode != PLUGIN_PANEL)
  {
    string strSrcDir;

    SrcPanel->GetCurDir(strSrcDir);
    DestPanel->SetCurDir(strSrcDir,FALSE);
  }
#else
  if(CDP.FileAttr != -1 && (CDP.FileAttr&FILE_ATTRIBUTE_DIRECTORY) && DestPanelMode != PLUGIN_PANEL)
  {
    // ���� SrcDir ���������� � DestDir...
    string strDestDir;
    string strSrcDir;

    DestPanel->GetCurDir(strDestDir);
    SrcPanel->GetCurDir(strSrcDir);

    if(CheckUpdateAnotherPanel(SrcPanel,strSrcDir))
      DestPanel->SetCurDir(strDestDir,FALSE);
  }
#endif
  // �������� "��������" ������� ��������� ������
  if(ShellCopy::Flags&FCOPY_UPDATEPPANEL)
  {
    DestPanel->SortFileList(TRUE);
    DestPanel->Update(UPDATE_KEEP_SELECTION|UPDATE_SECONDARY);
  }

  if(SrcPanelMode == PLUGIN_PANEL)
    SrcPanel->SetPluginModified();

  CtrlObject->Cp()->Redraw();
#else

  SrcPanel->Update(UPDATE_KEEP_SELECTION);
  if (CDP.SelCount==1 && strRenamedName.IsEmpty() )
    SrcPanel->GoToFile(strRenamedName);

  SrcPanel->Redraw();

  DestPanel->SortFileList(TRUE);
  DestPanel->Update(UPDATE_KEEP_SELECTION|UPDATE_SECONDARY);
  DestPanel->Redraw();
#endif
}


LONG_PTR WINAPI ShellCopy::CopyDlgProc(HANDLE hDlg,int Msg,int Param1,LONG_PTR Param2)
{
  #define DM_CALLTREE (DM_USER+1)

  struct CopyDlgParam *DlgParam;
  DlgParam=(struct CopyDlgParam *)Dialog::SendDlgMessage(hDlg,DM_GETDLGDATA,0,0);

  switch(Msg)
  {
    case DN_BTNCLICK:
    {
      if (Param1==ID_SC_USEFILTER) // "Use filter"
      {
        UseFilter=(int)Param2;
        return TRUE;
      }
      if(Param1 == ID_SC_BTNTREE) // Tree
      {
        Dialog::SendDlgMessage(hDlg,DM_CALLTREE,0,0);
        return FALSE;
      }
      else if(Param1 == ID_SC_BTNCOPY)
      {
        Dialog::SendDlgMessage(hDlg,DM_CLOSE,ID_SC_BTNCOPY,0);
      }
      else if(Param1 == ID_SC_ONLYNEWER && ((DlgParam->thisClass->Flags)&FCOPY_LINK))
      {
        // ����������� ��� ����� �������� ������������ � ������ ����� :-))
        Dialog::SendDlgMessage(hDlg,DN_EDITCHANGE,ID_SC_TARGETEDIT,0);
      }
      else if (Param1==ID_SC_BTNFILTER) // Filter
      {
        Filter->FilterEdit();
        return TRUE;
      }
      break;
    }

    case DM_KEY: // �� ������ ������!
    {
      if(Param2 == KEY_ALTF10 || Param2 == KEY_F10 || Param2 == KEY_SHIFTF10)
      {
        DlgParam->AltF10=Param2 == KEY_ALTF10?1:(Param2 == KEY_SHIFTF10?2:0);
        Dialog::SendDlgMessage(hDlg,DM_CALLTREE,DlgParam->AltF10,0);
        return TRUE;
      }
      break;
    }

    case DN_EDITCHANGE:
      if(Param1 == ID_SC_TARGETEDIT)
      {
        FarDialogItem *DItemACCopy,*DItemACInherit,*DItemACLeave,*DItemOnlyNewer,*DItemBtnCopy;
        string strSrcDir;

        DlgParam->thisClass->SrcPanel->GetCurDir(strSrcDir);

        DItemACCopy = (FarDialogItem *)Dialog::SendDlgMessage(hDlg,DM_GETDLGITEM,ID_SC_ACCOPY,0);
        DItemACInherit = (FarDialogItem *)Dialog::SendDlgMessage(hDlg,DM_GETDLGITEM,ID_SC_ACINHERIT,0);
        DItemACLeave = (FarDialogItem *)Dialog::SendDlgMessage(hDlg,DM_GETDLGITEM,ID_SC_ACLEAVE,0);
        DItemOnlyNewer = (FarDialogItem *)Dialog::SendDlgMessage(hDlg,DM_GETDLGITEM,ID_SC_ONLYNEWER,0);
        DItemBtnCopy = (FarDialogItem *)Dialog::SendDlgMessage(hDlg,DM_GETDLGITEM,ID_SC_BTNCOPY,0);

        // ��� �������� �����?
        if((DlgParam->thisClass->Flags)&FCOPY_LINK)
        {
          DlgParam->thisClass->LinkRules(&DItemBtnCopy->Flags,
                    &DItemOnlyNewer->Flags,
                    &DItemOnlyNewer->Param.Selected,
                    strSrcDir,
                    ((FarDialogItem *)Param2)->PtrData,DlgParam);
        }
        else // ������� Copy/Move
        {
          string strBuf = ((FarDialogItem *)Param2)->PtrData;
          strBuf.Upper();

          if(!DlgParam->strPluginFormat.IsEmpty() && wcsstr(strBuf, DlgParam->strPluginFormat))
          {
            DItemACCopy->Flags|=DIF_DISABLE;
            DItemACInherit->Flags|=DIF_DISABLE;
            DItemACLeave->Flags|=DIF_DISABLE;
            DItemOnlyNewer->Flags|=DIF_DISABLE;
            DlgParam->OnlyNewerFiles=DItemOnlyNewer->Param.Selected;
            DlgParam->CopySecurity=0;
            if (DItemACCopy->Param.Selected)
              DlgParam->CopySecurity=1;
            else if (DItemACLeave->Param.Selected)
              DlgParam->CopySecurity=2;
            DItemACCopy->Param.Selected=0;
            DItemACInherit->Param.Selected=0;
            DItemACLeave->Param.Selected=1;
            DItemOnlyNewer->Param.Selected=0;
          }
          else
          {
            DItemACCopy->Flags&=~DIF_DISABLE;
            DItemACInherit->Flags&=~DIF_DISABLE;
            DItemACLeave->Flags&=~DIF_DISABLE;
            DItemOnlyNewer->Flags&=~DIF_DISABLE;
            DItemOnlyNewer->Param.Selected=DlgParam->OnlyNewerFiles;
            DItemACCopy->Param.Selected=0;
            DItemACInherit->Param.Selected=0;
            DItemACLeave->Param.Selected=0;
            if (DlgParam->CopySecurity == 1)
            {
              DItemACCopy->Param.Selected=1;
            }
            else if (DlgParam->CopySecurity == 2)
            {
              DItemACLeave->Param.Selected=1;
            }
            else
              DItemACInherit->Param.Selected=1;
          }
        }

        Dialog::SendDlgMessage(hDlg,DM_SETDLGITEM,ID_SC_ACCOPY,(LONG_PTR)DItemACCopy);
        Dialog::SendDlgMessage(hDlg,DM_SETDLGITEM,ID_SC_ACINHERIT,(LONG_PTR)DItemACInherit);
        Dialog::SendDlgMessage(hDlg,DM_SETDLGITEM,ID_SC_ACLEAVE,(LONG_PTR)DItemACLeave);
        Dialog::SendDlgMessage(hDlg,DM_SETDLGITEM,ID_SC_ONLYNEWER,(LONG_PTR)DItemOnlyNewer);
        Dialog::SendDlgMessage(hDlg,DM_SETDLGITEM,ID_SC_BTNCOPY,(LONG_PTR)DItemBtnCopy);

        Dialog::SendDlgMessage(hDlg,DM_FREEDLGITEM,0,(LONG_PTR)DItemACCopy);
        Dialog::SendDlgMessage(hDlg,DM_FREEDLGITEM,0,(LONG_PTR)DItemACInherit);
        Dialog::SendDlgMessage(hDlg,DM_FREEDLGITEM,0,(LONG_PTR)DItemACLeave);
        Dialog::SendDlgMessage(hDlg,DM_FREEDLGITEM,0,(LONG_PTR)DItemOnlyNewer);
        Dialog::SendDlgMessage(hDlg,DM_FREEDLGITEM,0,(LONG_PTR)DItemBtnCopy);
      }
      break;

    case DM_CALLTREE:
    {
      /* $ 13.10.2001 IS
         + ��� ����������������� ��������� ��������� � "������" ������� � ���
           ������������� ������ ����� ����� � �������.
         - ���: ��� ����������������� ��������� � "������" ������� ��
           ���������� � �������, ���� �� �������� � �����
           ����� �������-�����������.
         - ���: ����������� �������� Shift-F10, ���� ������ ����� ���������
           ���� �� �����.
         - ���: ����������� �������� Shift-F10 ��� ����������������� -
           ����������� �������� �������, ������ ������������ ����� ������ �������
           � ������.
      */
      BOOL MultiCopy=Dialog::SendDlgMessage(hDlg,DM_GETCHECK,ID_SC_MULTITARGET,0)==BSTATE_CHECKED;

      string strOldFolder;
      int nLength;
      struct FarDialogItemData Data;

      nLength = (int)Dialog::SendDlgMessage(hDlg, DM_GETTEXTLENGTH, ID_SC_TARGETEDIT, 0);

      Data.PtrData = strOldFolder.GetBuffer(nLength+1);
      Data.PtrLength = nLength;
      Dialog::SendDlgMessage(hDlg,DM_GETTEXT,ID_SC_TARGETEDIT,(LONG_PTR)&Data);

      strOldFolder.ReleaseBuffer();

      string strNewFolder;

      if(DlgParam->AltF10 == 2)
      {
        strNewFolder = strOldFolder;
        if(MultiCopy)
        {
          UserDefinedList DestList(0,0,ULF_UNIQUE);
          if(DestList.Set(strOldFolder))
          {
            DestList.Reset();
            const wchar_t *NamePtr=DestList.GetNext();
            if(NamePtr)
              strNewFolder = NamePtr;
          }
        }
        if( strNewFolder.IsEmpty() )
          DlgParam->AltF10=-1;
        else // ������� ������ ����
          DeleteEndSlash(strNewFolder);
      }

      if(DlgParam->AltF10 != -1)
      {
        {
          string strNewFolder2;

          FolderTree Tree(strNewFolder2,
               (DlgParam->AltF10==1?MODALTREE_PASSIVE:
                  (DlgParam->AltF10==2?MODALTREE_FREE:
                     MODALTREE_ACTIVE)),
               FALSE,FALSE);

          strNewFolder = strNewFolder2;
        }
        if ( !strNewFolder.IsEmpty() )
        {
          AddEndSlash(strNewFolder);
          if(MultiCopy) // �����������������
          {
            // ������� �������, ���� ��� �������� �������� �������-�����������
            if(wcspbrk(strNewFolder,L";,"))
              InsertQuote(strNewFolder);

            if( strOldFolder.GetLength() )
                strOldFolder += L";"; // ������� ����������� � ��������� ������

            strOldFolder += strNewFolder;
            strNewFolder = strOldFolder;
          }
          Dialog::SendDlgMessage(hDlg,DM_SETTEXTPTR,ID_SC_TARGETEDIT,(LONG_PTR)(const wchar_t*)strNewFolder);
          Dialog::SendDlgMessage(hDlg,DM_SETFOCUS,ID_SC_TARGETEDIT,0);
        }
      }
      DlgParam->AltF10=0;
      return TRUE;
    }
  }
  return Dialog::DefDlgProc(hDlg,Msg,Param1,Param2);
}



BOOL ShellCopy::LinkRules(DWORD *Flags9,DWORD* Flags5,int* Selected5,
                         const wchar_t *SrcDir,const wchar_t *DstDir,struct CopyDlgParam *CDP)
{
  string strRoot;
  *Flags9|=DIF_DISABLE; // �������� �����!
  *Flags5|=DIF_DISABLE;

  if(DstDir && DstDir[0] == L'\\' && DstDir[1] == L'\\')
  {
    *Selected5=0;
    return TRUE;
  }
  // _SVS(SysLog(L"\n---"));
  // �������� ������ ���� � ��������� � ���������
  if(CDP->IsDTSrcFixed == -1)
  {
    string strFSysNameSrc;
    strRoot = SrcDir;

    Unquote(strRoot);
    ConvertNameToFull(strRoot, strRoot);
    GetPathRoot(strRoot,strRoot);
    // _SVS(SysLog(L"SrcDir=%s",SrcDir));
    // _SVS(SysLog(L"Root=%s",Root));
    CDP->IsDTSrcFixed=FAR_GetDriveType(strRoot);
    CDP->IsDTSrcFixed=CDP->IsDTSrcFixed == DRIVE_FIXED ||
                      IsDriveTypeCDROM(CDP->IsDTSrcFixed) ||
                      (NT5 && WinVer.dwMinorVersion>0?DRIVE_REMOVABLE:0);
    apiGetVolumeInformation(strRoot,NULL,NULL,NULL,&CDP->FileSystemFlagsSrc,&strFSysNameSrc);
    CDP->FSysNTFS=!StrCmpI(strFSysNameSrc,L"NTFS")?TRUE:FALSE;
    // _SVS(SysLog(L"FSysNameSrc=%s",FSysNameSrc));
  }

  /*
  � �������� �� ������ - ����� ������������� [ ] Symbolic link.
  � ������ �� �������  - ���������� ������� �������
  */
  // 1. ���� �������� ��������� �� �� ���������� �����
  if(CDP->IsDTSrcFixed || CDP->FSysNTFS)
  {
    string strFSysNameDst;
    DWORD FileSystemFlagsDst;

    strRoot = DstDir;
    Unquote(strRoot);

    ConvertNameToFull(strRoot,strRoot);
    GetPathRoot(strRoot,strRoot);
    if(GetFileAttributesW(strRoot) == -1)
      return TRUE;

    //GetVolumeInformation(Root,NULL,0,NULL,NULL,&FileSystemFlagsDst,FSysNameDst,sizeof(FSysNameDst));
    // 3. ���� �������� ��������� �� �� ���������� �����
    CDP->IsDTDstFixed=FAR_GetDriveType(strRoot);
    CDP->IsDTDstFixed=CDP->IsDTDstFixed == DRIVE_FIXED || IsDriveTypeCDROM(CDP->IsDTSrcFixed);
    apiGetVolumeInformation(strRoot,NULL,NULL,NULL,&FileSystemFlagsDst,&strFSysNameDst);
    int SameDisk=IsSameDisk(SrcDir,DstDir);
    int IsHardLink=(!CDP->FolderPresent && CDP->FilesPresent && SameDisk && (CDP->IsDTDstFixed || !StrCmpI(strFSysNameDst,L"NTFS")));
    // 4. ���� �������� ��������� �� ���������� �����, �������� �� NTFS
    if(!IsHardLink && (CDP->IsDTDstFixed || !StrCmpI(strFSysNameDst,L"NTFS")) || IsHardLink)
    {
      if(CDP->SelCount == 1)
      {
        if(CDP->FolderPresent) // Folder?
        {
          // . ���� �������� ��������� �� ���������� ����� NTFS, �� �� �������������� repase point
          if(NT5 &&
//             (CDP->FileSystemFlagsSrc&FILE_SUPPORTS_REPARSE_POINTS) &&
             (FileSystemFlagsDst&FILE_SUPPORTS_REPARSE_POINTS) &&
//    ! ������������� ����������� ��������� �������� � ������ �� ���� -
//      ��� ����� � ����� ������� �� ������� (�� ���, ���� ����)
             CDP->IsDTDstFixed && CDP->IsDTSrcFixed)
          {
            *Flags5 &=~ DIF_DISABLE;
            // ��� �������� �� ��������, ����� �� ������ ���� �� ������� �� ������
            // ����� �... ����� ����� � ��������.
            if(*Selected5 || (!*Selected5 && SameDisk))
               *Flags9 &=~ DIF_DISABLE;

            if(!CDP->IsDTDstFixed && SameDisk)
            {
              *Selected5=0;
              *Flags5 |= DIF_DISABLE;
              *Flags9 &=~ DIF_DISABLE;
            }
          }
          else if(NT /* && !NT5 */ && SameDisk)
          {
            *Selected5=0;
            *Flags9 &=~ DIF_DISABLE;
          }
          else
          {
            *Selected5=0;
//            *Flags9 &=~ DIF_DISABLE;
          }
        }
        else if(SameDisk)// && CDP->FSysNTFS) // ��� ����!
        {
          *Selected5=0;
          *Flags9 &=~ DIF_DISABLE;
        }
      }
      else
      {
        if(CDP->FolderPresent)
        {
          if(NT5 && (FileSystemFlagsDst&FILE_SUPPORTS_REPARSE_POINTS))
          {
            *Flags5 &=~ DIF_DISABLE;
            if(!CDP->FilesPresent)
            {
              *Flags9 &=~ DIF_DISABLE;
            }

            if(!CDP->IsDTDstFixed && SameDisk)
            {
              *Selected5=0;
              *Flags5 |= DIF_DISABLE;
              *Flags9 &=~ DIF_DISABLE;
            }
          }
          else if(NT && !NT5 && SameDisk)
          {
            *Selected5=0;
            *Flags9 &=~ DIF_DISABLE;
          }

          if(CDP->FilesPresent && SameDisk)// && CDP->FSysNTFS)
          {
//            *Selected5=0;
            *Flags9 &=~ DIF_DISABLE;
          }
        }
        else if(SameDisk)// && CDP->FSysNTFS) // ��� ����!
        {
          *Selected5=0;
          *Flags9 &=~ DIF_DISABLE;
        }
      }
    }
  }
  else
    return FALSE;
  return TRUE;
}


ShellCopy::~ShellCopy()
{
  _tran(SysLog(L"[%p] ShellCopy::~ShellCopy(), CopyBufer=%p",this,CopyBuffer));
  if ( CopyBuffer )
    delete[] CopyBuffer;

  // $ 26.05.2001 OT ��������� ����������� �������
  _tran(SysLog(L"call (*FrameManager)[0]->UnlockRefresh()"));
  (*FrameManager)[0]->Unlock();
  (*FrameManager)[0]->Refresh();

  if(sddata)
    delete[] sddata;

  if(Filter) // ��������� ������ �������
    delete Filter;
}




COPY_CODES ShellCopy::CopyFileTree(const wchar_t *Dest)
{
  ChangePriority ChPriority(THREAD_PRIORITY_NORMAL);

  //SaveScreen SaveScr;
  DWORD DestAttr=(DWORD)-1;

  string strSelName, strSelShortName;
  int Length,FileAttr;

  if ((Length=StrLength(Dest))==0 || StrCmp(Dest,L".")==0)
    return COPY_FAILURE; //????

  SetCursorType(FALSE,0);

  ShellCopy::Flags&=~(FCOPY_STREAMSKIP|FCOPY_STREAMALL);

  if(TotalCopySize == 0)
  {
    strTotalCopySizeText=L"";

    //  ! �� ��������� �������� ��� �������� ������
    if (ShowTotalCopySize && !(ShellCopy::Flags&FCOPY_LINK) && !CalcTotalSize())
      return COPY_FAILURE;
  }
  else
    CurCopiedSize=0;

  ShellCopyMsg(L"",L"",MSG_LEFTALIGN);

  LastShowTime = 0;

  // �������� ��������� ��������� � ����� ����������
  if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
  {
    //if (Length > 1 && Dest[Length-1]=='\\' && Dest[Length-2]!=':') //??????????
    {
      string strNewPath = Dest;

      wchar_t *lpwszNewPath = strNewPath.GetBuffer ();

      lpwszNewPath=wcsrchr(lpwszNewPath,L'\\');

      if(!lpwszNewPath)
        lpwszNewPath=(wchar_t*)wcsrchr(strNewPath,L'/');

      if(lpwszNewPath)
      {
        *lpwszNewPath=0;

        strNewPath.ReleaseBuffer ();

        if (Opt.CreateUppercaseFolders && !IsCaseMixed(strNewPath))
          strNewPath.Upper ();

        DWORD Attr=GetFileAttributesW(strNewPath);
        if (Attr==0xFFFFFFFF)
        {
          if (CreateDirectoryW(strNewPath,NULL))
            TreeList::AddTreeName(strNewPath);
          else
            CreatePath(strNewPath);
        }
        else if ((Attr & FILE_ATTRIBUTE_DIRECTORY)==0)
        {
          Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),UMSG(MCopyCannotCreateFolder),strNewPath,UMSG(MOk));
          return COPY_FAILURE;
        }
      }
      else
        strNewPath.ReleaseBuffer ();
    }
    DestAttr=GetFileAttributesW(Dest);
  }


  // �������� ������� "��� �� ����"
  int SameDisk=FALSE;
  if (ShellCopy::Flags&FCOPY_MOVE)
  {
    string strSrcDir;
    SrcPanel->GetCurDir(strSrcDir);

    SameDisk=IsSameDisk(strSrcDir,Dest);
  }

  // �������� ���� ����������� ����� ������.
  SetPreRedrawFunc(ShellCopy::PR_ShellCopyMsg);
  SrcPanel->GetSelName(NULL,FileAttr);
  {

  while (SrcPanel->GetSelName(&strSelName,FileAttr,&strSelShortName))
  {
    if (!(ShellCopy::Flags&FCOPY_COPYTONUL))
    {
      string strFullDest = Dest;

      if(wcspbrk(Dest,L"*?")!=NULL)
        ConvertWildcards(strSelName,strFullDest, SelectedFolderNameLength);

      DestAttr=GetFileAttributesW(strFullDest);
      // ������� ������ � ����� ����������
      if ( strDestDriveRoot.IsEmpty() )
      {
        GetPathRoot(strFullDest,strDestDriveRoot);
        DestDriveType=FAR_GetDriveType(wcschr(strFullDest,L'\\')!=NULL ? (const wchar_t*)strDestDriveRoot:NULL);
        if(GetFileAttributesW(strDestDriveRoot) != -1)
          if(!apiGetVolumeInformation(strDestDriveRoot,NULL,NULL,NULL,&DestFSFlags,&strDestFSName))
            strDestFSName=L"";
      }
    }

    string strDestPath = Dest;
    HANDLE FindHandle;
    FAR_FIND_DATA_EX SrcData;
    int CopyCode=COPY_SUCCESS,KeepPathPos;

    ShellCopy::Flags&=~FCOPY_OVERWRITENEXT;

    if ( strSrcDriveRoot.IsEmpty() || StrCmpNI(strSelName,strSrcDriveRoot,(int)strSrcDriveRoot.GetLength())!=0)
    {
      GetPathRoot(strSelName,strSrcDriveRoot);
      SrcDriveType=FAR_GetDriveType(wcschr(strSelName,L'\\')!=NULL ? (const wchar_t*)strSrcDriveRoot:NULL);
      if(GetFileAttributesW(strSrcDriveRoot) != -1)
        if(!apiGetVolumeInformation(strSrcDriveRoot,NULL,NULL,NULL,&SrcFSFlags,&strSrcFSName))
          strSrcFSName=L"";
    }

    if (FileAttr & FILE_ATTRIBUTE_DIRECTORY)
      SelectedFolderNameLength=(int)strSelName.GetLength();
    else
      SelectedFolderNameLength=0;

    // "�������" � ������ ���� ������� - �������� ������ �������, ���������� �� �����
    if(DestDriveType == DRIVE_REMOTE || SrcDriveType == DRIVE_REMOTE)
      ShellCopy::Flags|=FCOPY_COPYSYMLINKCONTENTS;

    KeepPathPos=(int)(PointToName(strSelName)-(const wchar_t*)strSelName);

    if(!StrCmpI(strSrcDriveRoot,strSelName) && (ShellCopy::Flags&FCOPY_CREATESYMLINK)) // �� ������� ��������� �� "��� ������ �����?"
      SrcData.dwFileAttributes=FILE_ATTRIBUTE_DIRECTORY;
    else
    {
      // �������� �� �������� ;-)
      if ((FindHandle=apiFindFirstFile(strSelName,&SrcData))==INVALID_HANDLE_VALUE)
      {
        strDestPath = strSelName;
        ShellCopy::ShellCopyMsg(strSelName,strDestPath,MSG_LEFTALIGN|MSG_KEEPBACKGROUND);
        if (Message(MSG_DOWN|MSG_WARNING,2,UMSG(MError),UMSG(MCopyCannotFind),
                strSelName,UMSG(MSkip),UMSG(MCancel))==1)
        {
          return COPY_FAILURE;
        }
        continue;
      }
      FindClose(FindHandle);
    }

    // ���� ��� ������� � ����� ������� �������...
    if((SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
       (ShellCopy::Flags&FCOPY_CREATESYMLINK)
      )
    {
      /*
      ���� �����, ���� ����� �� ������ ������ �� ������!
      char SrcRealName[NM*2];
      ConvertNameToReal(SelName,SrcRealName,sizeof(SrcRealName));
      switch(MkSymLink(SrcRealName,Dest,ShellCopy::Flags))
      */
      switch(MkSymLink(strSelName,Dest,ShellCopy::Flags))
      {
        case 2:
          break;
        case 1:
            // ������� (Ins) ��������� ���������, ALT-F6 Enter - ��������� � ����� �� �������.
            if ((!(ShellCopy::Flags&FCOPY_CURRENTONLY)) && (ShellCopy::Flags&FCOPY_COPYLASTTIME))
              SrcPanel->ClearLastGetSelection();
            continue;
        case 0:
          return COPY_FAILURE;
      }
    }

    //KeepPathPos=PointToName(SelName)-SelName;

    // �����?
    if ((ShellCopy::Flags&FCOPY_MOVE))
    {
      // ����, � ��� �� ���� "��� �� ����"?
      if (KeepPathPos && PointToName(Dest)==Dest)
      {
        strDestPath = strSelName;

        wchar_t *lpwszDestPath = strDestPath.GetBuffer ((int)(strDestPath.GetLength()+StrLength(Dest)+1));

        wcscpy(lpwszDestPath+KeepPathPos,Dest);

        strDestPath.ReleaseBuffer ();

        SameDisk=TRUE;
      }

      if (!SameDisk || (SrcData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && (ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS))
        CopyCode=COPY_FAILURE;
      else
      {
        CopyCode=ShellCopyOneFile(strSelName,SrcData,strDestPath,KeepPathPos,1);
        if (CopyCode==COPY_SUCCESS_MOVE)
        {
          if ( !strDestDizPath.IsEmpty() )
          {
            if ( !strRenamedName.IsEmpty() )
            {
              DestDiz.DeleteDiz(strSelName,strSelShortName);
              SrcPanel->CopyDiz(strSelName,strSelShortName,strRenamedName,strRenamedName,&DestDiz);
            }
            else
            {
              if ( strCopiedName.IsEmpty() )
                strCopiedName = strSelName;

              SrcPanel->CopyDiz(strSelName,strSelShortName,strCopiedName,strCopiedName,&DestDiz);
              SrcPanel->DeleteDiz(strSelName,strSelShortName);
            }
          }
          continue;
        }

        if (CopyCode==COPY_CANCEL)
          return COPY_CANCEL;

        if (CopyCode==COPY_NEXT)
        {
          unsigned __int64 CurSize = SrcData.nFileSize;
          TotalCopiedSize = TotalCopiedSize - CurCopiedSize + CurSize;
          TotalSkippedSize = TotalSkippedSize + CurSize - CurCopiedSize;
          continue;
        }

        if (!(ShellCopy::Flags&FCOPY_MOVE) || CopyCode==COPY_FAILURE)
          ShellCopy::Flags|=FCOPY_OVERWRITENEXT;
      }
    }

    if (!(ShellCopy::Flags&FCOPY_MOVE) || CopyCode==COPY_FAILURE)
    {
      CopyCode=ShellCopyOneFile(strSelName,SrcData,Dest,KeepPathPos,0);
      ShellCopy::Flags&=~FCOPY_OVERWRITENEXT;

      if (CopyCode==COPY_CANCEL)
        return COPY_CANCEL;

      if (CopyCode!=COPY_SUCCESS)
      {
        unsigned __int64 CurSize = SrcData.nFileSize;
        TotalCopiedSize = TotalCopiedSize - CurCopiedSize + CurSize;
        if (CopyCode == COPY_NEXT)
          TotalSkippedSize = TotalSkippedSize + CurSize - CurCopiedSize;
        continue;
      }
    }

    if (CopyCode==COPY_SUCCESS && !(ShellCopy::Flags&FCOPY_COPYTONUL) && !strDestDizPath.IsEmpty() )
    {
      if ( strCopiedName.IsEmpty() )
        strCopiedName = strSelName;

      SrcPanel->CopyDiz(strSelName,strSelShortName,strCopiedName,strCopiedName,&DestDiz);
    }
#if 0
    // ���� [ ] Copy contents of symbolic links
    if((SrcData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && !(ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS))
    {
      //������� �������
      switch(MkSymLink(SelName,Dest,FCOPY_LINK/*|FCOPY_NOSHOWMSGLINK*/))
      {
        case 2:
          break;
        case 1:
            // ������� (Ins) ��������� ���������, ALT-F6 Enter - ��������� � ����� �� �������.
            if ((!(ShellCopy::Flags&FCOPY_CURRENTONLY)) && (ShellCopy::Flags&FCOPY_COPYLASTTIME))
              SrcPanel->ClearLastGetSelection();
            _LOGCOPYR(SysLog(L"%d continue;",__LINE__));
            continue;
        case 0:
          _LOGCOPYR(SysLog(L"return COPY_FAILURE -> %d",__LINE__));
          return COPY_FAILURE;
      }
      continue;
    }
#endif

    // Mantis#44 - ������ ������ ��� ����������� ������ �� �����
    // ���� ������� (��� ����� ���������� �������) - �������� ���������� ����������...
    if ((SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        (
          !(SrcData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
          (SrcData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && (ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS)
        )
       )
    {
      // �������, �� �������� � ���������� ������: ���� ����������� ������ �������
      // �������� ��� ���������� ������� ��� ���� ����������� �������� � ����������.
      int TryToCopyTree=FALSE,FilesInDir=0;

      int SubCopyCode;
      string strSubName;
      string strFullName;
      ScanTree ScTree(TRUE,TRUE,ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS);

      strSubName = strSelName;
      strSubName += L"\\";
      if (DestAttr==(DWORD)-1)
        KeepPathPos=(int)strSubName.GetLength();

      int NeedRename=!((SrcData.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && (ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS) && (ShellCopy::Flags&FCOPY_MOVE));

      ScTree.SetFindPath(strSubName,L"*.*",FSCANTREE_FILESFIRST);
      while (ScTree.GetNextName(&SrcData,strFullName))
      {
        // ���� ������� ����������� ������� � ���������� ��� ���������� �������
        TryToCopyTree=TRUE;

        /* 23.04.2005 KM
           �������� � ������� �������� �� ��������, ��� ���������� ���, � ����
           ��-�� �������� ������� �� ����������� � ���������, ��� � ��� ���������
           ������ � ������ �� ������ �������. � ��������� �������� ����� � ShellCopyOneFile,
           ���������� ���� �� ����� ��������� � ������ �����.
        */
        if (UseFilter && (SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
          // ������ ���������� ������� ������������ - ���� ������� ������� �
          // ������� ��� ������������, �� ������� ���������� � ��� � �� ���
          // ����������. �� ��������� ���� ������ �� ������� "� ��������"
          if (Filter->FileInFilter(&SrcData, true))
          {
            ScTree.SkipDir();
            continue;
          }
          else
          {
            // ��-�� ���� ��������� ��� Move ��������� ������������� ������� ��
            // ��������� ������� �������, � ������ �������� �� ���������� ����
            // � ������ ����� ������ ��� ���������
            if (!(ShellCopy::Flags&FCOPY_MOVE) || SameDisk || !ScTree.IsDirSearchDone())
              continue;
            if(FilesInDir) goto remove_moved_directory;
          }
        }

        {
          int AttemptToMove=FALSE;
          if ((ShellCopy::Flags&FCOPY_MOVE) && SameDisk && (SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)==0)
          {
            AttemptToMove=TRUE;

            switch(ShellCopyOneFile(strFullName,SrcData,Dest,KeepPathPos,NeedRename)) // 1
            {
              case COPY_CANCEL:
                return COPY_CANCEL;

              case COPY_NEXT:
              {
                unsigned __int64 CurSize = SrcData.nFileSize;
                TotalCopiedSize = TotalCopiedSize - CurCopiedSize + CurSize;
                TotalSkippedSize = TotalSkippedSize + CurSize - CurCopiedSize;
                continue;
              }

              case COPY_SUCCESS_MOVE:
              {
                FilesInDir++;
                continue;
              }

              case COPY_SUCCESS:
                if(!NeedRename) // ������� ��� ����������� ����������� ������� � ������ "���������� ���������� ���..."
                {
                  unsigned __int64 CurSize = SrcData.nFileSize;
                  TotalCopiedSize = TotalCopiedSize - CurCopiedSize + CurSize;
                  TotalSkippedSize = TotalSkippedSize + CurSize - CurCopiedSize;
                  FilesInDir++;
                  continue;     // ...  �.�. �� ��� �� ������, � �����������, �� ���, �� ���� �������� �������� � ���� ������
                }
            }
          }

          int SaveOvrMode=OvrMode;

          if (AttemptToMove)
            OvrMode=1;

          SubCopyCode=ShellCopyOneFile(strFullName,SrcData,Dest,KeepPathPos,0);

          if (AttemptToMove)
            OvrMode=SaveOvrMode;
        }

        if (SubCopyCode==COPY_CANCEL)
          return COPY_CANCEL;

        if (SubCopyCode==COPY_NEXT)
        {
          unsigned __int64 CurSize = SrcData.nFileSize;
          TotalCopiedSize = TotalCopiedSize - CurCopiedSize + CurSize;
          TotalSkippedSize = TotalSkippedSize + CurSize - CurCopiedSize;
        }

        if (SubCopyCode==COPY_SUCCESS)
        {
          if(ShellCopy::Flags&FCOPY_MOVE)
          {
            if (SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
              if (ScTree.IsDirSearchDone() ||
                  ((SrcData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !(ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS)))
              {
                if (SrcData.dwFileAttributes & FA_RDONLY)
                  SetFileAttributesW(strFullName,FILE_ATTRIBUTE_NORMAL);
remove_moved_directory:
                if (apiRemoveDirectory(strFullName))
                  TreeList::DelTreeName(strFullName);
              }
            }
            // ����� ����� �������� �� FSCANTREE_INSIDEJUNCTION, �����
            // ��� ������� ����� �������� �����, ��� ������ �����������!
            else if(!ScTree.InsideJunction())
            {
              if (DeleteAfterMove(strFullName,SrcData.dwFileAttributes)==COPY_CANCEL)
                return COPY_CANCEL;
            }
          }

          FilesInDir++;
        }
      }

      if ((ShellCopy::Flags&FCOPY_MOVE) && CopyCode==COPY_SUCCESS)
      {
        if (FileAttr & FA_RDONLY)
          SetFileAttributesW(strSelName,FILE_ATTRIBUTE_NORMAL);

        if (apiRemoveDirectory(strSelName))
        {
          TreeList::DelTreeName(strSelName);

          if ( !strDestDizPath.IsEmpty() )
            SrcPanel->DeleteDiz(strSelName,strSelShortName);
        }
      }

      /* $ 23.04.2005 KM
         ���� ��������� ������� ����������� �������� � ���������� ���
         ���������� �������, �� �� ���� ����������� �� ������ �����,
         ������� ������ SelName � ��������-��������.
      */
      if (UseFilter && TryToCopyTree && !FilesInDir)
      {
        strDestPath = strSelName;
        apiRemoveDirectory(strDestPath);
      }

    }
    else if ((ShellCopy::Flags&FCOPY_MOVE) && CopyCode==COPY_SUCCESS)
    {
      int DeleteCode;
      if ((DeleteCode=DeleteAfterMove(strSelName,FileAttr))==COPY_CANCEL)
        return COPY_CANCEL;

      if (DeleteCode==COPY_SUCCESS && !strDestDizPath.IsEmpty() )
        SrcPanel->DeleteDiz(strSelName,strSelShortName);
    }

    if ((!(ShellCopy::Flags&FCOPY_CURRENTONLY)) && (ShellCopy::Flags&FCOPY_COPYLASTTIME))
    {
      SrcPanel->ClearLastGetSelection();
    }
  }
  }

  return COPY_SUCCESS; //COPY_SUCCESS_MOVE???
}



// ��������� ����������� �������. ������� ����� �������� �������� ���� �� �����. ���������� ASAP

COPY_CODES ShellCopy::ShellCopyOneFile(
        const wchar_t *Src,
        const FAR_FIND_DATA_EX &SrcData,
        const wchar_t *Dest,
        int KeepPathPos,
        int Rename
        )
{
  string strDestPath;
  DWORD DestAttr=(DWORD)-1;
  HANDLE FindHandle=INVALID_HANDLE_VALUE;
  FAR_FIND_DATA_EX DestData;
  DestData.Clear();

  /* RenameToShortName - ��������� SameName � ���������� ������ ���� �����,
       ����� ������ ����������������� � ��� �� _��������_ ���.  */
  int SameName=0, RenameToShortName=0, Append=0;

  CurCopiedSize = 0; // �������� ������� ��������

  int IsSetSecuty=FALSE;

  if (CheckForEscSilent() && ConfirmAbortOp())
  {
    return(COPY_CANCEL);
  }

  /* ����������� ����� �� ���������� � ����������� ������,
     �������� �� ���������� ������  */
  if (UseFilter)
  {
    // ������ �� �������� ����������� �������� �������� ������������ - ���� ���
    // ������, �� ���������� �������� ���������� � ������� ��� ������������.
    // ������� ��� ��������� ��������� �� ����������� ������� "� ��������"
    bool isDir = (SrcData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) != 0;
    if(Filter->FileInFilter(&SrcData, isDir) == isDir)
      return COPY_NEXT;
  }

  strDestPath = Dest;

  SetPreRedrawFunc(ShellCopy::PR_ShellCopyMsg);
  ConvertWildcards(Src, strDestPath, SelectedFolderNameLength); //BUGBUG, to check!!

  const wchar_t *NamePtr=PointToName(strDestPath);

  DestAttr=(DWORD)-1;

  if (strDestPath.At(0)=='\\' && strDestPath.At(1)=='\\')
  {
    string strRoot;

    GetPathRoot(strDestPath, strRoot);

    if (strRoot.GetLength()>0 && strRoot.At(strRoot.GetLength()-1)=='\\')
      strRoot.SetLength(strRoot.GetLength()-1);

    if (StrCmp(strDestPath,strRoot)==0)
      DestAttr=FILE_ATTRIBUTE_DIRECTORY;
  }

  if (*NamePtr==0 || TestParentFolderName(NamePtr))
    DestAttr=FILE_ATTRIBUTE_DIRECTORY;

  if (DestAttr==(DWORD)-1)
  {
    if ( apiGetFindDataEx (strDestPath,&DestData) )
      DestAttr=DestData.dwFileAttributes;
  }

  if (DestAttr!=(DWORD)-1 && (DestAttr & FILE_ATTRIBUTE_DIRECTORY))
  {
    int CmpCode;

    if ((CmpCode=CmpFullNames(Src,strDestPath))!=0)
    {
      SameName=1;

      if(CmpCode!=2 && Rename)
      {
         if(!StrCmp(PointToName(Src),PointToName(strDestPath)))
           CmpCode=2; // ������: ����� ��� ��������� �������
         else
           RenameToShortName = (!StrCmpI(DestData.strFileName, SrcData.strFileName) &&
                               0!=StrCmpI(DestData.strAlternateFileName,SrcData.strFileName));
      }
      if (CmpCode==2 || !Rename)
      {
        SetMessageHelp(L"ErrCopyItSelf");
        Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),UMSG(MCannotCopyFolderToItself1),
                Src,UMSG(MCannotCopyFolderToItself2),UMSG(MOk));
        return(COPY_CANCEL);
      }
    }

    if (!SameName)
    {
      int Length=(int)strDestPath.GetLength();

      if (strDestPath.At(Length-1)!=L'\\' && strDestPath.At(Length-1)!=L':')
        strDestPath += L"\\";

      const wchar_t *PathPtr=Src+KeepPathPos;

      if (*PathPtr && KeepPathPos==0 && PathPtr[1]==L':')
        PathPtr+=2;

      if (*PathPtr==L'\\')
        PathPtr++;

      /* $ 23.04.2005 KM
          ��������� �������� � ������� ���������� ���������� ����������� ���������,
          ����� �� ������� ������ �������� ��-�� ���������� � ������ ������,
          �� �������� �������� ����� ����������� ��� ����������� �����, ���������
          � ������, � ��� ����� ������ �������� ����������� �������� � ���������
          �� �� ����������� �������.
      */
      if (UseFilter)
      {
        //char OldPath[2*NM],NewPath[2*NM];

        string strOldPath, strNewPath;
        const wchar_t *path=PathPtr,*p1=NULL;

        while (p1=wcschr(path,L'\\'))
        {
          DWORD FileAttr=(DWORD)-1;
          FAR_FIND_DATA_EX FileData;
          FileData.Clear();

          strOldPath = Src;
          strOldPath.SetLength(p1-Src);

          apiGetFindDataEx (strOldPath,&FileData);
          FileAttr=FileData.dwFileAttributes;

          // �������� ��� ��������, ������� ������ ����� �������, ���� ������� ��� ��� ���
          strNewPath = strDestPath;
          {
            string tmp(PathPtr);
            tmp.SetLength(p1 - PathPtr);
            strNewPath += tmp;
          }

          // ������ �������� ��� ���, �������� ���
          if ((FindHandle=apiFindFirstFile(strNewPath,&FileData))==INVALID_HANDLE_VALUE)
          {
            int CopySecurity = ShellCopy::Flags&FCOPY_COPYSECURITY;
            SECURITY_ATTRIBUTES sa;

            if ((CopySecurity) && !GetSecurity(strOldPath,sa))
              CopySecurity = FALSE;

            // ���������� �������� ��������
            if (CreateDirectoryW(strNewPath,CopySecurity?&sa:NULL))
            {
              // ���������, ������� �������
              if (FileAttr!=(DWORD)-1)
                // ������ ��������� ��������. ����������������� �������� �� ���������
                // ��� ��� �������� "�����", �� ���� �������� � ��������� ��������
                ShellSetAttr(strNewPath,FileAttr);
            }
            else
            {
              // ��-��-��. ������� �� ������ �������! ������ ����� ��������!
              int MsgCode;
              MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,3,UMSG(MError),
                              UMSG(MCopyCannotCreateFolder),strNewPath,UMSG(MCopyRetry),
                              UMSG(MCopySkip),UMSG(MCopyCancel));

              if (MsgCode!=0)
              {
                // �� ��� �, ��� ���� ������ ��� ������� �������� ��������, ������� ������
                return((MsgCode==-2 || MsgCode==2) ? COPY_CANCEL:COPY_NEXT);
              }

              // ������� ����� ���������� ������� �������� ��������. �� ����� � ������ ����
              continue;
            }
          }
          else
            // �����. ������� ��� ������� - ���������. ������ ���������� ����������� ������.
            FindClose(FindHandle);

          // �� ����� �� �������� �����
          if (*p1==L'\\')
            p1++;

          // ������ ��������� ����� � ����� �������� �� �������� ������,
          // ��� ���� ����� ��������� �, ��������, ������� ��������� �������,
          // ����������� � ���������� ����� �����.
          path=p1;
        }
      }

      strDestPath += PathPtr;

      if ((FindHandle=apiFindFirstFile(strDestPath,&DestData))==INVALID_HANDLE_VALUE)
        DestAttr=(DWORD)-1;
      else
      {
        FindClose(FindHandle);
        DestAttr=DestData.dwFileAttributes;
      }
    }
  }

  if (!(ShellCopy::Flags&FCOPY_COPYTONUL) && StrCmpI(strDestPath,L"prn")!=0)
    SetDestDizPath(strDestPath);

  ShellCopyMsg(Src,strDestPath,MSG_LEFTALIGN|MSG_KEEPBACKGROUND);

  if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
  {
    // �������� ���������� ��������� �� ������
    switch(CheckStreams(Src,strDestPath))
    {
      case COPY_NEXT:
        return COPY_NEXT;
      case COPY_CANCEL:
        return COPY_CANCEL;
    }

    if (SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      if (!Rename)
        strCopiedName = PointToName(strDestPath);

      if (DestAttr!=(DWORD)-1 && !RenameToShortName)
      {
        if ((DestAttr & FILE_ATTRIBUTE_DIRECTORY) && !SameName)
        {
          DWORD SetAttr=SrcData.dwFileAttributes;
          if (IsDriveTypeCDROM(SrcDriveType) && Opt.ClearReadOnly && (SetAttr & FA_RDONLY))
            SetAttr&=~FA_RDONLY;

          if (SetAttr!=DestAttr)
            ShellSetAttr(strDestPath,SetAttr);

          string strSrcFullName;

          ConvertNameToFull(Src,strSrcFullName);

          return(StrCmp(strDestPath,strSrcFullName)==0 ? COPY_NEXT:COPY_SUCCESS);
        }

        int Type=GetFileTypeByName(strDestPath);
        if (Type==FILE_TYPE_CHAR || Type==FILE_TYPE_PIPE)
          return(Rename ? COPY_NEXT:COPY_SUCCESS);
      }

      if (Rename)
      {
        string strSrcFullName,strDestFullName;

        ConvertNameToFull (Src,strSrcFullName);

        SECURITY_ATTRIBUTES sa;

        // ��� Move ��� ���������� ������ ������� ��������, ����� �������� ��� ���������
        if (!(ShellCopy::Flags&(FCOPY_COPYSECURITY|FCOPY_LEAVESECURITY)))
        {
          IsSetSecuty=FALSE;
          if(CmpFullPath(Src,Dest)) // � �������� ������ �������� ������ �� ������
            IsSetSecuty=FALSE;
          else if(GetFileAttributesW(Dest) == (DWORD)-1) // ���� �������� ���...
          {
            // ...�������� ��������� ��������
            if(GetSecurity(GetParentFolder(Dest,strDestFullName), sa))
              IsSetSecuty=TRUE;
          }
          else if(GetSecurity(Dest,sa)) // ����� �������� ��������� Dest`�
            IsSetSecuty=TRUE;
        }

        // �������� �������������, ���� �� �������
        while (1)
        {
          BOOL SuccessMove=RenameToShortName?MoveFileThroughTemp(Src,strDestPath):apiMoveFile(Src,strDestPath);

          if (SuccessMove)
          {
            if(IsSetSecuty)// && WinVer.dwPlatformId==VER_PLATFORM_WIN32_NT && !strcmp(DestFSName,"NTFS"))
                SetRecursiveSecurity(strDestPath,sa);

            if (PointToName(strDestPath)==(const wchar_t*)strDestPath)
              strRenamedName = strDestPath;
            else
              strCopiedName = PointToName(strDestPath);

            ConvertNameToFull (Dest, strDestFullName);

            TreeList::RenTreeName(strSrcFullName,strDestFullName);

            return(SameName ? COPY_NEXT:COPY_SUCCESS_MOVE);
          }
          else
          {
            int LastError = GetLastError();
            int CopySecurity = ShellCopy::Flags&FCOPY_COPYSECURITY;
            SECURITY_ATTRIBUTES sa;
            if ((CopySecurity) && !GetSecurity(Src,sa))
              CopySecurity = FALSE;
            if (CreateDirectoryW(strDestPath,CopySecurity?&sa:NULL))
            {
              if (PointToName(strDestPath)==(const wchar_t*)strDestPath)
                strRenamedName = strDestPath;
              else
                strCopiedName = PointToName(strDestPath);

              TreeList::AddTreeName(strDestPath);
              return(COPY_SUCCESS);
            }
            else
            {
              SetLastError(LastError);
              int MsgCode = Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,3,UMSG(MError),
                                    UMSG(MCopyCannotRenameFolder),Src,UMSG(MCopyRetry),
                                    UMSG(MCopyIgnore),UMSG(MCopyCancel));
              switch (MsgCode)
              {
                case 0:  continue;
                case 1:
                  return (COPY_FAILURE);
                default:
                  return (COPY_CANCEL);
              } /* switch */
            } /* else */
          } /* else */
        } /* while */
      } // if (Rename)

      SECURITY_ATTRIBUTES sa;
      if ((ShellCopy::Flags&FCOPY_COPYSECURITY) && !GetSecurity(Src,sa))
        return COPY_CANCEL;

      while (!CreateDirectoryW(strDestPath,(ShellCopy::Flags&FCOPY_COPYSECURITY) ? &sa:NULL))
      {
        int MsgCode;
        MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,3,UMSG(MError),
                        UMSG(MCopyCannotCreateFolder),strDestPath,UMSG(MCopyRetry),
                        UMSG(MCopySkip),UMSG(MCopyCancel));

        if (MsgCode!=0)
          return((MsgCode==-2 || MsgCode==2) ? COPY_CANCEL:COPY_NEXT);
      }

      DWORD SetAttr=SrcData.dwFileAttributes;

      if (IsDriveTypeCDROM(SrcDriveType) && Opt.ClearReadOnly && (SetAttr & FA_RDONLY))
        SetAttr&=~FA_RDONLY;

      if( !(ShellCopy::Flags & FCOPY_SKIPSETATTRFLD) &&
           ((SetAttr & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) )
      {
        // �� ����� ���������� ����������, ���� ������� � �������
        // � ������������ FILE_ATTRIBUTE_ENCRYPTED (� �� ��� ����� ��������� ����� CreateDirectory)
        // �.�. ���������� ������ ���.
        if(GetFileAttributesW(strDestPath)&FILE_ATTRIBUTE_ENCRYPTED)
          SetAttr&=~FILE_ATTRIBUTE_COMPRESSED;

        if(SetAttr&FILE_ATTRIBUTE_COMPRESSED)
        {
          while(1)
          {
            int MsgCode=ESetFileCompression(strDestPath,1,0);
            if(MsgCode)
            {
              if(MsgCode == 2)
                ShellCopy::Flags|=FCOPY_SKIPSETATTRFLD;
              break;
            }
            if(MsgCode != 1)
              return (MsgCode==2) ? COPY_NEXT:COPY_CANCEL;
          }
        }

        while(!ShellSetAttr(strDestPath,SetAttr))
        {
          int MsgCode;
          MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,4,UMSG(MError),
                          UMSG(MCopyCannotChangeFolderAttr),strDestPath,
                          UMSG(MCopyRetry),UMSG(MCopySkip),UMSG(MCopySkipAll),UMSG(MCopyCancel));

          if (MsgCode!=0)
          {
            if(MsgCode==1)
              break;
            if(MsgCode==2)
            {
              ShellCopy::Flags|=FCOPY_SKIPSETATTRFLD;
              break;
            }
            apiRemoveDirectory(strDestPath);
            return((MsgCode==-2 || MsgCode==3) ? COPY_CANCEL:COPY_NEXT);
          }
        }
      }
      else if((SetAttr & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
      {
        while(!ShellSetAttr(strDestPath,SetAttr))
        {
          int MsgCode;
          MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,4,UMSG(MError),
                          UMSG(MCopyCannotChangeFolderAttr),strDestPath,
                          UMSG(MCopyRetry),UMSG(MCopySkip),UMSG(MCopySkipAll),UMSG(MCopyCancel));

          if (MsgCode!=0)
          {
            if(MsgCode==1)
              break;
            if(MsgCode==2)
            {
              ShellCopy::Flags|=FCOPY_SKIPSETATTRFLD;
              break;
            }
            apiRemoveDirectory(strDestPath);
            return((MsgCode==-2 || MsgCode==3) ? COPY_CANCEL:COPY_NEXT);
          }
        }
      }
      // ��� ���������, �������� ���� �������� - �������� �������
      // ���� [ ] Copy contents of symbolic links
      if(SrcData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && !(ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS))
      {
        string strSrcRealName;
        ConvertNameToFull (Src,strSrcRealName);
        switch(MkSymLink(strSrcRealName, strDestPath,FCOPY_LINK))
        {
          case 2:
            return COPY_CANCEL;
          case 1:
            break;
          case 0:
            return COPY_FAILURE;
        }
      }

      TreeList::AddTreeName(strDestPath);
      return COPY_SUCCESS;
    }

    if (DestAttr!=(DWORD)-1 && (DestAttr & FILE_ATTRIBUTE_DIRECTORY)==0)
    {
      if(!RenameToShortName)
      {
        if (SrcData.nFileSize==DestData.nFileSize)
        {
          int CmpCode;

          if ((CmpCode=CmpFullNames(Src,strDestPath))!=0)
          {
            SameName=1;

            if(CmpCode!=2 && Rename)
            {
               if(!StrCmp(PointToName(Src),PointToName(strDestPath)))
                 CmpCode=2; // ������: ����� ��� ��������� �������
               else
               {
                 RenameToShortName = (!StrCmpI(DestData.strFileName,
                   SrcData.strFileName) &&
                   0!=StrCmpI(DestData.strAlternateFileName,SrcData.strFileName));
               }
            }

            if (CmpCode==2 || !Rename)
            {
              Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),UMSG(MCannotCopyFileToItself1),
                      Src,UMSG(MCannotCopyFileToItself2),UMSG(MOk));
              return(COPY_CANCEL);
            }
          }
        }

        int RetCode;
        if (!AskOverwrite(SrcData,strDestPath,DestAttr,SameName,Rename,((ShellCopy::Flags&FCOPY_LINK)?0:1),Append,RetCode))
        {
          return((COPY_CODES)RetCode);
        }
      }
    }
  }
  else
  {
    if (SrcData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      return COPY_SUCCESS;
    }
  }

  int NWFS_Attr=(Opt.Nowell.MoveRO && !StrCmp(strDestFSName,L"NWFS"))?TRUE:FALSE;
  {
  while (1)
  {
    int CopyCode=0;
    unsigned __int64 SaveTotalSize=TotalCopiedSize;
    if (!(ShellCopy::Flags&FCOPY_COPYTONUL) && Rename)
    {
      int MoveCode=FALSE,AskDelete;

      if ((WinVer.dwPlatformId!=VER_PLATFORM_WIN32_NT || !StrCmp(strDestFSName,L"NWFS")) && !Append &&
          DestAttr!=(DWORD)-1 && !SameName &&
          !RenameToShortName) // !!!
      {
        _wremove (strDestPath); //BUGBUG
      }

      if (!Append)
      {
        string strSrcFullName;
        ConvertNameToFull(Src,strSrcFullName);

        if (NWFS_Attr)
          SetFileAttributesW(strSrcFullName,SrcData.dwFileAttributes&(~FA_RDONLY));

        SECURITY_ATTRIBUTES sa;
        IsSetSecuty=FALSE;

        // ��� Move ��� ���������� ������ ������� ��������, ����� �������� ��� ���������
        if (Rename && !(ShellCopy::Flags&(FCOPY_COPYSECURITY|FCOPY_LEAVESECURITY)))
        {
          if(CmpFullPath(Src,Dest)) // � �������� ������ �������� ������ �� ������
            IsSetSecuty=FALSE;
          else if(GetFileAttributesW(Dest) == (DWORD)-1) // ���� �������� ���...
          {
            string strDestFullName;
            // ...�������� ��������� ��������
            if(GetSecurity(GetParentFolder(Dest,strDestFullName),sa))
              IsSetSecuty=TRUE;
          }
          else if(GetSecurity(Dest,sa)) // ����� �������� ��������� Dest`�
            IsSetSecuty=TRUE;
        }

        if(RenameToShortName)
          MoveCode=MoveFileThroughTemp(strSrcFullName, strDestPath);
        else
        {
          if (WinVer.dwPlatformId!=VER_PLATFORM_WIN32_NT || !StrCmp(strDestFSName,L"NWFS"))
            MoveCode=apiMoveFile(strSrcFullName,strDestPath);
          else
            MoveCode=apiMoveFileEx(strSrcFullName,strDestPath,SameName ? MOVEFILE_COPY_ALLOWED:MOVEFILE_COPY_ALLOWED|MOVEFILE_REPLACE_EXISTING);
        }

        if (!MoveCode)
        {
          int MoveLastError=GetLastError();
          if (NWFS_Attr)
            SetFileAttributesW(strSrcFullName,SrcData.dwFileAttributes);

          if(MoveLastError==ERROR_NOT_SAME_DEVICE)
            return COPY_FAILURE;

          SetLastError(MoveLastError);
        }
        else
        {
          if (IsSetSecuty)
            SetSecurity(strDestPath,sa);
        }

        if (NWFS_Attr)
          SetFileAttributesW(strDestPath,SrcData.dwFileAttributes);

        if (ShowTotalCopySize && MoveCode)
        {
          unsigned __int64 AddSize = SrcData.nFileSize;
          TotalCopiedSize+=AddSize;
          ShowBar(TotalCopiedSize,TotalCopySize,true);
          ShowTitle(FALSE);
        }
        AskDelete=0;
      }
      else
      {
        CopyCode=ShellCopyFile(Src,SrcData,strDestPath,(DWORD)-1,Append);

        switch(CopyCode)
        {
          case COPY_SUCCESS:
            MoveCode=TRUE;
            break;
          case COPY_FAILUREREAD:
          case COPY_FAILURE:
            MoveCode=FALSE;
            break;
          case COPY_CANCEL:
            return COPY_CANCEL;
          case COPY_NEXT:
            return COPY_NEXT;
        }
        AskDelete=1;
      }

      if (MoveCode)
      {
        if (DestAttr==(DWORD)-1 || (DestAttr & FILE_ATTRIBUTE_DIRECTORY)==0)
        {
          if (PointToName(strDestPath)==(const wchar_t*)strDestPath)
            strRenamedName = strDestPath;
          else
            strCopiedName = PointToName(strDestPath);
        }

        if (IsDriveTypeCDROM(SrcDriveType) && Opt.ClearReadOnly &&
            (SrcData.dwFileAttributes & FA_RDONLY))
          ShellSetAttr(strDestPath,SrcData.dwFileAttributes & (~FA_RDONLY));

        TotalFiles++;
        if (AskDelete && DeleteAfterMove(Src,SrcData.dwFileAttributes)==COPY_CANCEL)
          return COPY_CANCEL;

        return(COPY_SUCCESS_MOVE);
      }
    }
    else
    {
      CopyCode=ShellCopyFile(Src,SrcData,strDestPath,DestAttr,Append);

      if (CopyCode==COPY_SUCCESS)
      {
        strCopiedName = PointToName(strDestPath);
        if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
        {
          if (IsDriveTypeCDROM(SrcDriveType) && Opt.ClearReadOnly &&
              (SrcData.dwFileAttributes & FA_RDONLY))
            ShellSetAttr(strDestPath,SrcData.dwFileAttributes & ~FA_RDONLY);

          if (DestAttr!=(DWORD)-1 && StrCmpI(strCopiedName,DestData.strFileName)==0 &&
              StrCmp(strCopiedName,DestData.strFileName)!=0)
            apiMoveFile(strDestPath,strDestPath); //???
        }

        TotalFiles++;
        if(DestAttr!=-1 && Append)
          SetFileAttributesW(strDestPath,DestAttr);

        return COPY_SUCCESS;
      }
      else if (CopyCode==COPY_CANCEL || CopyCode==COPY_NEXT)
      {
        if(DestAttr!=-1 && Append)
          SetFileAttributesW(strDestPath,DestAttr);
        return((COPY_CODES)CopyCode);
      }

      if(DestAttr!=-1 && Append)
        SetFileAttributesW(strDestPath,DestAttr);
    }
    //????
    if(CopyCode == COPY_FAILUREREAD)
      return COPY_FAILURE;
    //????

    //char Msg1[2*NM],Msg2[2*NM];
    string strMsg1, strMsg2;
    int MsgMCannot=(ShellCopy::Flags&FCOPY_LINK) ? MCannotLink: (ShellCopy::Flags&FCOPY_MOVE) ? MCannotMove: MCannotCopy;

    strMsg1 = Src;
    strMsg2 = strDestPath;

    TruncPathStr(strMsg1, 64);
    TruncPathStr(strMsg2, 64);

    InsertQuote(strMsg1);
    InsertQuote(strMsg2);

    {
      int MsgCode;
      if((SrcData.dwFileAttributes&FILE_ATTRIBUTE_ENCRYPTED))
      {
        if (SkipEncMode!=-1)
        {
          MsgCode=SkipEncMode;
          if(SkipEncMode == 1)
            ShellCopy::Flags|=FCOPY_DECRYPTED_DESTINATION;
        }
        else
        {
          if(_localLastError == 5)
          {
            #define ERROR_EFS_SERVER_NOT_TRUSTED     6011L
            ;//SetLastError(_localLastError=(DWORD)0x80090345L);//SEC_E_DELEGATION_REQUIRED);
            SetLastError(_localLastError=ERROR_EFS_SERVER_NOT_TRUSTED);
          }

          MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,5,UMSG(MError),
                          UMSG(MsgMCannot),
                          strMsg1,
                          UMSG(MCannotCopyTo),
                          strMsg2,
                          UMSG(MCopyDecrypt),
                          UMSG(MCopyDecryptAll),
                          UMSG(MCopySkip),
                          UMSG(MCopySkipAll),
                          UMSG(MCopyCancel));

          switch(MsgCode)
          {
            case  0:
              ShellCopy::Flags|=FCOPY_DECRYPTED_DESTINATION;
              break;//return COPY_NEXT;
            case  1:
              SkipEncMode=1;
              ShellCopy::Flags|=FCOPY_DECRYPTED_DESTINATION;
              break;//return COPY_NEXT;
            case  2:
              return COPY_NEXT;
            case  3:
              SkipMode=1;
              return COPY_NEXT;
            case -1:
            case -2:
            case  4:
              return COPY_CANCEL;
          }
        }
      }
      else
      {
        if (SkipMode!=-1)
          MsgCode=SkipMode;
        else
        {
          MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,4,UMSG(MError),
                          UMSG(MsgMCannot),
                          strMsg1,
                          UMSG(MCannotCopyTo),
                          strMsg2,
                          UMSG(MCopyRetry),UMSG(MCopySkip),
                          UMSG(MCopySkipAll),UMSG(MCopyCancel));
        }

        switch(MsgCode)
        {
          case -1:
          case  1:
            return COPY_NEXT;
          case  2:
            SkipMode=1;
            return COPY_NEXT;
          case -2:
          case  3:
            return COPY_CANCEL;
        }
      }
    }

//    CurCopiedSize=SaveCopiedSize;
    TotalCopiedSize=SaveTotalSize;
    int RetCode;
    if (!AskOverwrite(SrcData,strDestPath,DestAttr,SameName,Rename,((ShellCopy::Flags&FCOPY_LINK)?0:1),Append,RetCode))
      return((COPY_CODES)RetCode);
  }
  }
}


// �������� ���������� ��������� �� ������
COPY_CODES ShellCopy::CheckStreams(const wchar_t *Src,const wchar_t *DestPath)
{
#if 0
  int AscStreams=(ShellCopy::Flags&FCOPY_STREAMSKIP)?2:((ShellCopy::Flags&FCOPY_STREAMALL)?0:1);
  if(!(ShellCopy::Flags&FCOPY_USESYSTEMCOPY) && NT && AscStreams)
  {
    int CountStreams=EnumNTFSStreams(Src,NULL,NULL);
    if(CountStreams > 1 ||
       (CountStreams >= 1 && (GetFileAttributes(Src)&FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY))
    {
      if(AscStreams == 2)
      {
        return(COPY_NEXT);
      }

      SetMessageHelp("WarnCopyStream");
      //char SrcFullName[NM];
      //ConvertNameToFull(Src,SrcFullName, sizeof(SrcFullName));
      //TruncPathStr(SrcFullName,ScrX-16);
      int MsgCode=Message(MSG_DOWN|MSG_WARNING,5,MSG(MWarning),
              MSG(MCopyStream1),
              MSG(CanCreateHardLinks(DestPath,NULL)?MCopyStream2:MCopyStream3),
              MSG(MCopyStream4),"\1",//SrcFullName,"\1",
              MSG(MCopyResume),MSG(MCopyOverwriteAll),MSG(MCopySkipOvr),MSG(MCopySkipAllOvr),MSG(MCopyCancelOvr));
      switch(MsgCode)
      {
        case 0: break;
        case 1: ShellCopy::Flags|=FCOPY_STREAMALL; break;
        case 2: return(COPY_NEXT);
        case 3: ShellCopy::Flags|=FCOPY_STREAMSKIP; return(COPY_NEXT);
        default:
          return COPY_CANCEL;
      }
    }
  }
#endif
    return COPY_SUCCESS;
}


void ShellCopy::PR_ShellCopyMsg(void)
{
  LastShowTime = 0;
  ((ShellCopy*)PreRedrawParam.Param1)->ShellCopyMsg((wchar_t*)PreRedrawParam.Param2,(wchar_t*)PreRedrawParam.Param3,PreRedrawParam.Flags&(~MSG_KEEPBACKGROUND));
}


void ShellCopy::ShellCopyMsg(const wchar_t *Src,const wchar_t *Dest,int Flags)
{
  wchar_t FilesStr[100],BarStr[100]; //BUGBUG, dynamic

  string strSrcName, strDestName;

  #define BAR_SIZE  46
  static wchar_t Bar[BAR_SIZE+2]={0}; //BUGBUG
  if(!Bar[0])
  {
    for (int i = 0; i < BAR_SIZE; i++)
      Bar[i] = 0x2500;
  }

  wcscpy(BarStr,Bar); //BUGBUG

  if (ShowTotalCopySize)
  {
    int nLength = (int)(wcslen (UMSG(MCopyDlgTotal))+strTotalCopySizeText.GetLength()+4+1);

    wchar_t *wszTotalMsg = (wchar_t*)xf_malloc (nLength*sizeof (wchar_t));

    if ( !strTotalCopySizeText.IsEmpty() ) //BUGBUG, but really not used
      swprintf(wszTotalMsg,L" %s: %s ",UMSG(MCopyDlgTotal),(const wchar_t*)strTotalCopySizeText);
    else
      swprintf(wszTotalMsg, L" %s ", UMSG(MCopyDlgTotal));

    int TotalLength=StrLength(wszTotalMsg);
    memcpy(BarStr+(StrLength(BarStr)-TotalLength+1)/2,wszTotalMsg,(TotalLength)*sizeof (wchar_t));
//    *FilesStr=0;

    swprintf (FilesStr, UMSG(MCopyProcessedTotal),TotalFiles, TotalFilesToProcess);

    xf_free (wszTotalMsg);
  }
  else
  {
    swprintf(FilesStr,UMSG(MCopyProcessed),TotalFiles);

    if ((Src!=NULL) && (ShowCopyTime))
    {
      CopyStartTime = clock();
      LastShowTime = 0;
      WaitUserTime = OldCalcTime = 0;
    }
  }

  if (Src!=NULL)
  {
    strSrcName.Format (L"%-*s",BAR_SIZE,Src);
    TruncPathStr(strSrcName, BAR_SIZE);
  }

  strDestName.Format (L"%-*s", BAR_SIZE, Dest);
  TruncPathStr(strDestName, BAR_SIZE);

  SetMessageHelp(L"CopyFiles");

  if (Src==NULL)
    Message(Flags,0,(ShellCopy::Flags&FCOPY_MOVE) ? UMSG(MMoveDlgTitle):
                       UMSG(MCopyDlgTitle),
                       L"",UMSG(MCopyScanning),
                       strDestName,L"",L"",BarStr,L"");
  else
  {
    int Move = ShellCopy::Flags&FCOPY_MOVE;

    if ( ShowTotalCopySize )
    {
      if ( ShowCopyTime )
        Message(Flags, 0, UMSG(Move?MMoveDlgTitle:MCopyDlgTitle),UMSG(Move?MCopyMoving:MCopyCopying),strSrcName,UMSG(MCopyTo),strDestName,L"",BarStr,L"",Bar,FilesStr,Bar,L"");
      else
        Message(Flags, 0, UMSG(Move?MMoveDlgTitle:MCopyDlgTitle),UMSG(Move?MCopyMoving:MCopyCopying),strSrcName,UMSG(MCopyTo),strDestName,L"",BarStr,L"",Bar,FilesStr);
    }
    else
    {
      if ( ShowCopyTime )
        Message(Flags, 0, UMSG(Move?MMoveDlgTitle:MCopyDlgTitle),UMSG(Move?MCopyMoving:MCopyCopying),strSrcName,UMSG(MCopyTo),strDestName,L"",BarStr,FilesStr,Bar,L"");
      else
        Message(Flags, 0, UMSG(Move?MMoveDlgTitle:MCopyDlgTitle),UMSG(Move?MCopyMoving:MCopyCopying),strSrcName,UMSG(MCopyTo),strDestName,L"",BarStr,FilesStr);
    }
  }

  int MessageX1,MessageY1,MessageX2,MessageY2;
  GetMessagePosition(MessageX1,MessageY1,MessageX2,MessageY2);
  BarX=MessageX1+5;
  BarY=MessageY1+6;
  BarLength=MessageX2-MessageX1-9-5; //-5 ��� ���������

  if (Src!=NULL)
  {
    // // _LOGCOPYR(SysLog(L" ******************  ShowTotalCopySize=%d",ShowTotalCopySize));
    ShowBar(0,0,false);
    if (ShowTotalCopySize)
    {
      ShowBar(TotalCopiedSize,TotalCopySize,true);
      ShowTitle(FALSE);
    }
  }
  PreRedrawParam.Flags=Flags;
  PreRedrawParam.Param1=this;
  PreRedrawParam.Param2=Src;
  PreRedrawParam.Param3=Dest;
  // // _LOGCOPYR(SysLog(L"@@ShellCopyMsg 2='%s'/0x%08X  3='%s'/0x%08X  Flags=0x%08X",(char*)PreRedrawParam.Param2,PreRedrawParam.Param2,(char*)PreRedrawParam.Param3,PreRedrawParam.Param3,PreRedrawParam.Flags));
}


int ShellCopy::DeleteAfterMove(const wchar_t *Name,int Attr)
{
  if (Attr & FA_RDONLY)
  {
    int MsgCode;
    if (ReadOnlyDelMode!=-1)
      MsgCode=ReadOnlyDelMode;
    else
      MsgCode=Message(MSG_DOWN|MSG_WARNING,5,UMSG(MWarning),
              UMSG(MCopyFileRO),Name,UMSG(MCopyAskDelete),
              UMSG(MCopyDeleteRO),UMSG(MCopyDeleteAllRO),
              UMSG(MCopySkipRO),UMSG(MCopySkipAllRO),UMSG(MCopyCancelRO));
    switch(MsgCode)
    {
      case 1:
        ReadOnlyDelMode=1;
        break;
      case 2:
        return(COPY_NEXT);
      case 3:
        ReadOnlyDelMode=3;
        return(COPY_NEXT);
      case -1:
      case -2:
      case 4:
        return(COPY_CANCEL);
    }
    SetFileAttributesW(Name,FILE_ATTRIBUTE_NORMAL);
  }
  while ( _wremove(Name) )
  {
    int MsgCode;
    MsgCode=Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,3,UMSG(MError),
                    UMSG(MCannotDeleteFile),Name,UMSG(MDeleteRetry),
                    UMSG(MDeleteSkip),UMSG(MDeleteCancel));
    if (MsgCode==1 || MsgCode==-1)
      break;
    if (MsgCode==2 || MsgCode==-2)
      return(COPY_CANCEL);
  }
  return(COPY_SUCCESS);
}



int ShellCopy::ShellCopyFile(const wchar_t *SrcName,const FAR_FIND_DATA_EX &SrcData,
                             const wchar_t *DestName,DWORD DestAttr,int Append)
{
  OrigScrX=ScrX;
  OrigScrY=ScrY;

  SetPreRedrawFunc(ShellCopy::PR_ShellCopyMsg);

  if ((ShellCopy::Flags&FCOPY_LINK))
  {
    _wremove(DestName); //BUGBUG
    return(MkLink(SrcName,DestName) ? COPY_SUCCESS:COPY_FAILURE);
  }

  if((SrcData.dwFileAttributes&FILE_ATTRIBUTE_ENCRYPTED) &&
     !CheckDisksProps(SrcName,DestName,CHECKEDPROPS_ISDST_ENCRYPTION)
    )
  {
    int MsgCode;
    if (SkipEncMode!=-1)
    {
      MsgCode=SkipEncMode;
      if(SkipEncMode == 1)
        ShellCopy::Flags|=FCOPY_DECRYPTED_DESTINATION;
    }
    else
    {
      SetMessageHelp(L"WarnCopyEncrypt");

      string strTruncSrcName = SrcName;
      InsertQuote(TruncPathStr(strTruncSrcName,64));
      MsgCode=Message(MSG_DOWN|MSG_WARNING,3,UMSG(MWarning),
                      UMSG(MCopyEncryptWarn1),
                      strTruncSrcName,
                      UMSG(MCopyEncryptWarn2),
                      UMSG(MCopyEncryptWarn3),
                      UMSG(MCopyIgnore),UMSG(MCopyIgnoreAll),UMSG(MCopyCancel));
    }

    switch(MsgCode)
    {
      case  0:
        _LOGCOPYR(SysLog(L"return COPY_NEXT -> %d",__LINE__));
        ShellCopy::Flags|=FCOPY_DECRYPTED_DESTINATION;
        break;//return COPY_NEXT;
      case  1:
        SkipEncMode=1;
        ShellCopy::Flags|=FCOPY_DECRYPTED_DESTINATION;
        _LOGCOPYR(SysLog(L"return COPY_NEXT -> %d",__LINE__));
        break;//return COPY_NEXT;
      case -1:
      case -2:
      case  2:
        _LOGCOPYR(SysLog(L"return COPY_CANCEL -> %d",__LINE__));
        return COPY_CANCEL;
    }
  }

  if (!(ShellCopy::Flags&FCOPY_COPYTONUL) && (ShellCopy::Flags&FCOPY_USESYSTEMCOPY) && !Append)
  {
    //if(!(WinVer.dwMajorVersion >= 5 && WinVer.dwMinorVersion > 0) && (ShellCopy::Flags&FCOPY_DECRYPTED_DESTINATION))
    if(!(SrcData.dwFileAttributes&FILE_ATTRIBUTE_ENCRYPTED) ||
        (SrcData.dwFileAttributes&FILE_ATTRIBUTE_ENCRYPTED) &&
          (WinVer.dwMajorVersion >= 5 && WinVer.dwMinorVersion > 0 ||
          !(ShellCopy::Flags&(FCOPY_DECRYPTED_DESTINATION)))
      )
    {
      if (!Opt.CMOpt.CopyOpened)
      {
        HANDLE SrcHandle=apiCreateFile(
            SrcName,
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            NULL
            );

        if (SrcHandle==INVALID_HANDLE_VALUE)
        {
          _LOGCOPYR(SysLog(L"return COPY_FAILURE -> %d if (SrcHandle==INVALID_HANDLE_VALUE)",__LINE__));
          return COPY_FAILURE;
        }

        CloseHandle(SrcHandle);
      }

      //_LOGCOPYR(SysLog(L"call ShellSystemCopy('%s','%s',%p)",SrcName,DestName,SrcData));
      return(ShellSystemCopy(SrcName,DestName,SrcData));
    }
  }

  SECURITY_ATTRIBUTES sa;
  if ((ShellCopy::Flags&FCOPY_COPYSECURITY) && !GetSecurity(SrcName,sa))
    return COPY_CANCEL;

  int OpenMode=FILE_SHARE_READ;
  if (Opt.CMOpt.CopyOpened)
    OpenMode|=FILE_SHARE_WRITE;
  HANDLE SrcHandle= apiCreateFile(
      SrcName,
      GENERIC_READ,
      OpenMode,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_SEQUENTIAL_SCAN,
      NULL
      );
  if (SrcHandle==INVALID_HANDLE_VALUE)
  {
    if (Opt.CMOpt.CopyOpened)
    {
      _localLastError=GetLastError();
      SetLastError(_localLastError);
      if ( _localLastError == ERROR_SHARING_VIOLATION )
      {
        SrcHandle = apiCreateFile(
            SrcName,
            GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            NULL
            );
        if (SrcHandle == INVALID_HANDLE_VALUE )
        {
          _localLastError=GetLastError();
          SetLastError(_localLastError);
          return COPY_FAILURE;
        }
      }
    }
    else
    {
      _localLastError=GetLastError();
      SetLastError(_localLastError);
      return COPY_FAILURE;
    }
  }

  HANDLE DestHandle=INVALID_HANDLE_VALUE;
  DWORD AppendPos=0;
  LONG  AppendPosHigh=0;

  if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
  {
    if (DestAttr!=(DWORD)-1 && !Append)
      _wremove(DestName);
    DestHandle=apiCreateFile(
        DestName,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        (ShellCopy::Flags&FCOPY_COPYSECURITY) ? &sa:NULL,
        (Append ? OPEN_EXISTING:CREATE_ALWAYS),
        SrcData.dwFileAttributes&(~((ShellCopy::Flags&(FCOPY_DECRYPTED_DESTINATION))?FILE_ATTRIBUTE_ENCRYPTED|FILE_FLAG_SEQUENTIAL_SCAN:FILE_FLAG_SEQUENTIAL_SCAN)),
        NULL
        );

    ShellCopy::Flags&=~FCOPY_DECRYPTED_DESTINATION;
    if (DestHandle==INVALID_HANDLE_VALUE)
    {
      _localLastError=GetLastError();
      CloseHandle(SrcHandle);
      SetLastError(_localLastError);
      _LOGCOPYR(SysLog(L"return COPY_FAILURE -> %d CreateFile=-1, LastError=%d (0x%08X)",__LINE__,_localLastError,_localLastError));
      return COPY_FAILURE;
    }

    if (Append)
    {
      AppendPos=SetFilePointer(DestHandle,0,&AppendPosHigh,FILE_END);
      _localLastError=GetLastError();
      if(AppendPos == (DWORD)0xFFFFFFFF && _localLastError != NO_ERROR)
      {
        CloseHandle(SrcHandle);
        CloseHandle(DestHandle);
        SetLastError(_localLastError);
        _LOGCOPYR(SysLog(L"return COPY_FAILURE -> %d SetFilePointer() == -1, LastError=%d (0x%08X)",__LINE__,_localLastError,_localLastError));
        return COPY_FAILURE;
      }
      SetLastError(_localLastError);
    }
  }

//  int64 WrittenSize(0,0);
  int   AbortOp = FALSE;
  UINT  OldErrMode=SetErrorMode(SEM_NOOPENFILEERRORBOX|SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
  unsigned __int64 FileSize = SrcData.nFileSize;

  while (1)
  {
    BOOL IsChangeConsole=OrigScrX != ScrX || OrigScrY != ScrY;
    if (CheckForEscSilent())
    {
      AbortOp = ConfirmAbortOp();
      IsChangeConsole=TRUE; // !!! ������ ���; ��� ����, ����� ��������� �����
    }
    if(IsChangeConsole)
    {
      ShellCopy::PR_ShellCopyMsg();
      OrigScrX=ScrX;
      OrigScrY=ScrY;
    }

    if (AbortOp)
    {
      CloseHandle(SrcHandle);
      if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
      {
        if (Append)
        {
          SetFilePointer(DestHandle,AppendPos,&AppendPosHigh,FILE_BEGIN);
          SetEndOfFile(DestHandle);
        }
        CloseHandle(DestHandle);
        if (!Append)
        {
          SetFileAttributesW(DestName,FILE_ATTRIBUTE_NORMAL);
          _wremove(DestName); //BUGBUG
        }
      }
      SetErrorMode(OldErrMode);
      return COPY_CANCEL;
    }
    DWORD BytesRead,BytesWritten;

//    if (CopyBufSize < CopyBufferSize)
//      StartTime=clock();
    while (!ReadFile(SrcHandle,CopyBuffer,CopyBufSize,&BytesRead,NULL))
    {
      int MsgCode = Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,2,UMSG(MError),
                            UMSG(MCopyReadError),SrcName,
                            UMSG(MRetry),UMSG(MCancel));
      ShellCopy::PR_ShellCopyMsg();
      if (MsgCode==0)
        continue;
      DWORD LastError=GetLastError();
      CloseHandle(SrcHandle);
      if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
      {
        if (Append)
        {
          SetFilePointer(DestHandle,AppendPos,&AppendPosHigh,FILE_BEGIN);
          SetEndOfFile(DestHandle);
        }
        CloseHandle(DestHandle);
        if (!Append)
        {
          SetFileAttributesW(DestName,FILE_ATTRIBUTE_NORMAL);
          _wremove(DestName); //BUGBUG
        }
      }
      ShowBar(0,0,false);
      ShowTitle(FALSE);
      SetErrorMode(OldErrMode);
      SetLastError(_localLastError=LastError);
      CurCopiedSize = 0; // �������� ������� ��������
      return COPY_FAILURE;
    }
    if (BytesRead==0)
      break;

    if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
    {
      while (!WriteFile(DestHandle,CopyBuffer,BytesRead,&BytesWritten,NULL))
      {
        DWORD LastError=GetLastError();
        int Split=FALSE,SplitCancelled=FALSE,SplitSkipped=FALSE;
        if ((LastError==ERROR_DISK_FULL || LastError==ERROR_HANDLE_DISK_FULL) &&
            DestName[0]!=0 && DestName[1]==':')
        {
          string strDriveRoot;
          GetPathRoot(DestName,strDriveRoot);

          DWORD SectorsPerCluster,BytesPerSector,FreeClusters,Clusters;
          if (GetDiskFreeSpaceW(strDriveRoot,&SectorsPerCluster,&BytesPerSector,
                               &FreeClusters,&Clusters))
          {
            DWORD FreeSize=SectorsPerCluster*BytesPerSector*FreeClusters;
            if (FreeSize<BytesRead &&
                WriteFile(DestHandle,CopyBuffer,FreeSize,&BytesWritten,NULL) &&
                SetFilePointer(SrcHandle,FreeSize-BytesRead,NULL,FILE_CURRENT)!=0xFFFFFFFF)
            {
              CloseHandle(DestHandle);
              SetMessageHelp(L"CopyFiles");
              int MsgCode=Message(MSG_DOWN|MSG_WARNING,4,UMSG(MError),
                                  UMSG(MErrorInsufficientDiskSpace),DestName,
                                  UMSG(MSplit),UMSG(MSkip),UMSG(MRetry),UMSG(MCancel));
              ShellCopy::PR_ShellCopyMsg();
              if (MsgCode==2)
              {
                CloseHandle(SrcHandle);
                if (!Append)
                {
                  SetFileAttributesW(DestName,FILE_ATTRIBUTE_NORMAL);
                  _wremove(DestName); //BUGBUG
                }
                SetErrorMode(OldErrMode);
                return COPY_FAILURE;
              }
              if (MsgCode==0)
              {
                Split=TRUE;
                while (1)
                {
                  if (GetDiskFreeSpaceW(strDriveRoot,&SectorsPerCluster,&BytesPerSector,&FreeClusters,&Clusters))
                    if (SectorsPerCluster*BytesPerSector*FreeClusters==0)
                    {
                      int MsgCode = Message(MSG_DOWN|MSG_WARNING,2,UMSG(MWarning),
                                            UMSG(MCopyErrorDiskFull),DestName,
                                            UMSG(MRetry),UMSG(MCancel));
                      ShellCopy::PR_ShellCopyMsg();
                      if (MsgCode!=0)
                      {
                        Split=FALSE;
                        SplitCancelled=TRUE;
                      }
                      else
                        continue;
                    }
                  break;
                }
              }
              if (MsgCode==1)
                SplitSkipped=TRUE;
              if (MsgCode==-1 || MsgCode==3)
                SplitCancelled=TRUE;
            }
          }
        }
        if (Split)
        {
          int RetCode;
          if (!AskOverwrite(SrcData,DestName,0xFFFFFFFF,FALSE,((ShellCopy::Flags&FCOPY_MOVE)?TRUE:FALSE),((ShellCopy::Flags&FCOPY_LINK)?0:1),Append,RetCode))
          {
            CloseHandle(SrcHandle);
            SetErrorMode(OldErrMode);
            return(COPY_CANCEL);
          }
          string strDestDir = DestName;

          if (CutToSlash(strDestDir,true))
            CreatePath(strDestDir);

          DestHandle=apiCreateFile(
              DestName,
              GENERIC_WRITE,
              FILE_SHARE_READ,
              NULL,
              (Append ? OPEN_EXISTING:CREATE_ALWAYS),
              SrcData.dwFileAttributes|FILE_FLAG_SEQUENTIAL_SCAN,
              NULL
              );

          if (DestHandle==INVALID_HANDLE_VALUE ||
              Append && SetFilePointer(DestHandle,0,NULL,FILE_END)==0xFFFFFFFF)
          {
            DWORD LastError=GetLastError();
            CloseHandle(SrcHandle);
            CloseHandle(DestHandle);
            SetErrorMode(OldErrMode);
            SetLastError(_localLastError=LastError);
            return COPY_FAILURE;
          }
        }
        else
        {
          if (!SplitCancelled && !SplitSkipped &&
              Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,2,UMSG(MError),
              UMSG(MCopyWriteError),DestName,UMSG(MRetry),UMSG(MCancel))==0)
          {
            continue;
          }
          CloseHandle(SrcHandle);
          if (Append)
          {
            SetFilePointer(DestHandle,AppendPos,&AppendPosHigh,FILE_BEGIN);
            SetEndOfFile(DestHandle);
          }
          CloseHandle(DestHandle);
          if (!Append)
          {
            SetFileAttributesW(DestName,FILE_ATTRIBUTE_NORMAL);
            _wremove(DestName); //BUGBUG
          }
          ShowBar(0,0,false);
          ShowTitle(FALSE);
          SetErrorMode(OldErrMode);
          SetLastError(_localLastError=LastError);
          if (SplitSkipped)
            return COPY_NEXT;

          return(SplitCancelled ? COPY_CANCEL:COPY_FAILURE);
        }
        break;
      }
    }
    else
    {
      BytesWritten=BytesRead; // �� ������� ���������� ���������� ���������� ����
    }

    CurCopiedSize+=BytesWritten;
    if (ShowTotalCopySize)
      TotalCopiedSize+=BytesWritten;

    //  + ���������� �������� �� ���� 5 ��� � �������
    if ((CurCopiedSize == FileSize) || (clock() - LastShowTime > COPY_TIMEOUT))
    {
      ShowBar(CurCopiedSize,FileSize,false);
      if (ShowTotalCopySize)
      {
        ShowBar(TotalCopiedSize,TotalCopySize,true);
        ShowTitle(FALSE);
      }
    }
  } /* while */

  SetErrorMode(OldErrMode);

  if(!(ShellCopy::Flags&FCOPY_COPYTONUL))
  {
    SetFileTime(DestHandle,NULL,NULL,&SrcData.ftLastWriteTime);
    CloseHandle(SrcHandle);
    CloseHandle(DestHandle);

    // TODO: ����� ������� Compressed???
    if (WinVer.dwPlatformId==VER_PLATFORM_WIN32_WINDOWS &&
        (SrcData.dwFileAttributes & (FA_HIDDEN|FA_SYSTEM|FA_RDONLY)))
      ShellSetAttr(DestName,SrcData.dwFileAttributes&(~((ShellCopy::Flags&FCOPY_DECRYPTED_DESTINATION)?FILE_ATTRIBUTE_ENCRYPTED:0)));
    ShellCopy::Flags&=~FCOPY_DECRYPTED_DESTINATION;
  }
  else
    CloseHandle(SrcHandle);

  return COPY_SUCCESS;
}

static void GetTimeText(int Time, string &strTimeText)
{
  int Sec = Time;
  int Min = Sec/60;
  Sec-=(Min * 60);
  int Hour = Min/60;
  Min-=(Hour*60);
  strTimeText.Format (L"%02d:%02d:%02d",Hour,Min,Sec);
}

//  + ������� ���������� TRUE, ���� ���-�� ����������, ����� FALSE
int ShellCopy::ShowBar(unsigned __int64 WrittenSize,unsigned __int64 TotalSize,bool TotalBar)
{
  // // _LOGCOPYR(CleverSysLog clv(L"ShellCopy::ShowBar"));
  // // _LOGCOPYR(SysLog(L"WrittenSize=%Ld ,TotalSize=%Ld, TotalBar=%d",WrittenSize,TotalSize,TotalBar));
  if (!ShowTotalCopySize || TotalBar)
    LastShowTime = clock();

  unsigned __int64 OldWrittenSize = WrittenSize;
  unsigned __int64 OldTotalSize = TotalSize;

  WrittenSize=WrittenSize>>8;
  TotalSize=TotalSize>>8;

  int Length;
  if (WrittenSize > TotalSize )
    WrittenSize = TotalSize;
  if (TotalSize==0)
    Length=BarLength;
  else
    if (TotalSize<1000000)
      Length=static_cast<int>(WrittenSize*BarLength/TotalSize);
    else
      Length=static_cast<int>((WrittenSize/100)*BarLength/(TotalSize/100));
  wchar_t ProgressBar[100];

  for (int i = 0; i < BarLength; i++)
    ProgressBar[i] = 0x2591;

  ProgressBar[BarLength]=0;

  if (TotalSize!=0)
  {
    for (int i = 0; i < Length; i++)
      ProgressBar[i] = 0x2588;
  }

  SetColor(COL_DIALOGTEXT);
  GotoXY(BarX,BarY+(TotalBar ? 2:0));
  Text(ProgressBar);

  GotoXY(BarX+BarLength,BarY+(TotalBar ? 2:0));

  string strPercents;

  strPercents.Format (L"%4d%%", ToPercent64 (WrittenSize, TotalSize));

  Text(strPercents);

  //  + ���������� ����� �����������,���������� ����� � ������� ��������.
  // // _LOGCOPYR(SysLog(L"!!!!!!!!!!!!!! ShowCopyTime=%d ,ShowTotalCopySize=%d, TotalBar=%d",ShowCopyTime,ShowTotalCopySize,TotalBar));
  if (ShowCopyTime && (!ShowTotalCopySize || TotalBar))
  {
    unsigned WorkTime = clock() - CopyStartTime;
    unsigned __int64 SizeLeft = OldTotalSize - OldWrittenSize;
    if (SizeLeft < 0)
      SizeLeft = 0;

    unsigned CalcTime = OldCalcTime;
    if (WaitUserTime != -1) // -1 => ��������� � �������� �������� ������ �����
      OldCalcTime = CalcTime = WorkTime - WaitUserTime;
    WorkTime /= 1000;
    CalcTime /= 1000;

    unsigned TimeLeft;
    string strTimeStr;
    wchar_t c[2];
    c[1]=0;

    if (OldTotalSize == 0 || WorkTime == 0)
      strTimeStr.Format (UMSG(MCopyTimeInfo), L" ", L" ", 0, L" ");
    else
    {
      if (TotalBar)
        OldWrittenSize = OldWrittenSize - TotalSkippedSize;
      unsigned CPS = static_cast<int>(CalcTime?OldWrittenSize/CalcTime:0);
      TimeLeft = static_cast<int>((CPS)?SizeLeft/CPS:0);
      c[0]=L' ';
      if (CPS > 99999) {
        c[0]=L'K';
        CPS = CPS/1024;
      }
      if (CPS > 99999) {
        c[0]=L'M';
        CPS = CPS/1024;
      }
      if (CPS > 99999) {
        c[0]=L'G';
        CPS = CPS/1024;
      }
      string strWorkTimeStr;
      string strTimeLeftStr;
      GetTimeText(WorkTime, strWorkTimeStr);
      GetTimeText(TimeLeft, strTimeLeftStr);
      strTimeStr.Format (UMSG(MCopyTimeInfo), (const wchar_t*)strWorkTimeStr, (const wchar_t*)strTimeLeftStr, CPS, c);
    }
    GotoXY(BarX,BarY+(TotalBar?6:4));
    Text(strTimeStr);
  }
  return (TRUE);
}


void ShellCopy::SetDestDizPath(const wchar_t *DestPath)
{
  if (!(ShellCopy::Flags&FCOPY_DIZREAD))
  {
    strDestDizPath = DestPath;

    CutToSlash(strDestDizPath);

    if ( strDestDizPath.IsEmpty() )
      strDestDizPath = L".";

    if (Opt.Diz.UpdateMode==DIZ_UPDATE_IF_DISPLAYED && !SrcPanel->IsDizDisplayed() ||
        Opt.Diz.UpdateMode==DIZ_NOT_UPDATE)
      strDestDizPath=L"";
    if ( !strDestDizPath.IsEmpty() )
      DestDiz.Read(strDestDizPath);
    ShellCopy::Flags|=FCOPY_DIZREAD;
  }
}

int ShellCopy::AskOverwrite(const FAR_FIND_DATA_EX &SrcData,
               const wchar_t *DestName, DWORD DestAttr,
               int SameName,int Rename,int AskAppend,
               int &Append,int &RetCode)
{
  HANDLE FindHandle;
  FAR_FIND_DATA_EX DestData;
  DestData.Clear();
  int DestDataFilled=FALSE;

  int MsgCode;

  Append=FALSE;

  if((ShellCopy::Flags&FCOPY_COPYTONUL))
  {
    RetCode=COPY_NEXT;
    return TRUE;
  }

  if (DestAttr==0xFFFFFFFF)
    if ((DestAttr=GetFileAttributesW(DestName))==0xFFFFFFFF)
      return(TRUE);

  if (DestAttr & FILE_ATTRIBUTE_DIRECTORY)
    return(TRUE);

  string strTruncDestName = DestName;
  TruncPathStr(strTruncDestName,ScrX-16);

  if (OvrMode!=-1)
    MsgCode=OvrMode;
  else
  {
    int Type;
    if (!Opt.Confirm.Copy && !Rename || !Opt.Confirm.Move && Rename ||
        SameName || (Type=GetFileTypeByName(DestName))==FILE_TYPE_CHAR ||
        Type==FILE_TYPE_PIPE || (ShellCopy::Flags&FCOPY_OVERWRITENEXT))
      MsgCode=1;
    else
    {
      DestData.Clear();
      if ((FindHandle=apiFindFirstFile(DestName,&DestData))!=INVALID_HANDLE_VALUE)
        FindClose(FindHandle);
      DestDataFilled=TRUE;
      //   ����� "Only newer file(s)"
      if((ShellCopy::Flags&FCOPY_ONLYNEWERFILES))
      {
        // ������� �����
        __int64 RetCompare=*(__int64*)&DestData.ftLastWriteTime - *(__int64*)&SrcData.ftLastWriteTime;
        if(RetCompare < 0)
          MsgCode=0;
        else
          MsgCode=2;
      }
      else
      {
        string strSrcFileStr, strDestFileStr;
        unsigned __int64 SrcSize = SrcData.nFileSize;
        string strSrcSizeText;
        strSrcSizeText.Format(L"%I64u", SrcSize);

        unsigned __int64 DestSize = DestData.nFileSize;

        string strDestSizeText;
        strDestSizeText.Format(L"%I64u", DestSize);

        string strDateText, strTimeText;
        ConvertDate(SrcData.ftLastWriteTime,strDateText,strTimeText,8,FALSE,FALSE,TRUE,TRUE);
        strSrcFileStr.Format (L"%-17s %11.11s %s %s",UMSG(MCopySource),(const wchar_t*)strSrcSizeText,(const wchar_t*)strDateText,(const wchar_t*)strTimeText);
        ConvertDate(DestData.ftLastWriteTime,strDateText,strTimeText,8,FALSE,FALSE,TRUE,TRUE);
        strDestFileStr.Format (L"%-17s %11.11s %s %s",UMSG(MCopyDest),(const wchar_t*)strDestSizeText,(const wchar_t*)strDateText,(const wchar_t*)strTimeText);

        SetMessageHelp(L"CopyFiles");
        MsgCode=Message(MSG_DOWN|MSG_WARNING,AskAppend ? 6:5,UMSG(MWarning),
                UMSG(MCopyFileExist),strTruncDestName,L"\x1",strSrcFileStr, strDestFileStr,
                L"\x1",UMSG(MCopyOverwrite),UMSG(MCopyOverwriteAll),
                UMSG(MCopySkipOvr),UMSG(MCopySkipAllOvr),
                AskAppend ? (AskAppend==1 ? UMSG(MCopyAppend):UMSG(MCopyResume)):UMSG(MCopyCancelOvr),
                AskAppend ? UMSG(MCopyCancelOvr):NULL);
        if (!AskAppend && MsgCode==4)
          MsgCode=5;
      }
    }
  }

  switch(MsgCode)
  {
    case 1:
      OvrMode=1;
      break;
    case 2:
      RetCode=COPY_NEXT;
      return(FALSE);
    case 3:
      OvrMode=3;
      RetCode=COPY_NEXT;
      return(FALSE);
    case 4:
      Append=TRUE;
      break;
    case -1:
    case -2:
    case 5:
      RetCode=COPY_CANCEL;
      return(FALSE);
  }
  if ((DestAttr & FA_RDONLY) && !(ShellCopy::Flags&FCOPY_OVERWRITENEXT))
  {
    int MsgCode;
    if (SameName)
      MsgCode=0;
    else
      if (ReadOnlyOvrMode!=-1)
        MsgCode=ReadOnlyOvrMode;
      else
      {
        if (!DestDataFilled)
        {
          DestData.Clear();
          if ((FindHandle=apiFindFirstFile(DestName,&DestData))!=INVALID_HANDLE_VALUE)
            FindClose(FindHandle);
        }
        string strDateText,strTimeText;
        string strSrcFileStr, strDestFileStr;

        unsigned __int64 SrcSize = SrcData.nFileSize;
        string strSrcSizeText;
        strSrcSizeText.Format(L"%I64u", SrcSize);
        unsigned __int64 DestSize = DestData.nFileSize;
        string strDestSizeText;
        strDestSizeText.Format(L"%I64u", DestSize);

        ConvertDate(SrcData.ftLastWriteTime,strDateText,strTimeText,8,FALSE,FALSE,TRUE,TRUE);
        strSrcFileStr.Format (L"%-17s %11.11s %s %s",UMSG(MCopySource),(const wchar_t*)strSrcSizeText,(const wchar_t*)strDateText,(const wchar_t*)strTimeText);
        ConvertDate(DestData.ftLastWriteTime,strDateText,strTimeText,8,FALSE,FALSE,TRUE,TRUE);
        strDestFileStr.Format (L"%-17s %11.11s %s %s",UMSG(MCopyDest),(const wchar_t*)strDestSizeText,(const wchar_t*)strDateText,(const wchar_t*)strTimeText);

        SetMessageHelp(L"CopyFiles");
        MsgCode=Message(MSG_DOWN|MSG_WARNING,AskAppend ? 6:5,UMSG(MWarning),
                UMSG(MCopyFileRO),strTruncDestName,L"\x1",strSrcFileStr, strDestFileStr,
                L"\x1",UMSG(MCopyOverwrite),UMSG(MCopyOverwriteAll),
                UMSG(MCopySkipOvr),UMSG(MCopySkipAllOvr),
                AskAppend ? UMSG(MCopyAppend):UMSG(MCopyCancelOvr),
                AskAppend ? UMSG(MCopyCancelOvr):NULL);
        if (!AskAppend && MsgCode==4)
          MsgCode=5;
      }
    switch(MsgCode)
    {
      case 1:
        ReadOnlyOvrMode=1;
        break;
      case 2:
        RetCode=COPY_NEXT;
        return(FALSE);
      case 3:
        ReadOnlyOvrMode=3;
        RetCode=COPY_NEXT;
        return(FALSE);
      case 4:
        ReadOnlyOvrMode=1;
        Append=TRUE;
        break;
      case -1:
      case -2:
      case 5:
        RetCode=COPY_CANCEL;
        return(FALSE);
    }
  }
  if (!SameName && (DestAttr & (FA_RDONLY|FA_HIDDEN|FA_SYSTEM)))
    SetFileAttributesW(DestName,FILE_ATTRIBUTE_NORMAL);
  return(TRUE);
}



int ShellCopy::GetSecurity(const wchar_t *FileName,SECURITY_ATTRIBUTES &sa)
{
  SECURITY_INFORMATION si=DACL_SECURITY_INFORMATION;
  SECURITY_DESCRIPTOR *sd=(SECURITY_DESCRIPTOR *)sddata;
  DWORD Needed;
  BOOL RetSec=GetFileSecurityW(FileName,si,sd,SDDATA_SIZE,&Needed);
  int LastError=GetLastError();
  if (!RetSec)
  {
    sd=NULL;
    if (LastError!=ERROR_SUCCESS && LastError!=ERROR_FILE_NOT_FOUND &&
        LastError!=ERROR_CALL_NOT_IMPLEMENTED &&
        Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,2,UMSG(MError),
                UMSG(MCannotGetSecurity),FileName,UMSG(MOk),UMSG(MCancel))==1)
      return(FALSE);
  }
  sa.nLength=sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor=sd;
  sa.bInheritHandle=FALSE;
  return(TRUE);
}



int ShellCopy::SetSecurity(const wchar_t *FileName,const SECURITY_ATTRIBUTES &sa)
{
  SECURITY_INFORMATION si=DACL_SECURITY_INFORMATION;
  BOOL RetSec=SetFileSecurityW(FileName,si,(PSECURITY_DESCRIPTOR)sa.lpSecurityDescriptor);

  int LastError=GetLastError();

  if (!RetSec)
  {
    if (LastError!=ERROR_SUCCESS && LastError!=ERROR_FILE_NOT_FOUND &&
        LastError!=ERROR_CALL_NOT_IMPLEMENTED &&
        Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,2,UMSG(MError),
                UMSG(MCannotSetSecurity),FileName,UMSG(MOk),UMSG(MCancel))==1)
      return(FALSE);
  }
  return(TRUE);
}



BOOL ShellCopySecuryMsg(const wchar_t *Name)
{
  static clock_t PrepareSecuryStartTime;
  static int Width=30;
  int WidthTemp;

  string strOutFileName;

  if (Name == NULL || *Name == 0 || (static_cast<DWORD>(clock() - PrepareSecuryStartTime) > Opt.ShowTimeoutDACLFiles))
  {
    if(Name && *Name)
    {
      PrepareSecuryStartTime = clock();     // ������ ���� �������� ������
      WidthTemp=Max(StrLength(Name),(int)30);
    }
    else
      Width=WidthTemp=30;

    if(WidthTemp > WidthNameForMessage)
      WidthTemp=WidthNameForMessage; // ������ ������ - 38%

    if(Width < WidthTemp)
      Width=WidthTemp;

    strOutFileName = Name;
    TruncPathStr(strOutFileName,Width);
    CenterStr(strOutFileName, strOutFileName,Width+4);

    Message(0,0,UMSG(MMoveDlgTitle),UMSG(MCopyPrepareSecury),strOutFileName);

    if(CheckForEscSilent())
    {
      if(ConfirmAbortOp())
        return FALSE;
    }
  }

  PreRedrawParam.Param1=static_cast<void*>(const_cast<wchar_t*>(Name));
  return TRUE;
}

static void PR_ShellCopySecuryMsg(void)
{
  ShellCopySecuryMsg(static_cast<const wchar_t*>(PreRedrawParam.Param1));
}


int ShellCopy::SetRecursiveSecurity(const wchar_t *FileName,const SECURITY_ATTRIBUTES &sa)
{
  if(SetSecurity(FileName,sa))
  {
    if(::GetFileAttributesW(FileName) & FILE_ATTRIBUTE_DIRECTORY)
    {
      SaveScreen SaveScr;
      PREREDRAWFUNC OldPreRedrawFunc=PreRedrawFunc;
      //SetCursorType(FALSE,0);
      SetPreRedrawFunc(PR_ShellCopySecuryMsg);
      //ShellCopySecuryMsg("");

      string strFullName;
      FAR_FIND_DATA_EX SrcData;
      ScanTree ScTree(TRUE,TRUE,ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS);
      ScTree.SetFindPath(FileName,L"*.*",FSCANTREE_FILESFIRST);
      while (ScTree.GetNextName(&SrcData,strFullName))
      {
        if(!ShellCopySecuryMsg(strFullName))
          break;

        if(!SetSecurity(strFullName,sa))
        {
          SetPreRedrawFunc(OldPreRedrawFunc);
          return FALSE;
        }
      }
      SetPreRedrawFunc(OldPreRedrawFunc);
    }
    return TRUE;
  }
  return FALSE;
}



int ShellCopy::ShellSystemCopy(const wchar_t *SrcName,const wchar_t *DestName,const FAR_FIND_DATA_EX &SrcData)
{
  SECURITY_ATTRIBUTES sa;
  if ((ShellCopy::Flags&FCOPY_COPYSECURITY) && !GetSecurity(SrcName,sa))
    return(COPY_CANCEL);

  ShellCopyMsg(SrcName,DestName,MSG_LEFTALIGN|MSG_KEEPBACKGROUND);

  if (Init_CopyFileEx())
  {
    BOOL Cancel=0;
    TotalCopiedSizeEx=TotalCopiedSize;
#ifndef COPY_FILE_ALLOW_DECRYPTED_DESTINATION
#define COPY_FILE_ALLOW_DECRYPTED_DESTINATION 0x00000008
#endif
    if (!apiCopyFileEx(SrcName,DestName,(void *)CopyProgressRoutine,NULL,&Cancel,
         ShellCopy::Flags&FCOPY_DECRYPTED_DESTINATION?COPY_FILE_ALLOW_DECRYPTED_DESTINATION:0))
    {
      ShellCopy::Flags&=~FCOPY_DECRYPTED_DESTINATION;
      return (_localLastError=GetLastError())==ERROR_REQUEST_ABORTED ? COPY_CANCEL:COPY_FAILURE;
    }
    ShellCopy::Flags&=~FCOPY_DECRYPTED_DESTINATION;
  }
  else
  {
    if (ShowTotalCopySize)
    {
      unsigned __int64 AddSize = SrcData.nFileSize;
      TotalCopiedSize += AddSize;
      CurCopiedSize = AddSize;
      ShowBar(TotalCopiedSize,TotalCopySize,true);
      ShowTitle(FALSE);
    }
    // ����� ��... ����� ���� ������ ���, ��� �� ����� �� �������� ���� CopyFileExA
    if (!apiCopyFile(SrcName,DestName,FALSE))
      return COPY_FAILURE;
  }

  if ((ShellCopy::Flags&FCOPY_COPYSECURITY) && !SetSecurity(DestName,sa))
    return(COPY_CANCEL);
  return(COPY_SUCCESS);
}


#define PROGRESS_CONTINUE  0
#define PROGRESS_CANCEL    1
#if defined(__BORLANDC__)
#pragma warn -par
#endif


DWORD WINAPI CopyProgressRoutine(LARGE_INTEGER TotalFileSize,
      LARGE_INTEGER TotalBytesTransferred,LARGE_INTEGER StreamSize,
      LARGE_INTEGER StreamBytesTransferred,DWORD dwStreamNumber,
      DWORD dwCallbackReason,HANDLE hSourceFile,HANDLE hDestinationFile,
      LPVOID lpData)
{
  // // _LOGCOPYR(CleverSysLog clv(L"CopyProgressRoutine"));
  // // _LOGCOPYR(SysLog(L"dwStreamNumber=%d",dwStreamNumber));

  unsigned __int64 TransferredSize = TotalBytesTransferred.QuadPart;
  unsigned __int64 TotalSize = TotalFileSize.QuadPart;

  int AbortOp = FALSE;
  BOOL IsChangeConsole=OrigScrX != ScrX || OrigScrY != ScrY;
  if (CheckForEscSilent())
  {
    // // _LOGCOPYR(SysLog(L"2='%s'/0x%08X  3='%s'/0x%08X  Flags=0x%08X",(char*)PreRedrawParam.Param2,PreRedrawParam.Param2,(char*)PreRedrawParam.Param3,PreRedrawParam.Param3,PreRedrawParam.Flags));
    AbortOp = ConfirmAbortOp();
    IsChangeConsole=TRUE; // !!! ������ ���; ��� ����, ����� ��������� �����
  }

  if(IsChangeConsole)
  {
    // // _LOGCOPYR(SysLog(L"IsChangeConsole 1"));
    ShellCopy::PR_ShellCopyMsg();
    OrigScrX=ScrX;
    OrigScrY=ScrY;
  }

  CurCopiedSize = TransferredSize;

  if ((CurCopiedSize == TotalSize) || (clock() - LastShowTime > COPY_TIMEOUT))
  {
    ShellCopy::ShowBar(TransferredSize,TotalSize,FALSE);
    if (ShowTotalCopySize && dwStreamNumber==1)
    {
      TotalCopiedSize=TotalCopiedSizeEx+CurCopiedSize;
      ShellCopy::ShowBar(TotalCopiedSize,TotalCopySize,true);
      ShellCopy::ShowTitle(FALSE);
    }
  }
  return(AbortOp ? PROGRESS_CANCEL:PROGRESS_CONTINUE);
}

#if defined(__BORLANDC__)
#pragma warn +par
#endif



int ShellCopy::IsSameDisk(const wchar_t *SrcPath,const wchar_t *DestPath)
{
  return CheckDisksProps(SrcPath,DestPath,CHECKEDPROPS_ISSAMEDISK);
}


bool ShellCopy::CalcTotalSize()
{
  string strSelName, strSelShortName;
  int FileAttr;

  // ��� �������
  FAR_FIND_DATA_EX fd;

  TotalCopySize=CurCopiedSize=0;
  TotalFilesToProcess = 0;

  ShellCopyMsg(NULL,L"",MSG_LEFTALIGN);

  SrcPanel->GetSelName(NULL,FileAttr);
  while (SrcPanel->GetSelName(&strSelName,FileAttr,&strSelShortName,&fd))
  {
    if (FileAttr & FILE_ATTRIBUTE_DIRECTORY)
    {
      {
        unsigned long DirCount,FileCount,ClusterSize;
        unsigned __int64 FileSize,CompressedSize,RealFileSize;
        ShellCopyMsg(NULL,strSelName,MSG_LEFTALIGN|MSG_KEEPBACKGROUND);
        if (!GetDirInfo(L"",strSelName,DirCount,FileCount,FileSize,CompressedSize,
                        RealFileSize,ClusterSize,0xffffffff,
                        Filter,
                        (ShellCopy::Flags&FCOPY_COPYSYMLINKCONTENTS?GETDIRINFO_SCANSYMLINK:0)|
                        (UseFilter?GETDIRINFO_USEFILTER:0)))
        {
          ShowTotalCopySize=false;
          return(false);
        }
        TotalCopySize+=FileSize;
        TotalFilesToProcess += FileCount;
      }
    }
    else
    {
      //  ���������� ���������� ������
      if (UseFilter)
      {
        if (!Filter->FileInFilter(&fd))
          continue;
      }

      unsigned __int64 FileSize = SrcPanel->GetLastSelectedSize();

      if ( FileSize != (unsigned __int64)-1 )
      {
        TotalCopySize+=FileSize;
        TotalFilesToProcess++;
      }
    }
  }
  // TODO: ��� ��� ��������, ����� "����� = ����� ������ * ���������� �����"
  TotalCopySize=TotalCopySize*(__int64)CountTarget;

  InsertCommas(TotalCopySize,strTotalCopySizeText);
  return(true);
}

void ShellCopy::ShowTitle(int FirstTime)
{
  if (ShowTotalCopySize && !FirstTime)
  {
    unsigned __int64 CopySize=TotalCopiedSize>>8,TotalSize=TotalCopySize>>8;
    StaticCopyTitle->Set(L"{%d%%} %s",ToPercent64(CopySize,TotalSize),StaticMove ? UMSG(MCopyMovingTitle):UMSG(MCopyCopyingTitle));
  }
}


/* $ 25.05.2002 IS
 + ������ �������� � ��������� _��������_ �������, � ���������� ����
   ������������� ��������, �����
   Src="D:\Program Files\filename"
   Dest="D:\PROGRA~1\filename"
   ("D:\PROGRA~1" - �������� ��� ��� "D:\Program Files")
   ���������, ��� ����� ���� ����������, � ������ ���������,
   ��� ��� ������ (������� �� �����, ��� � � ������, � �� ������ ������
   ���� ���� � ��� ��)
 ! ����������� - "���������" ������� �� DeleteEndSlash
 ! ������� ��� ���������������� �� �������� ���� � ������
   ��������� �� ������� �����, ������ ��� ��� ����� ������ ������ ���
   ��������������, � ������� ���������� � ��� ����������� ����. ��� ���
   ������ �������������� �� � ���, � ��� ��, ��� � RenameToShortName.
   ������ ������� ������ 1, ��� ������ ���� src=path\filename,
   dest=path\filename (������ ���������� 2 - �.�. ������ �� ������).
*/

int ShellCopy::CmpFullNames(const wchar_t *Src,const wchar_t *Dest)
{
  string strSrcFullName, strDestFullName;
  int I;

  // ������� ������ ���� � ������ ������������� ������
  ConvertNameToReal(Src, strSrcFullName);
  ConvertNameToReal(Dest, strDestFullName);

  wchar_t *lpwszSrc = strSrcFullName.GetBuffer ();
  // ������ ����� �� ����
  for (I=StrLength(lpwszSrc)-1;I>0 && lpwszSrc[I]==L'.';I--)
    lpwszSrc[I]=0;

  strSrcFullName.ReleaseBuffer ();

  DeleteEndSlash(strSrcFullName);

  wchar_t *lpwszDest = strDestFullName.GetBuffer ();

  for (I=StrLength(lpwszDest)-1;I>0 && lpwszDest[I]==L'.';I--)
    lpwszDest[I]=0;

  strDestFullName.ReleaseBuffer ();

  DeleteEndSlash(strDestFullName);

  // ��������� �� �������� ����
  if(IsLocalPath(strSrcFullName))
    ConvertNameToLong (strSrcFullName, strSrcFullName);

  if(IsLocalPath(strDestFullName))
    ConvertNameToLong (strDestFullName, strDestFullName);

  return StrCmpI(strSrcFullName,strDestFullName)==0;
}




int ShellCopy::CmpFullPath(const wchar_t *Src, const wchar_t *Dest)
{
  string strSrcFullName, strDestFullName;
  int I;

  GetParentFolder(Src, strSrcFullName);
  GetParentFolder(Dest, strDestFullName);

  wchar_t *lpwszSrc = strSrcFullName.GetBuffer ();
  // ������ ����� �� ����
  for (I=StrLength(lpwszSrc)-1;I>0 && lpwszSrc[I]==L'.';I--)
    lpwszSrc[I]=0;

  strSrcFullName.ReleaseBuffer ();

  DeleteEndSlash(strSrcFullName);

  wchar_t *lpwszDest = strDestFullName.GetBuffer ();

  for (I=StrLength(lpwszDest)-1;I>0 && lpwszDest[I]=='.';I--)
    lpwszDest[I]=0;

  strDestFullName.ReleaseBuffer ();

  DeleteEndSlash(strDestFullName);

  // ��������� �� �������� ����
  if(IsLocalPath(strSrcFullName))
    ConvertNameToLong (strSrcFullName, strSrcFullName);

  if(IsLocalPath(strDestFullName))
    ConvertNameToLong (strDestFullName, strDestFullName);

  return StrCmpI (strSrcFullName, strDestFullName)==0;
}




string &ShellCopy::GetParentFolder(const wchar_t *Src, string &strDest)
{
  string strDestFullName;

  if (ConvertNameToReal (Src,strDestFullName))
  {
    strDest = L"";
    return strDest;
  }

  CutToSlash(strDestFullName,true);

  strDest = strDestFullName; //??? � ������ �� ����� �� �������� � strDest???

  return strDest;
}

// ����� ��� �������� SymLink ��� ���������.

int ShellCopy::MkSymLink(const wchar_t *SelName,const wchar_t *Dest,DWORD Flags)
{
  if (Flags&(FCOPY_LINK|FCOPY_VOLMOUNT))
  {
    string strSrcFullName, strDestFullName, strSelOnlyName;
    string strMsgBuf, strMsgBuf2;

    // ������� ���
    strSelOnlyName = SelName;

    wchar_t *lpwszSelOnly = strSelOnlyName.GetBuffer ();

    if(lpwszSelOnly[StrLength(lpwszSelOnly)-1] == L'\\')
      lpwszSelOnly[StrLength(lpwszSelOnly)-1]=0;

    strSelOnlyName.ReleaseBuffer ();

    const wchar_t *PtrSelName=wcsrchr(strSelOnlyName,L'\\');

    if(!PtrSelName)
      PtrSelName=strSelOnlyName;
    else
      ++PtrSelName;

    if(SelName[1] == L':' && (SelName[2] == 0 || SelName[2] == L'\\' && SelName[3] == 0)) // C: ��� C:/
    {
//      if(Flags&FCOPY_VOLMOUNT)
      {
        strSrcFullName = SelName;
        AddEndSlash(strSrcFullName);
      }
      /*
        ��� ����� - �� ����� ����� ���������!
        �.�. ���� � �������� SelName �������� "C:", �� � ���� ����� ����������
        ��������� ���� ����� - � symlink`� �� volmount
      */
      Flags&=~FCOPY_LINK;
      Flags|=FCOPY_VOLMOUNT;
    }
    else
      ConvertNameToFull(SelName,strSrcFullName);

    ConvertNameToFull(Dest,strDestFullName);

    if(strDestFullName.At(strDestFullName.GetLength()-1) == L'\\')
    {
      if(!(Flags&FCOPY_VOLMOUNT))
        strDestFullName += PtrSelName;
      else
      {
        string strTmp;
        strTmp.Format(L"Disk_%c",*SelName);
        strDestFullName += strTmp;
      }
    }

    if(Flags&FCOPY_VOLMOUNT)
    {
      AddEndSlash(strSrcFullName);
      AddEndSlash(strDestFullName);
    }

    int JSAttr=GetFileAttributesW(strDestFullName);
    if(JSAttr != -1) // ���������� �����?
    {
      if((JSAttr&FILE_ATTRIBUTE_DIRECTORY)!=FILE_ATTRIBUTE_DIRECTORY)
      {
        if(!(Flags&FCOPY_NOSHOWMSGLINK))
        {
          Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
                UMSG(MCopyCannotCreateJunctionToFile),
                strDestFullName,UMSG(MOk));
        }
        return 0;
      }

      if(CheckFolder(strDestFullName) == CHKFLD_NOTEMPTY) // � ������?
      {
        // �� ������, �� ��� ��, ����� ������� ������� dest\srcname
        AddEndSlash(strDestFullName);
        if(Flags&FCOPY_VOLMOUNT)
        {
          string strTmpName;
          strTmpName.Format (UMSG(MCopyMountName),*SelName);

          strDestFullName += strTmpName;
          AddEndSlash(strDestFullName);
        }
        else
          strDestFullName += PtrSelName;

        int JSAttr=GetFileAttributesW(strDestFullName);

        if(JSAttr != -1) // � ����� ���� ����???
        {
          if(CheckFolder(strDestFullName) == CHKFLD_NOTEMPTY) // � ������?
          {
            if(!(Flags&FCOPY_NOSHOWMSGLINK))
            {
              if(Flags&FCOPY_VOLMOUNT)
              {
                strMsgBuf.Format (UMSG(MCopyMountVolFailed), SelName);
                strMsgBuf2.Format (UMSG(MCopyMountVolFailed2), (const wchar_t *)strDestFullName);
                Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
                   strMsgBuf,
                   strMsgBuf2,
                   UMSG(MCopyFolderNotEmpty),
                   UMSG(MOk));
              }
              else
                Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
                      UMSG(MCopyCannotCreateJunction),strDestFullName,
                      UMSG(MCopyFolderNotEmpty),UMSG(MOk));
            }
            return 0; // ���������� � ����
          }
        }
        else // �������.
        {
          if (CreateDirectoryW(strDestFullName,NULL))
            TreeList::AddTreeName(strDestFullName);
          else
            CreatePath(strDestFullName);
        }
        if(GetFileAttributesW(strDestFullName) == -1) // ���, ��� ����� ���� �����.
        {
          if(!(Flags&FCOPY_NOSHOWMSGLINK))
          {
            Message(MSG_DOWN|MSG_WARNING|MSG_ERRORTYPE,1,UMSG(MError),
                      UMSG(MCopyCannotCreateFolder),
                      strDestFullName,UMSG(MOk));
          }
          return 0;
        }
      }
    }
    else
    {
      if (CreateDirectoryW(strDestFullName,NULL))
        TreeList::AddTreeName(strDestFullName);
      else
        CreatePath(strDestFullName);

      if(GetFileAttributesW(strDestFullName) == -1) // ���. ��� ����� ���� �����.
      {
        if(!(Flags&FCOPY_NOSHOWMSGLINK))
        {
          Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
                   UMSG(MCopyCannotCreateFolder),strDestFullName,UMSG(MOk));
        }
        return 0;
      }
    }

    if(Flags&FCOPY_LINK)
    {
      if(CreateJunctionPoint(strSrcFullName,strDestFullName))
      {
        return 1;
      }
      else
      {
        if(!(Flags&FCOPY_NOSHOWMSGLINK))
        {
          Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
                 UMSG(MCopyCannotCreateJunction),strDestFullName,UMSG(MOk));
        }
        return 0;
      }
    }
    else if(Flags&FCOPY_VOLMOUNT)
    {
      int ResultVol=CreateVolumeMountPoint(strSrcFullName,strDestFullName);
      if(!ResultVol)
      {
        return 1;
      }
      else
      {
        if(!(Flags&FCOPY_NOSHOWMSGLINK))
        {
          switch(ResultVol)
          {
            case 1:
              strMsgBuf.Format (UMSG(MCopyRetrVolFailed), SelName);
              break;
            case 2:
              strMsgBuf.Format (UMSG(MCopyMountVolFailed), SelName);
              strMsgBuf2.Format (UMSG(MCopyMountVolFailed2), (const wchar_t *)strDestFullName);
              break;
            case 3:
              strMsgBuf = UMSG(MCopyCannotSupportVolMount);
              break;
          }

          if(ResultVol == 2)
            Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
              strMsgBuf,
              strMsgBuf2,
              UMSG(MOk));
          else
            Message(MSG_DOWN|MSG_WARNING,1,UMSG(MError),
              UMSG(MCopyCannotCreateVolMount),
              strMsgBuf,
              UMSG(MOk));
        }
        return 0;
      }
    }
  }
  return 2;
}

/*
  �������� ������ SetFileAttributes() ���
  ����������� ����������� ���������
*/


int ShellCopy::ShellSetAttr(const wchar_t *Dest,DWORD Attr)
{
  string strRoot;
  string strFSysNameDst;

  DWORD FileSystemFlagsDst;

  ConvertNameToFull(Dest, strRoot);

  GetPathRoot(strRoot,strRoot);

  if(GetFileAttributesW(strRoot) == -1) // �������, ����� ������� ����, �� ��� � �������
  { // ... � ���� ������ �������� AS IS
    ConvertNameToFull(Dest,strRoot);
    GetPathRootOne(strRoot,strRoot);
    if(GetFileAttributesW(strRoot) == -1)
      return FALSE;
  }
  int GetInfoSuccess = apiGetVolumeInformation (strRoot,NULL,NULL,NULL,&FileSystemFlagsDst,&strFSysNameDst);
  if (GetInfoSuccess)
  {
     if(!(FileSystemFlagsDst&FS_FILE_COMPRESSION))
       Attr&=~FILE_ATTRIBUTE_COMPRESSED;
     if(!(FileSystemFlagsDst&FILE_SUPPORTS_ENCRYPTION))
       Attr&=~FILE_ATTRIBUTE_ENCRYPTED;
  }
  if (!SetFileAttributesW(Dest,Attr))
    return FALSE;

  if((Attr&FILE_ATTRIBUTE_COMPRESSED) && !(Attr&FILE_ATTRIBUTE_ENCRYPTED))
  {
    if(!ESetFileCompression(Dest,1,Attr&(~FILE_ATTRIBUTE_COMPRESSED)))
      return FALSE;
  }
    // ��� �����������/�������� ���������� FILE_ATTRIBUTE_ENCRYPTED
    // ��� ��������, ���� �� ����
  if (GetInfoSuccess && (FileSystemFlagsDst&FILE_SUPPORTS_ENCRYPTION) &&
     (Attr&(FILE_ATTRIBUTE_ENCRYPTED|FILE_ATTRIBUTE_DIRECTORY)) == (FILE_ATTRIBUTE_ENCRYPTED|FILE_ATTRIBUTE_DIRECTORY))
    if (!ESetFileEncryption(Dest,1,0))
      return FALSE;
  return TRUE;
}


BOOL ShellCopy::CheckNulOrCon(const wchar_t *Src)
{
  if(!StrCmpI (Src,L"nul")             ||
     !StrCmpNI(Src,L"nul\\", 4)        ||
     !StrCmpNI(Src,L"\\\\.\\nul", 7)   ||
     !StrCmpNI(Src,L"\\\\.\\nul\\", 8) ||
     !StrCmpI (Src,L"con")             ||
     !StrCmpNI(Src,L"con\\", 4)
    )
    return TRUE;
  return FALSE;
}

void ShellCopy::CheckUpdatePanel() // ���������� ���� FCOPY_UPDATEPPANEL
{
}
