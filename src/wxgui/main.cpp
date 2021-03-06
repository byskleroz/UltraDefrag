//////////////////////////////////////////////////////////////////////////
//
//  UltraDefrag - a powerful defragmentation tool for Windows NT.
//  Copyright (c) 2007-2015 Dmitri Arkhangelski (dmitriar@gmail.com).
//  Copyright (c) 2010-2013 Stefan Pendl (stefanpe@users.sourceforge.net).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
//////////////////////////////////////////////////////////////////////////

/**
 * @file main.cpp
 * @brief Main window.
 * @addtogroup MainWindow
 * @{
 */

// Ideas by Stefan Pendl <stefanpe@users.sourceforge.net>
// and Dmitri Arkhangelski <dmitriar@gmail.com>.

// =======================================================================
//                            Declarations
// =======================================================================

#include "main.h"

#if !defined(__GNUC__)
#include <new.h> // for _set_new_handler
#endif

// =======================================================================
//                            Global variables
// =======================================================================

MainFrame *g_mainFrame = NULL;
double g_scaleFactor = 1.0f;   // DPI-aware scaling factor
int g_iconSize;                // small icon size
HANDLE g_synchEvent = NULL;    // synchronization for threads
UINT g_TaskbarIconMsg;         // taskbar icon overlay setup on shell restart

// =======================================================================
//                             Web statistics
// =======================================================================

void *StatThread::Entry()
{
    bool enabled = true; wxString s;
    if(wxGetEnv(wxT("UD_DISABLE_USAGE_TRACKING"),&s))
        if(s.Cmp(wxT("1")) == 0) enabled = false;

    if(enabled){
#ifndef _WIN64
        Utils::GaRequest(wxT("/appstat/gui-x86.html"));
#else
    #if defined(_IA64_)
        Utils::GaRequest(wxT("/appstat/gui-ia64.html"));
    #else
        Utils::GaRequest(wxT("/appstat/gui-x64.html"));
    #endif
#endif
    }

    return NULL;
}

// =======================================================================
//                    Application startup and shutdown
// =======================================================================

#if !defined(__GNUC__)
static int out_of_memory_handler(size_t n)
{
    int choice = MessageBox(
        g_mainFrame ? (HWND)g_mainFrame->GetHandle() : NULL,
        wxT("Try to release some memory by closing\n")
        wxT("other applications and click Retry then\n")
        wxT("or click Cancel to terminate the program."),
        wxT("UltraDefrag: out of memory!"),
        MB_RETRYCANCEL | MB_ICONHAND);
    if(choice == IDCANCEL){
        winx_flush_dbg_log(FLUSH_IN_OUT_OF_MEMORY);
        if(g_mainFrame) // remove system tray icon
            delete g_mainFrame->m_systemTrayIcon;
        exit(3); return 0;
    }
    return 1;
}
#endif

/**
 * @brief Initializes the application.
 */
bool App::OnInit()
{
    // initialize wxWidgets
    SetAppName(wxT("UltraDefrag"));
    wxInitAllImageHandlers();
    if(!wxApp::OnInit())
        return false;

    // initialize udefrag library
    if(::udefrag_init_library() < 0){
        wxLogError(wxT("Initialization failed!"));
        return false;
    }

    // set out of memory handler
#if !defined(__GNUC__)
    winx_set_killer(out_of_memory_handler);
    _set_new_handler(out_of_memory_handler);
    _set_new_mode(1);
#endif

    // initialize debug log
    wxFileName logpath(wxT(".\\logs\\ultradefrag.log"));
    logpath.Normalize();
    wxSetEnv(wxT("UD_LOG_FILE_PATH"),logpath.GetFullPath());
    ::udefrag_set_log_file_path();

    // initialize logging
    m_log = new Log();

    // use global config object for internal settings
    wxFileConfig *cfg = new wxFileConfig(wxT(""),wxT(""),
        wxT("gui.ini"),wxT(""),wxCONFIG_USE_RELATIVE_PATH);
    wxConfigBase::Set(cfg);

    // enable i18n support
    InitLocale();

    // save report translation on setup
    wxString cmdLine(GetCommandLine());
    if(cmdLine.Find(wxT("--setup")) != wxNOT_FOUND){
        SaveReportTranslation();
        ::winx_flush_dbg_log(0);
        delete m_log;
        return false;
    }

    // start web statistics
    m_statThread = new StatThread();

    // check for administrative rights
    if(!Utils::CheckAdminRights()){
        wxMessageDialog dlg(NULL,
            wxT("Administrative rights are needed to run the program!"),
            wxT("UltraDefrag"),wxOK | wxICON_ERROR
        );
        dlg.ShowModal(); Cleanup();
        return false;
    }

    // create synchronization event
    g_synchEvent = ::CreateEvent(NULL,TRUE,FALSE,NULL);
    if(!g_synchEvent){
        letrace("cannot create synchronization event");
        wxMessageDialog dlg(NULL,
            wxT("Cannot create synchronization event!"),
            wxT("UltraDefrag"),wxOK | wxICON_ERROR
        );
        dlg.ShowModal(); Cleanup();
        return false;
    }

    // keep things DPI-aware
    HDC hdc = ::GetDC(NULL);
    if(hdc){
        g_scaleFactor = (double)::GetDeviceCaps(hdc,LOGPIXELSX) / 96.0f;
        ::ReleaseDC(NULL,hdc);
    }
    g_iconSize = wxSystemSettings::GetMetric(wxSYS_SMALLICON_X);
    if(g_iconSize < 20) g_iconSize = 16;
    else if(g_iconSize < 24) g_iconSize = 20;
    else if(g_iconSize < 32) g_iconSize = 24;
    else g_iconSize = 32;

    // support taskbar icon overlay setup on shell restart
    g_TaskbarIconMsg = ::RegisterWindowMessage(wxT("TaskbarButtonCreated"));
    if(!g_TaskbarIconMsg) letrace("cannot register TaskbarButtonCreated message");

    // create main window
    g_mainFrame = new MainFrame();
    g_mainFrame->Show(true);
    SetTopWindow(g_mainFrame);
    return true;
}

/**
 * @brief Frees application resources.
 */
void App::Cleanup()
{
    // flush configuration to disk
    delete wxConfigBase::Set(NULL);

    // stop web statistics
    delete m_statThread;

    // deinitialize logging
    ::winx_flush_dbg_log(0);
    delete m_log;
}

/**
 * @brief Deinitializes the application.
 */
int App::OnExit()
{
    Cleanup();
    return wxApp::OnExit();
}

IMPLEMENT_APP(App)

// =======================================================================
//                             Main window
// =======================================================================

/**
 * @brief Initializes main window.
 */
MainFrame::MainFrame()
    :wxFrame(NULL,wxID_ANY,wxT("UltraDefrag"))
{
    g_mainFrame = this;
    m_vList = NULL;
    m_cMap = NULL;
    m_currentJob = NULL;
    m_busy = false;
    m_paused = false;

    // set main window icon
    SetIcons(wxICON(appicon));

    // read configuration
    ReadAppConfiguration();
    ProcessCommandEvent(ID_ReadUserPreferences);

    // set main window title
    wxString *instdir = new wxString();
    if(wxGetEnv(wxT("UD_INSTALL_DIR"),instdir)){
        wxStandardPaths stdpaths;
        wxFileName exepath(stdpaths.GetExecutablePath());
        wxString cd = exepath.GetPath();
        itrace("current directory: %ls",cd.wc_str());
        itrace("installation directory: %ls",instdir->wc_str());
        if(cd.CmpNoCase(*instdir) == 0){
            itrace("current directory matches "
                "installation location, so it isn't portable");
            m_title = new wxString(wxT(VERSIONINTITLE));
        } else {
            itrace("current directory differs from "
                "installation location, so it is portable");
            m_title = new wxString(wxT(VERSIONINTITLE_PORTABLE));
        }
    } else {
        m_title = new wxString(wxT(VERSIONINTITLE_PORTABLE));
    }
    ProcessCommandEvent(ID_SetWindowTitle);
    delete instdir;

    // set main window size and position
    SetSize(m_width,m_height);
    if(!m_saved){
        CenterOnScreen();
        GetPosition(&m_x,&m_y);
    }
    Move(m_x,m_y);
    if(m_maximized) Maximize(true);

    SetMinSize(wxSize(DPI(MAIN_WINDOW_MIN_WIDTH),DPI(MAIN_WINDOW_MIN_HEIGHT)));

    // create menu, tool and status bars
    InitMenu(); InitToolbar(); InitStatusBar();

    // create list of volumes and cluster map
    // don't use live update style to avoid horizontal scrollbar appearance on list resizing
    m_splitter = new wxSplitterWindow(this,wxID_ANY,
        wxDefaultPosition,wxDefaultSize,
        wxSP_3D/* | wxSP_LIVE_UPDATE*/ | wxCLIP_CHILDREN);
    m_splitter->SetMinimumPaneSize(DPI(MIN_PANEL_HEIGHT));

    m_vList = new DrivesList(m_splitter,wxLC_REPORT | \
        wxLC_NO_SORT_HEADER | wxLC_HRULES | wxLC_VRULES | wxBORDER_NONE);
    //LONG_PTR style = ::GetWindowLongPtr((HWND)m_vList->GetHandle(),GWL_STYLE);
    //style |= LVS_SHOWSELALWAYS; ::SetWindowLongPtr((HWND)m_vList->GetHandle(),GWL_STYLE,style);

    m_cMap = new ClusterMap(m_splitter);

    m_splitter->SplitHorizontally(m_vList,m_cMap);

    int height = GetClientSize().GetHeight();
    int maxPanelHeight = height - DPI(MIN_PANEL_HEIGHT) - m_splitter->GetSashSize();
    if(m_separatorPosition < DPI(MIN_PANEL_HEIGHT)) m_separatorPosition = DPI(MIN_PANEL_HEIGHT);
    else if(m_separatorPosition > maxPanelHeight) m_separatorPosition = maxPanelHeight;
    m_splitter->SetSashPosition(m_separatorPosition);

    // update frame layout so we'll be able
    // to initialize list of volumes and
    // cluster map properly
    wxSizeEvent evt(wxSize(m_width,m_height));
    ProcessEvent(evt); m_splitter->UpdateSize();

    InitVolList();
    m_vList->SetFocus();

    // populate list of volumes
    m_listThread = new ListThread();

    // check the boot time defragmenter presence
    wxFileName btdFile(wxT("%SystemRoot%\\system32\\defrag_native.exe"));
    btdFile.Normalize();
    bool btd = btdFile.FileExists();
    m_menuBar->FindItem(ID_BootEnable)->Enable(btd);
    m_menuBar->FindItem(ID_BootScript)->Enable(btd);
    m_toolBar->EnableTool(ID_BootEnable,btd);
    m_toolBar->EnableTool(ID_BootScript,btd);
    if(btd && ::winx_bootex_check(L"defrag_native") > 0){
        m_menuBar->FindItem(ID_BootEnable)->Check(true);
        m_toolBar->ToggleTool(ID_BootEnable,true);
        m_btdEnabled = true;
    } else {
        m_btdEnabled = false;
    }

    // launch threads for time consuming operations
    m_btdThread = btd ? new BtdThread() : NULL;
    m_configThread = new ConfigThread();
    m_crashInfoThread = new CrashInfoThread();

    wxConfigBase *cfg = wxConfigBase::Get();
    int ulevel = (int)cfg->Read(wxT("/Upgrade/Level"),1);
    wxMenuItem *item = m_menuBar->FindItem(ID_HelpUpgradeNone + ulevel);
    if(item) item->Check();

    m_upgradeThread = new UpgradeThread(ulevel);

    // set system tray icon
    m_systemTrayIcon = new SystemTrayIcon();
    if(!m_systemTrayIcon->IsOk()){
        etrace("system tray icon initialization failed");
        wxSetEnv(wxT("UD_MINIMIZE_TO_SYSTEM_TRAY"),wxT("0"));
    }
    SetSystemTrayIcon(wxT("tray"),wxT("UltraDefrag"));

    // set localized text
    ProcessCommandEvent(ID_LocaleChange \
        + g_locale->GetLanguage());

    // allow disk processing
    m_jobThread = new JobThread();
}

/**
 * @brief Deinitializes main window.
 */
MainFrame::~MainFrame()
{
    // terminate threads
    ProcessCommandEvent(ID_Stop);
    ::SetEvent(g_synchEvent);
    delete m_btdThread;
    delete m_configThread;
    delete m_crashInfoThread;
    delete m_jobThread;
    delete m_listThread;

    // save configuration
    SaveAppConfiguration();
    delete m_upgradeThread;

    // remove system tray icon
    delete m_systemTrayIcon;

    // free resources
    ::CloseHandle(g_synchEvent);
    delete m_title;
}

/**
 * @brief Returns true if the program
 * is going to be terminated.
 * @param[in] time timeout interval,
 * in milliseconds.
 */
bool MainFrame::CheckForTermination(int time)
{
    DWORD result = ::WaitForSingleObject(g_synchEvent,(DWORD)time);
    if(result == WAIT_FAILED){
        letrace("synchronization failed");
        return true;
    }
    return result == WAIT_OBJECT_0 ? true : false;
}

// =======================================================================
//                             Event table
// =======================================================================

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
    // action menu
    EVT_MENU_RANGE(ID_Analyze, ID_MftOpt,
                   MainFrame::OnStartJob)
    EVT_MENU(ID_Pause, MainFrame::OnPause)
    EVT_MENU(ID_Stop,  MainFrame::OnStop)

    EVT_MENU(ID_ShowReport, MainFrame::OnShowReport)

    EVT_MENU(ID_Repeat,  MainFrame::OnRepeat)

    EVT_MENU(ID_SkipRem, MainFrame::OnSkipRem)
    EVT_MENU(ID_Rescan,  MainFrame::OnRescan)

    EVT_MENU(ID_Repair,  MainFrame::OnRepair)

    EVT_MENU(ID_Exit, MainFrame::OnExit)

    // settings menu
    EVT_MENU(ID_LangTranslateOnline, MainFrame::OnLangTranslateOnline)
    EVT_MENU(ID_LangTranslateOffline, MainFrame::OnLangTranslateOffline)
    EVT_MENU(ID_LangOpenFolder, MainFrame::OnLangOpenFolder)

    EVT_MENU_RANGE(ID_LocaleChange, ID_LocaleChange \
        + wxUD_LANGUAGE_LAST, MainFrame::OnLocaleChange)

    EVT_MENU(ID_GuiOptions, MainFrame::OnGuiOptions)

    EVT_MENU(ID_BootEnable, MainFrame::OnBootEnable)
    EVT_MENU(ID_BootScript, MainFrame::OnBootScript)

    // help menu
    EVT_MENU(ID_HelpContents,     MainFrame::OnHelpContents)
    EVT_MENU(ID_HelpBestPractice, MainFrame::OnHelpBestPractice)
    EVT_MENU(ID_HelpFaq,          MainFrame::OnHelpFaq)
    EVT_MENU(ID_HelpLegend,       MainFrame::OnHelpLegend)

    EVT_MENU(ID_DebugLog,  MainFrame::OnDebugLog)
    EVT_MENU(ID_DebugSend, MainFrame::OnDebugSend)

    EVT_MENU_RANGE(ID_HelpUpgradeNone,
                   ID_HelpUpgradeCheck,
                   MainFrame::OnHelpUpgrade)
    EVT_MENU(ID_HelpAbout, MainFrame::OnHelpAbout)

    // event handlers
    EVT_ACTIVATE(MainFrame::OnActivate)
    EVT_MOVE(MainFrame::OnMove)
    EVT_SIZE(MainFrame::OnSize)

    EVT_MENU(ID_AdjustListColumns, MainFrame::AdjustListColumns)
    EVT_MENU(ID_AdjustListHeight,  MainFrame::AdjustListHeight)
    EVT_MENU(ID_AdjustSystemTrayIcon,     MainFrame::AdjustSystemTrayIcon)
    EVT_MENU(ID_AdjustTaskbarIconOverlay, MainFrame::AdjustTaskbarIconOverlay)
    EVT_MENU(ID_BootChange,        MainFrame::OnBootChange)
    EVT_MENU(ID_CacheJob,          MainFrame::CacheJob)
    EVT_MENU(ID_DefaultAction,     MainFrame::OnDefaultAction)
    EVT_MENU(ID_DiskProcessingFailure, MainFrame::OnDiskProcessingFailure)
    EVT_MENU(ID_JobCompletion,     MainFrame::OnJobCompletion)
    EVT_MENU(ID_PopulateList,      MainFrame::PopulateList)
    EVT_MENU(ID_ReadUserPreferences,   MainFrame::ReadUserPreferences)
    EVT_MENU(ID_RedrawMap,         MainFrame::RedrawMap)
    EVT_MENU(ID_SelectAll,         MainFrame::SelectAll)
    EVT_MENU(ID_SetWindowTitle,    MainFrame::SetWindowTitle)
    EVT_MENU(ID_ShowUpgradeDialog, MainFrame::ShowUpgradeDialog)
    EVT_MENU(ID_Shutdown,          MainFrame::Shutdown)
    EVT_MENU(ID_UpdateStatusBar,   MainFrame::UpdateStatusBar)
    EVT_MENU(ID_UpdateVolumeInformation, MainFrame::UpdateVolumeInformation)
    EVT_MENU(ID_UpdateVolumeStatus,      MainFrame::UpdateVolumeStatus)
END_EVENT_TABLE()

// =======================================================================
//                            Event handlers
// =======================================================================

WXLRESULT MainFrame::MSWWindowProc(WXUINT msg,WXWPARAM wParam,WXLPARAM lParam)
{
    if(msg == g_TaskbarIconMsg){
        // handle shell restart
        PostCommandEvent(this,ID_AdjustTaskbarIconOverlay);
        return 0;
    }
    return wxFrame::MSWWindowProc(msg,wParam,lParam);
}

void MainFrame::SetWindowTitle(wxCommandEvent& event)
{
    if(event.GetString().IsEmpty()){
        if(CheckOption(wxT("UD_DRY_RUN"))){
            SetTitle(*m_title + wxT(" (dry run)"));
        } else {
            SetTitle(*m_title);
        }
    } else {
        SetTitle(event.GetString());
    }
}

void MainFrame::OnActivate(wxActivateEvent& event)
{
    /* suggested by Brian Gaff */
    if(event.GetActive() && m_vList)
        m_vList->SetFocus();
    event.Skip();
}

void MainFrame::OnMove(wxMoveEvent& event)
{
    if(!IsMaximized() && !IsIconized()){
        GetPosition(&m_x,&m_y);
        GetSize(&m_width,&m_height);
    }

    // hide window on minimization if system tray icon is turned on
    if(CheckOption(wxT("UD_MINIMIZE_TO_SYSTEM_TRAY")) && IsIconized()) Hide();

    event.Skip();
}

void MainFrame::OnSize(wxSizeEvent& event)
{
    if(!IsMaximized() && !IsIconized())
        GetSize(&m_width,&m_height);
    if(m_cMap) m_cMap->Refresh();
    event.Skip();
}

// =======================================================================
//                            Menu handlers
// =======================================================================

void MainFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
    Close(true);
}

// help menu handlers
void MainFrame::OnHelpContents(wxCommandEvent& WXUNUSED(event))
{
    Utils::OpenHandbook(wxT("index.html"));
}

void MainFrame::OnHelpBestPractice(wxCommandEvent& WXUNUSED(event))
{
    Utils::OpenHandbook(wxT("Tips.html"));
}

void MainFrame::OnHelpFaq(wxCommandEvent& WXUNUSED(event))
{
    Utils::OpenHandbook(wxT("FAQ.html"));
}

void MainFrame::OnHelpLegend(wxCommandEvent& WXUNUSED(event))
{
    Utils::OpenHandbook(wxT("GUI.html"),wxT("cluster_map_legend"));
}

/** @} */
