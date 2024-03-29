#pragma once

/*
panel.hpp

Parent class ��� �������
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

#include "scrobj.hpp"
#include "panelctype.hpp"

class DizList;

struct column_base
{
	unsigned __int64 type;
	int width;
	int width_type;
};

struct column: public column_base
{
	column():
		column_base()
	{}

	column(const column_base& rhs)
	{
		static_cast<column_base&>(*this) = rhs;
	}

	string title;
};

struct PanelViewSettings: noncopyable
{
	PanelViewSettings(): Flags() {}
	PanelViewSettings(PanelViewSettings&& rhs) noexcept: Flags() { *this = std::move(rhs); }
	MOVE_OPERATOR_BY_SWAP(PanelViewSettings);

	void swap(PanelViewSettings& rhs) noexcept
	{
		using std::swap;
		PanelColumns.swap(rhs.PanelColumns);
		StatusColumns.swap(rhs.StatusColumns);
		Name.swap(rhs.Name);
		swap(Flags, rhs.Flags);
	}

	FREE_SWAP(PanelViewSettings);

	PanelViewSettings clone() const
	{
		PanelViewSettings result;
		result.PanelColumns = PanelColumns;
		result.StatusColumns = StatusColumns;
		result.Name = Name;
		result.Flags = Flags;
		return result;
	}

	std::vector<column> PanelColumns;
	std::vector<column> StatusColumns;
	string Name;
	unsigned __int64 Flags;
};

enum
{
	PVS_FULLSCREEN            = 0x00000001,
	PVS_ALIGNEXTENSIONS       = 0x00000002,
	PVS_FOLDERALIGNEXTENSIONS = 0x00000004,
	PVS_FOLDERUPPERCASE       = 0x00000008,
	PVS_FILELOWERCASE         = 0x00000010,
	PVS_FILEUPPERTOLOWERCASE  = 0x00000020,
};

enum
{
	FILE_PANEL,
	TREE_PANEL,
	QVIEW_PANEL,
	INFO_PANEL
};

enum SORT_MODE
{
	UNSORTED,
	BY_NAME,
	BY_EXT,
	BY_MTIME,
	BY_CTIME,
	BY_ATIME,
	BY_SIZE,
	BY_DIZ,
	BY_OWNER,
	BY_COMPRESSEDSIZE,
	BY_NUMLINKS,
	BY_NUMSTREAMS,
	BY_STREAMSSIZE,
	BY_FULLNAME,
	BY_CHTIME,
	BY_CUSTOMDATA,

	SORTMODE_COUNT
};

enum {VIEW_0=0,VIEW_1,VIEW_2,VIEW_3,VIEW_4,VIEW_5,VIEW_6,VIEW_7,VIEW_8,VIEW_9};

enum
{
	DRIVE_SHOW_TYPE       = 0x00000001,
	DRIVE_SHOW_NETNAME    = 0x00000002,
	DRIVE_SHOW_LABEL      = 0x00000004,
	DRIVE_SHOW_FILESYSTEM = 0x00000008,
	DRIVE_SHOW_SIZE       = 0x00000010,
	DRIVE_SHOW_REMOVABLE  = 0x00000020,
	DRIVE_SHOW_PLUGINS    = 0x00000040,
	DRIVE_SHOW_CDROM      = 0x00000080,
	DRIVE_SHOW_SIZE_FLOAT = 0x00000100,
	DRIVE_SHOW_REMOTE     = 0x00000200,
	DRIVE_SORT_PLUGINS_BY_HOTKEY = 0x00000400,
};


enum {UPDATE_KEEP_SELECTION=1,UPDATE_SECONDARY=2,UPDATE_IGNORE_VISIBLE=4,UPDATE_DRAW_MESSAGE=8};

enum {NORMAL_PANEL,PLUGIN_PANEL};

class VMenu2;
class Edit;
struct PanelMenuItem;
class Viewer;

class DelayedDestroy
{
public:
	DelayedDestroy():
		destroyed(false),
		prevent_delete_count(0)
	{}

protected:
	virtual ~DelayedDestroy() { assert(destroyed); }

public:
	bool Destroy()
	{
		assert(!destroyed);
		destroyed = true;
		if (prevent_delete_count > 0)
		{
			return false;
		}
		else
		{
			delete this;
			return true;
		}
	}

	bool Destroyed() const { return destroyed; }

	int AddRef() { return ++prevent_delete_count; }

	int Release()
	{
		assert(prevent_delete_count > 0);
		if (--prevent_delete_count > 0 || !destroyed)
			return prevent_delete_count;
		else {
			delete this;
			return 0;
		}
	}

private:
	bool destroyed;
	int prevent_delete_count;

};

class DelayDestroy: noncopyable
{
public:
	DelayDestroy(DelayedDestroy *host):m_host(host) { m_host->AddRef(); }
	~DelayDestroy() { m_host->Release(); }
private:
	DelayedDestroy *m_host;
};

struct delayed_destroyer
{
	void operator()(DelayedDestroy* Object) { Object->Destroy(); }
};

struct PluginHandle;
class FilePanels;

class Panel:public ScreenObject, public DelayedDestroy, public std::enable_shared_from_this<Panel>
{
public:
	// TODO: make empty methods pure virtual, move empty implementations to dummy_panel class
	virtual void CloseFile() {}
	virtual void UpdateViewPanel() {}
	virtual void CompareDir() {}
	virtual void MoveToMouse(const MOUSE_EVENT_RECORD *MouseEvent) {}
	virtual void ClearSelection() {}
	virtual void SaveSelection() {}
	virtual void RestoreSelection() {}
	virtual void SortFileList(int KeepPosition) {}
	virtual void EditFilter() {}
	virtual bool FileInFilter(size_t idxItem) {return true;}
	virtual bool FilterIsEnabled() {return false;}
	virtual void ReadDiz(PluginPanelItem *ItemList=nullptr,int ItemLength=0, DWORD dwFlags=0) {}
	virtual void DeleteDiz(const string& Name,const string& ShortName) {}
	virtual void GetDizName(string &strDizName) const {}
	virtual void FlushDiz() {}
	virtual void CopyDiz(const string& Name,const string& ShortName,const string& DestName, const string& DestShortName,DizList *DestDiz) {}
	virtual bool IsDizDisplayed() { return false; }
	virtual bool IsColumnDisplayed(int Type) {return false;}
	virtual int GetColumnsCount() const { return 1;}
	virtual void SetReturnCurrentFile(int Mode) {}
	virtual void QViewDelTempName() {}
	virtual void GetOpenPanelInfo(OpenPanelInfo *Info) const {}
	virtual void SetPluginMode(PluginHandle* hPlugin,const string& PluginFile,bool SendOnFocus=false) {}
	virtual void SetPluginModified() {}
	virtual int ProcessPluginEvent(int Event,void *Param) {return FALSE;}
	virtual PluginHandle* GetPluginHandle() const {return nullptr;}
	virtual void SetTitle();
	virtual string GetTitle() const;
	virtual __int64 VMProcess(int OpCode,void *vParam=nullptr,__int64 iParam=0) override;
	virtual int SendKeyToPlugin(DWORD Key,bool Pred=false) {return FALSE;}
	virtual bool SetCurDir(const string& NewDir,bool ClosePanel,bool IsUpdated=true);
	virtual void ChangeDirToCurrent();
	virtual const string& GetCurDir() const;
	virtual size_t GetSelCount() const { return 0; }
	virtual size_t GetRealSelCount() const {return 0;}
	virtual int GetSelName(string *strName, DWORD &FileAttr, string *ShortName = nullptr, api::FAR_FIND_DATA *fd = nullptr) { return FALSE; }
	virtual void UngetSelName() {}
	virtual void ClearLastGetSelection() {}
	virtual unsigned __int64 GetLastSelectedSize() const { return -1; }
	virtual int GetCurName(string &strName, string &strShortName) const;
	virtual int GetCurBaseName(string &strName, string &strShortName) const;
	virtual int GetFileName(string &strName, int Pos, DWORD &FileAttr) const { return FALSE; }
	virtual int GetCurrentPos() const {return 0;}
	virtual void SetFocus();
	virtual void KillFocus();
	virtual void Update(int Mode) = 0;
	virtual bool UpdateIfChanged(bool Idle) {return false;}
	virtual void UpdateIfRequired() {}
	virtual void StartFSWatcher(bool got_focus=false, bool check_time=true) {}
	virtual void StopFSWatcher() {}
	virtual int FindPartName(const string& Name,int Next,int Direct=1) {return FALSE;}
	virtual bool GetPlainString(string& Dest, int ListPos) const { return false; }
	virtual int GoToFile(long idxItem) {return TRUE;}
	virtual int GoToFile(const string& Name,BOOL OnlyPartName=FALSE) {return TRUE;}
	virtual long FindFile(const string& Name,BOOL OnlyPartName=FALSE) {return -1;}
	virtual int IsSelected(const string& Name) {return FALSE;}
	virtual int IsSelected(size_t indItem) {return FALSE;}
	virtual long FindFirst(const string& Name) {return -1;}
	virtual long FindNext(int StartPos, const string& Name) {return -1;}
	virtual void SetSelectedFirstMode(bool) {}
	virtual bool GetSelectedFirstMode() const { return false; }
	virtual void SetViewMode(int ViewMode);
	virtual int GetPrevViewMode() const {return m_PrevViewMode;}
	virtual int GetPrevSortMode() const {return m_SortMode;}
	virtual bool GetPrevSortOrder() const { return m_ReverseSortOrder; }
	virtual bool GetPrevNumericSort() const { return m_NumericSort; }
	virtual void ChangeNumericSort(bool Mode) { SetNumericSort(Mode); }
	virtual bool GetPrevCaseSensitiveSort() const { return m_CaseSensitiveSort; }
	virtual void ChangeCaseSensitiveSort(bool Mode) {SetCaseSensitiveSort(Mode);}
	virtual bool GetPrevDirectoriesFirst() const { return m_DirectoriesFirst; }
	virtual void ChangeDirectoriesFirst(bool Mode) { SetDirectoriesFirst(Mode); }
	virtual void SetSortMode(int Mode, bool KeepOrder = false) {m_SortMode=Mode;}
	virtual void SetCustomSortMode(int SortMode, bool KeepOrder = false, bool InvertByDefault = false) {}
	virtual void ChangeSortOrder(bool Reverse) {SetSortOrder(Reverse);}
	virtual void IfGoHome(wchar_t Drive) {}
	virtual void UpdateKeyBar() = 0;
	virtual size_t GetFileCount() const { return 0; }
	virtual void Hide() override;
	virtual void Show() override;
	virtual void DisplayObject() override {}
	virtual Viewer* GetViewer(void) {return nullptr;}
	virtual Viewer* GetById(int ID) {(void)ID; return nullptr;}

	static void exclude_sets(string& mask);

	int GetMode() const { return m_PanelMode; }
	void SetMode(int Mode) {m_PanelMode=Mode;}
	int GetModalMode() const { return m_ModalMode; }
	void SetModalMode(int ModalMode) {m_ModalMode=ModalMode;}
	int GetViewMode() const { return m_ViewMode; }
	void SetPrevViewMode(int PrevViewMode) {m_PrevViewMode=PrevViewMode;}
	int GetSortMode() const { return m_SortMode; }
	bool GetNumericSort() const { return m_NumericSort; }
	void SetNumericSort(bool Mode) { m_NumericSort = Mode; }
	bool GetCaseSensitiveSort() const { return m_CaseSensitiveSort; }
	void SetCaseSensitiveSort(bool Mode) {m_CaseSensitiveSort = Mode;}
	bool GetDirectoriesFirst() const { return m_DirectoriesFirst; }
	void SetDirectoriesFirst(bool Mode) { m_DirectoriesFirst = Mode != 0; }
	bool GetSortOrder() const { return m_ReverseSortOrder; }
	void SetSortOrder(bool Reverse) {m_ReverseSortOrder = Reverse;}
	bool GetSortGroups() const { return m_SortGroups; }
	void SetSortGroups(bool Mode) {m_SortGroups=Mode;}
	bool GetShowShortNamesMode() const { return m_ShowShortNames; }
	void SetShowShortNamesMode(bool Mode) {m_ShowShortNames=Mode;}
	void InitCurDir(const string& CurDir);
	bool ExecShortcutFolder(int Pos, bool raw=false);
	bool ExecShortcutFolder(string& strShortcutFolder, const GUID& PluginGuid, const string& strPluginFile, const string& strPluginData, bool CheckType, bool TryClosest = true, bool Silent = false);
	bool SaveShortcutFolder(int Pos, bool Add) const;
	int SetPluginCommand(int Command,int Param1,void* Param2);
	int PanelProcessMouse(const MOUSE_EVENT_RECORD *MouseEvent,int &RetCode);
	void ChangeDisk();
	bool GetFocus() const { return m_Focus; }
	int GetType() const {return m_Type;}
	void SetUpdateMode(int Mode) {m_EnableUpdate=Mode;}
	bool MakeListFile(string &strListFileName,bool ShortNames,const string& Modifers);
	int SetCurPath();
	BOOL NeedUpdatePanel(const Panel *AnotherPanel);
	bool IsFullScreen() const { return (m_ViewSettings.Flags & PVS_FULLSCREEN) != 0; }
	void SetFullScreen() { m_ViewSettings.Flags |= PVS_FULLSCREEN; }
	bool CreateFullPathName(const string& Name, const string& ShortName, DWORD FileAttr, string &strDest, int UNC, int ShortNameAsIs = TRUE) const;


	static void EndDrag();

	int ProcessingPluginCommand;

protected:
	Panel(window_ptr Owner);
	virtual ~Panel();
	virtual void ClearAllItem(){}

	void FastFind(int FirstKey);
	void DrawSeparator(int Y);
	void ShowScreensCount();

	static bool IsDragging();
	FilePanels* Parent(void)const;

private:
	struct ShortcutInfo
	{
		string ShortcutFolder;
		string PluginFile;
		string PluginData;
		GUID PluginGuid;
	};
	bool GetShortcutInfo(ShortcutInfo& Info) const;
	int ChangeDiskMenu(int Pos,int FirstCall);
	int DisconnectDrive(const PanelMenuItem *item, VMenu2 &ChDisk);
	void RemoveHotplugDevice(const PanelMenuItem *item, VMenu2 &ChDisk);
	int ProcessDelDisk(wchar_t Drive, int DriveType,VMenu2 *ChDiskMenu);

	static void DragMessage(int X,int Y,int Move);

protected:
	PanelViewSettings m_ViewSettings;
	string m_CurDir;
	bool m_Focus;
	int m_Type;
	int m_EnableUpdate;
	int m_PanelMode;
	int m_SortMode;
	bool m_ReverseSortOrder;
	bool m_SortGroups;
	int m_PrevViewMode;
	int m_ViewMode;
	int m_CurTopFile;
	int m_CurFile;
	bool m_ShowShortNames;
	bool m_NumericSort;
	bool m_CaseSensitiveSort;
	bool m_DirectoriesFirst;
	int m_ModalMode;
	int m_PluginCommand;
	string m_PluginParam;

private:
	string strDragName;
};

class dummy_panel : public Panel
{
public:
	dummy_panel(window_ptr Owner): Panel(Owner){}
private:
	virtual void Update(int Mode) override {};
	virtual void UpdateKeyBar() override {}
};