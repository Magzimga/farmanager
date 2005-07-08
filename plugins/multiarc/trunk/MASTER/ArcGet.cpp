int PluginClass::GetFiles(struct PluginPanelItem *PanelItem, int ItemsNumber,
                          int Move, char *DestPath, int OpMode)
{
  char SaveDir[NM];
  GetCurrentDirectory(sizeof(SaveDir),SaveDir);
  char Command[512],AllFilesMask[32];
  if (ItemsNumber==0)
    return /*0*/1; //$ 07.02.2002 AA �⮡� �����⮬�� CAB� ��ଠ�쭮 �ᯠ���뢠����
  if (*DestPath)
    FSF.AddEndSlash(DestPath);
  const char *PathHistoryName="ExtrDestPath";
  struct InitDialogItem InitItems[]={
  /* 0 */{DI_DOUBLEBOX,3,1,72,13,0,0,0,0,(char *)MExtractTitle},
  /* 1 */{DI_TEXT,5,2,0,0,0,0,0,0,(char *)MExtractTo},
  /* 2 */{DI_EDIT,5,3,70,3,1,(DWORD)PathHistoryName,DIF_HISTORY,0,DestPath},
  /* 3 */{DI_TEXT,3,4,0,0,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,""},
  /* 4 */{DI_TEXT,5,5,0,0,0,0,0,0,(char *)MExtrPassword},
  /* 5 */{DI_PSWEDIT,5,6,35,5,0,0,0,0,""},
  /* 6 */{DI_TEXT,3,7,0,0,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,""},
  /* 7 */{DI_CHECKBOX,5,8,0,0,0,0,0,0,(char *)MExtrWithoutPaths},
  /* 8 */{DI_CHECKBOX,5,9,0,0,0,0,0,0,(char *)MBackground},
  /* 9 */{DI_CHECKBOX,5,10,0,0,0,0,0,0,(char *)MExtrDel},
  /*10 */{DI_TEXT,3,11,0,11,0,0,DIF_BOXCOLOR|DIF_SEPARATOR,0,""},
  /*11 */{DI_BUTTON,0,12,0,0,0,0,DIF_CENTERGROUP,1,(char *)MExtrExtract},
  /*12 */{DI_BUTTON,0,12,0,0,0,0,DIF_CENTERGROUP,0,(char *)MExtrCancel},
  };

  struct FarDialogItem DialogItems[sizeof(InitItems)/sizeof(InitItems[0])];
  InitDialogItems(InitItems,DialogItems,sizeof(InitItems)/sizeof(InitItems[0]));

  int AskVolume=(OpMode & (OPM_FIND|OPM_VIEW|OPM_EDIT))==0 &&
                CurArcInfo.Volume && *CurDir==0;

  if (!AskVolume)
  {
    DialogItems[7].Selected=TRUE;
    for (int I=0;I<ItemsNumber;I++)
      if (PanelItem[I].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
        DialogItems[7].Selected=FALSE;
        break;
      }
  }

  Opt.UserBackground=0; // $ 14.02.2001 raVen //��� ����� "䮭���� ��娢���"
  DialogItems[8].Selected=Opt.UserBackground;
  DialogItems[9].Selected=Move;

  if ((OpMode & OPM_SILENT)==0)
  {
    int AskCode=Info.Dialog(Info.ModuleNumber,-1,-1,76,15,"ExtrFromArc",
                DialogItems,sizeof(DialogItems)/sizeof(DialogItems[0]));
    if (AskCode!=11)
      return -1;
    strcpy(DestPath,DialogItems[2].Data);
    Opt.UserBackground=DialogItems[8].Selected;
    //SetRegKey(HKEY_CURRENT_USER,"","Background",Opt.UserBackground); // $ 06.02.2002 AA
  }

  LastWithoutPathsState=DialogItems[7].Selected;

  Opt.Background=OpMode & OPM_SILENT ? 0 : Opt.UserBackground;

  int SpaceOnly=TRUE;
  for (int I=0;DestPath[I]!=0;I++)
    if (DestPath[I]!=' ')
    {
      SpaceOnly=FALSE;
      break;
    }

  if (!SpaceOnly)
  {
    for (char *ChPtr=DestPath;*ChPtr!=0;ChPtr++)
      if (*ChPtr=='\\')
      {
        *ChPtr=0;
        CreateDirectory(DestPath,NULL);
        *ChPtr='\\';
      }
    CreateDirectory(DestPath,NULL);
  }


  if (*DestPath && DestPath[strlen(DestPath)-1]!=':')
    FSF.AddEndSlash(DestPath);
  GetCommandFormat(CMD_ALLFILESMASK,AllFilesMask,sizeof(AllFilesMask));

  struct PluginPanelItem MaskPanelItem;

  if (AskVolume)
  {
    char VolMsg[300];
    FSF.sprintf(VolMsg,GetMsg(MExtrVolume),FSF.PointToName(ArcName));
    const char *MsgItems[]={GetMsg(MExtractTitle),VolMsg,GetMsg(MExtrVolumeAsk1),
                      GetMsg(MExtrVolumeAsk2),GetMsg(MExtrVolumeSelFiles),
                      GetMsg(MExtrAllVolumes)};
    int MsgCode=Info.Message(Info.ModuleNumber,0,NULL,MsgItems,sizeof(MsgItems)/sizeof(MsgItems[0]),2);
    if (MsgCode<0)
      return -1;
    if (MsgCode==1)
    {
      memset(&MaskPanelItem,0,sizeof(MaskPanelItem));
      strcpy(MaskPanelItem.FindData.cFileName,AllFilesMask);
      strcpy(MaskPanelItem.FindData.cAlternateFileName,AllFilesMask);
      if (ItemsInfo.Encrypted)
        MaskPanelItem.Flags=F_ENCRYPTED;
      PanelItem=&MaskPanelItem;
      ItemsNumber=1;
    }
  }

  int CommandType=LastWithoutPathsState ? CMD_EXTRACTWITHOUTPATH:CMD_EXTRACT;
  GetCommandFormat(CommandType,Command,sizeof(Command));

  if (*DialogItems[5].Data==0 && strstr(Command,"%%P")!=NULL)
    for (int I=0;I<ItemsNumber;I++)
      if ((PanelItem[I].Flags & F_ENCRYPTED) || ItemsInfo.Encrypted &&
          (PanelItem[I].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      {
        if(OpMode & OPM_FIND) //Silent
          return 0;
        if (!GetPassword(DialogItems[5].Data,FSF.PointToName(ArcName)))
          return -1;
        break;
      }

  SetCurrentDirectory(DestPath);
  int SaveHideOut=Opt.HideOutput;
  if (OpMode & OPM_FIND)
    Opt.HideOutput=2;
  int IgnoreErrors=(CurArcInfo.Flags & AF_IGNOREERRORS);

  ArcCommand ArcCmd(PanelItem,ItemsNumber,Command,ArcName,CurDir,
             DialogItems[5].Data,AllFilesMask,IgnoreErrors,
             (OpMode & OPM_VIEW)!=0,(OpMode & OPM_FIND),CurDir);

  //��᫥���騥 ����樨 (���஢���� � �) �� ������ ���� 䮭��묨
  Opt.Background=0; // $ 06.02.2002 AA

  Opt.HideOutput=SaveHideOut;
  SetCurrentDirectory(SaveDir);
  if (!IgnoreErrors && ArcCmd.GetExecCode()!=0)
    if (!(OpMode & OPM_VIEW))
      return 0;

  if (DialogItems[9].Selected)
    DeleteFiles(PanelItem,ItemsNumber,TRUE);

  if (Opt.UpdateDescriptions)
    for (int I=0;I<ItemsNumber;I++)
      PanelItem[I].Flags|=PPIF_PROCESSDESCR;

  return 1;
}
