#include <FarPluginBase.h>
#ifdef _DEBUG
#include <debug.h>
#endif
#include "wcx.class.h"

WcxModule::WcxModule (const char *lpFileName)
{
	m_hModule = LoadLibraryEx (
			lpFileName,
			NULL,
			LOAD_WITH_ALTERED_SEARCH_PATH
			);

	if ( m_hModule )
	{
		m_pfnOpenArchive=(PLUGINOPENARCHIVE)GetProcAddress(m_hModule,"OpenArchive");
		m_pfnCloseArchive=(PLUGINCLOSEARCHIVE)GetProcAddress(m_hModule,"CloseArchive");
		m_pfnReadHeader=(PLUGINREADHEADER)GetProcAddress(m_hModule,"ReadHeader");
		m_pfnProcessFile=(PLUGINPROCESSFILE)GetProcAddress(m_hModule,"ProcessFile");
		m_pfnPackFiles=(PLUGINPACKFILES)GetProcAddress(m_hModule,"PackFiles");
		m_pfnDeleteFiles=(PLUGINDELETEFILES)GetProcAddress(m_hModule,"DeleteFiles");
		m_pfnSetChangeVolProc=(PLUGINSETCHANGEVOLPROC)GetProcAddress(m_hModule,"SetChangeVolProc");
		m_pfnSetProcessDataProc=(PLUGINSETPROCESSDATAPROC)GetProcAddress(m_hModule,"SetProcessDataProc");
		m_pfnConfigurePacker=(PLUGINCONFIGUREPACKER)GetProcAddress(m_hModule,"ConfigurePacker");
		m_pfnGetPackerCaps=(PLUGINGETPACKERCAPS)GetProcAddress(m_hModule,"GetPackerCaps");
		m_pfnCanYouHandleThisFile=(PLUGINCANYOUHANDLETHISFILE)GetProcAddress(m_hModule,"CanYouHandleThisFile");
		m_pfnPackSetDefaultParams=(PLUGINPACKSETDEFAULTPARAMS)GetProcAddress(m_hModule,"PackSetDefaultParams");
	}

	if ( LoadedOK() )
	{
		if (m_pfnPackSetDefaultParams)
		{
			PackDefaultParamStruct dps;

			dps.size = sizeof(dps);
			dps.PluginInterfaceVersionLow = 10;
			dps.PluginInterfaceVersionHi = 2;
			strcpy(dps.DefaultIniName,"");

			m_pfnPackSetDefaultParams(&dps);
		}
	}
}

bool WcxModule::LoadedOK ()
{
	if ( !(m_hModule && m_pfnOpenArchive && m_pfnCloseArchive && m_pfnReadHeader && m_pfnProcessFile) )
		return false;

	return true;
}

WcxModule::~WcxModule ()
{
	FreeLibrary (m_hModule);
}

WcxModules::WcxModules ()
{
	memset (&m_PluginInfo, 0, sizeof (m_PluginInfo));

	char *lpPluginsPath = StrDuplicate (Info.ModuleName, 260);

	CutToSlash (lpPluginsPath);

	strcat (lpPluginsPath, "Formats");

	m_Modules.Create(10);
	m_ExtraPluginInfo.Create(10);

	FSF.FarRecursiveSearch(lpPluginsPath,"*.wcx",(FRSUSERFUNC)LoadWcxModules,FRS_RECUR,this);

	StrFree(lpPluginsPath);

	m_PluginInfo.pFormatInfo = (ArchiveFormatInfo *) realloc (m_PluginInfo.pFormatInfo, sizeof (ArchiveFormatInfo) * m_PluginInfo.nFormats);

	for (int i=0; i<m_ExtraPluginInfo.GetCount (); i++)
	{
		m_PluginInfo.pFormatInfo[i].dwFlags = AFF_SUPPORT_INTERNAL_EXTRACT|AFF_SUPPORT_INTERNAL_TEST;
		m_PluginInfo.pFormatInfo[i].lpName = m_ExtraPluginInfo[i]->Name;
		m_PluginInfo.pFormatInfo[i].lpDefaultExtention = m_ExtraPluginInfo[i]->DefaultExtention;
	}
}

WcxModules::~WcxModules ()
{
	if ( m_PluginInfo.pFormatInfo )
		free (m_PluginInfo.pFormatInfo);

	m_ExtraPluginInfo.Free ();
	m_Modules.Free ();
}

WcxModule *WcxModules::IsArchive (QueryArchiveStruct *pQAS, int *nModuleNum)
{
	WcxModule *TrueArc = NULL;

    *nModuleNum = -1;

	for (int i=0; i<m_Modules.GetCount(); i++)
	{
		if ( m_Modules[i]->m_pfnCanYouHandleThisFile )
		{
			if (m_Modules[i]->m_pfnCanYouHandleThisFile (pQAS->lpFileName))
			{
				TrueArc = m_Modules[i];
				*nModuleNum = i;
				break;
			}
		}
		else
		{
			tOpenArchiveData OpenArchiveData = {0};

			OpenArchiveData.ArcName = (char *)pQAS->lpFileName;
			OpenArchiveData.OpenMode = PK_OM_LIST;

			HANDLE hArchive = m_Modules[i]->m_pfnOpenArchive (&OpenArchiveData);

			if (hArchive)
			{
				m_Modules[i]->m_pfnCloseArchive (hArchive);
				TrueArc = m_Modules[i];
				*nModuleNum = i;
				break;
			}
		}
	}

	return TrueArc;
}

int WINAPI WcxModules::LoadWcxModules (const WIN32_FIND_DATA *pFindData,
		const char *lpFullName,
		WcxModules *pModules
		)
{
	if ( pFindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		return TRUE;

	WcxModule *pModule = new WcxModule (lpFullName);

	if ( !pModule )
		return TRUE;

	if ( !pModule->LoadedOK () )
	{
		delete pModule;
		return TRUE;
	}

    pModules->m_PluginInfo.nFormats++;

    ExtraPluginInfo *pExtraPluginInfo = (ExtraPluginInfo *) malloc (sizeof (ExtraPluginInfo));

	strcpy(pExtraPluginInfo->Name,FSF.PointToName(lpFullName));
	*strrchr(pExtraPluginInfo->Name,'.') = 0;
	strcpy(pExtraPluginInfo->DefaultExtention,pExtraPluginInfo->Name);
	strcat(pExtraPluginInfo->Name," [wcx]");

	pModules->m_ExtraPluginInfo.Add (pExtraPluginInfo);
	pModules->m_Modules.Add (pModule);

	return TRUE;
}

void WcxModules::GetArchivePluginInfo (ArchivePluginInfo *ai)
{
	ai->nFormats = m_PluginInfo.nFormats;
	ai->pFormatInfo = m_PluginInfo.pFormatInfo;
}

bool WcxModules::GetDefaultCommand (int nFormat, int nCommand, char *lpCommand)
{
	return false;
}

WcxArchive::WcxArchive (WcxModule *pModule, int nModuleNum, const char *lpFileName)
{
	m_pModule = pModule;

	m_lpFileName = StrDuplicate (lpFileName);

	m_nModuleNum = nModuleNum;

	m_hArchive = NULL;
}

WcxArchive::~WcxArchive ()
{
	StrFree (m_lpFileName);
}

int WcxArchive::ConvertResult (int nResult)
{
	switch (nResult)
	{
		case E_END_ARCHIVE:
			return E_EOF;
		case E_BAD_ARCHIVE:
			return E_BROKEN;
		case E_BAD_DATA:
			return E_UNEXPECTED_EOF;
		case E_EREAD:
			return E_READ_ERROR;
	}

	return E_UNEXPECTED_EOF;
}

bool __stdcall WcxArchive::pOpenArchive ()
{
	tOpenArchiveData OpenArchiveData = {0};

	OpenArchiveData.ArcName = m_lpFileName;
	OpenArchiveData.OpenMode = PK_OM_LIST;

	m_hArchive = m_pModule->m_pfnOpenArchive (&OpenArchiveData);

	return m_hArchive!=NULL;
}

void __stdcall WcxArchive::pCloseArchive ()
{
	m_pModule->m_pfnCloseArchive (m_hArchive);
}

int __stdcall WcxArchive::pGetArchiveItem (ArchiveItemInfo *pItem)
{
	tHeaderData HeaderData;
	memset (&HeaderData, 0, sizeof (HeaderData));
	//strcpy (HeaderData.ArcName, m_lpFileName);

	int nResult = m_pModule->m_pfnReadHeader (m_hArchive, &HeaderData);

	if ( !nResult )
	{
		m_pModule->m_pfnProcessFile (m_hArchive, PK_SKIP, NULL, HeaderData.FileName);

	    CharToOem(HeaderData.FileName,pItem->pi.FindData.cFileName);
	    pItem->pi.FindData.dwFileAttributes = HeaderData.FileAttr;
	    pItem->pi.FindData.nFileSizeLow = HeaderData.UnpSize;

		FILETIME filetime;
		DosDateTimeToFileTime ((WORD)((DWORD)HeaderData.FileTime >> 16), (WORD)HeaderData.FileTime, &filetime);
		LocalFileTimeToFileTime (&filetime, &pItem->pi.FindData.ftLastWriteTime);
	    pItem->pi.FindData.ftCreationTime = pItem->pi.FindData.ftLastAccessTime = pItem->pi.FindData.ftLastWriteTime;

	    pItem->pi.PackSize = HeaderData.PackSize;
	    pItem->pi.CRC32 = HeaderData.FileCRC;

		return E_SUCCESS;
	}

	return ConvertResult (nResult);
}

int __stdcall WcxArchive::pGetArchiveType ()
{
	return (m_nModuleNum);
}
