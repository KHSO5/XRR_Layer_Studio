#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "bruker_reader.hpp"
#include "fft.hpp"
#include "fft_dialog_win.hpp"
#include "fit_dialog_win.hpp"
#include "material_picker_win.hpp"
#include "plot_win.hpp"
#include "project.hpp"
#include "simulation.hpp"

namespace fs=std::filesystem;
using xrr::wide;
namespace {
constexpr wchar_t mainClass[]=L"XrrLayerStudioMain_v251";
constexpr wchar_t plotClass[]=L"XrrLayerStudioPlot_v251";
constexpr wchar_t appTitle[]=L"XRR Layer Studio v2.5.1";
enum Id {
    ID_NEW=100,ID_OPEN_DATA,ID_OPEN_PROJECT,ID_SAVE_PROJECT,ID_SAVE_AS,
    ID_EXPORT_PNG,ID_EXPORT_SVG,ID_EXPORT_DATA,ID_EXPORT_LAYERS,ID_EXPORT_WINDOW,ID_EXIT,
    ID_RESET_PLOT,ID_FFT,ID_NOMINAL,ID_SHOW_SIMULATION,ID_ABOUT,ID_SCAN_COMBO,
    ID_INSERT_ABOVE,ID_INSERT_BELOW,ID_DELETE_LAYER,ID_MOVE_UP,ID_MOVE_DOWN,ID_FIT,
    ID_TABLE=200,ID_PLOT,ID_DETAILS,ID_STATUS,ID_WATERMARK,ID_LAYER_HELP,ID_CELL_EDIT
};
struct App;
struct PlotController {
    App* app=nullptr;
    bool dragging=false;
    POINT start{},current{};
};
template<class Function>
Function user32Function(const char* name) {
    const auto raw=GetProcAddress(GetModuleHandleW(L"user32.dll"),name);Function function=nullptr;
    static_assert(sizeof(function)==sizeof(raw));std::memcpy(&function,&raw,sizeof(function));return function;
}
UINT windowDpi(HWND w) {
    using GetDpiForWindowFn=UINT(WINAPI*)(HWND);
    const auto f=user32Function<GetDpiForWindowFn>("GetDpiForWindow");
    if(f&&w)return f(w);
    HDC dc=GetDC(nullptr);const UINT dpi=dc?static_cast<UINT>(GetDeviceCaps(dc,LOGPIXELSX)):96;
    if(dc)ReleaseDC(nullptr,dc);
    return dpi?dpi:96;
}
double dpiScale(HWND w){return static_cast<double>(windowDpi(w))/96.0;}
HFONT makeFont(UINT dpi,int pointTenths,int weight) {
    const int height=-std::max(1,MulDiv(pointTenths,static_cast<int>(dpi),720));
    return CreateFontW(height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
}
void adjustWindowRectForDpi(RECT& rectangle,DWORD style,BOOL menu,DWORD extendedStyle,UINT dpi) {
    using AdjustForDpiFn=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
    const auto f=user32Function<AdjustForDpiFn>("AdjustWindowRectExForDpi");
    if(!f||!f(&rectangle,style,menu,extendedStyle,dpi))AdjustWindowRectEx(&rectangle,style,menu,extendedStyle);
}
std::wstring formatNumber(double n,int digits=7) {
    std::wostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(digits)<<n;return o.str();
}
std::wstring commaNumber(std::size_t n) {
    auto s=std::to_wstring(n);
    for(std::ptrdiff_t i=static_cast<std::ptrdiff_t>(s.size())-3;i>0;i-=3)s.insert(static_cast<std::size_t>(i),1,L',');
    return s;
}
void message(HWND parent,const std::wstring& text,const std::wstring& title=L"XRR Layer Studio",UINT flags=MB_OK|MB_ICONERROR) {
    MessageBoxW(parent,text.c_str(),title.c_str(),flags);
}
std::wstring errorText(const std::exception& e){return wide(e.what());}
fs::path dialog(HWND owner,bool save,const wchar_t* title,const wchar_t* filter,const wchar_t* extension=L"",const fs::path& initial={}) {
    std::vector<wchar_t> name(32768,L'\0');
    if(!initial.empty()) {
        const auto source=initial.filename().wstring();
        const auto count=std::min(source.size(),name.size()-1);
        std::copy_n(source.data(),count,name.data());name[count]=L'\0';
    }
    OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=owner;d.lpstrTitle=title;d.lpstrFilter=filter;
    d.lpstrFile=name.data();d.nMaxFile=static_cast<DWORD>(name.size());d.lpstrDefExt=extension;
    d.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(save?GetSaveFileNameW(&d):GetOpenFileNameW(&d))return fs::path(name.data());
    if(CommDlgExtendedError())throw std::runtime_error("The Windows file dialog failed");
    return {};
}
std::wstring minimumDecimals(std::wstring text,std::size_t count) {
    const auto first=text.find_first_not_of(L" \t\r\n"),last=text.find_last_not_of(L" \t\r\n");
    if(first==std::wstring::npos)return {};
    text=text.substr(first,last-first+1);
    const auto exponent=text.find_first_of(L"eE");const auto end=exponent==std::wstring::npos?text.size():exponent;
    const auto decimal=text.find(L'.');const auto existing=decimal==std::wstring::npos||decimal>end?0:end-decimal-1;
    if(decimal==std::wstring::npos||decimal>end)text.insert(end,L"."+std::wstring(count,L'0'));
    else if(existing<count)text.insert(end,std::wstring(count-existing,L'0'));
    return text;
}
std::wstring rowText(const xrr::Layer& l,int row,int col) {
    switch(col) {
    case 0:return l.substrate?L"Substrate":L"Film "+std::to_wstring(row+1);
    case 1:return wide(l.material);case 2:return wide(l.density);
    case 3:return l.substrate?L"":wide(l.thickness);case 4:return minimumDecimals(wide(l.roughness),3);
    default:return {};
    }
}
void setItem(HWND table,int row,int col,const std::wstring& text) {
    if(col==0) {
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=const_cast<wchar_t*>(text.c_str());
        ListView_InsertItem(table,&item);
    }else ListView_SetItemText(table,row,col,const_cast<wchar_t*>(text.c_str()));
}
int selectedRow(HWND table) {
    return ListView_GetNextItem(table,-1,LVNI_SELECTED);
}
void selectRow(HWND table,int row) {
    if(row<0)return;
    ListView_SetItemState(table,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_EnsureVisible(table,row,FALSE);
}
void setStatus(HWND status,const std::wstring& text){SetWindowTextW(status,text.c_str());}

struct App {
    HINSTANCE instance{};
    HWND window{},plot{},table{},status{},watermark{},details{},scanCombo{},nominal{},showSimulation{},layerHelp{},resetPlotButton{},fftButton{},edit{};
    std::vector<HWND> toolbar,layerButtons;
    HFONT uiFont{},titleFont{},smallFont{},panelFont{},panelSmallFont{};
    HBRUSH background{},white{};
    xrr::Project project;
    std::optional<xrr::Scan> simulated;
    std::string simulationIssue="Load a measurement and complete the layer table";
    double simulationMilliseconds=0;
    fs::path projectPath,measurementPath;
    bool dirty=false,endingEdit=false;
    int editRow=-1,editColumn=-1;
    PlotController plotController{this};
    std::wstring startupInput,smokeScreenshot,smokeProject;

    ~App() {
        if(uiFont)DeleteObject(uiFont);
        if(titleFont)DeleteObject(titleFont);
        if(smallFont)DeleteObject(smallFont);
        if(panelFont)DeleteObject(panelFont);
        if(panelSmallFont)DeleteObject(panelSmallFont);
        if(background)DeleteObject(background);
        if(white)DeleteObject(white);
    }
    const xrr::Scan* active()const {
        return project.scans.empty()||project.activeScan>=project.scans.size()?nullptr:&project.scans[project.activeScan];
    }
    xrr::Scan* active() {
        return project.scans.empty()||project.activeScan>=project.scans.size()?nullptr:&project.scans[project.activeScan];
    }
    void updateTitle() {
        std::wstring t=appTitle;
        if(!projectPath.empty())t+=L" — "+projectPath.filename().wstring();
        else if(!measurementPath.empty())t+=L" — "+measurementPath.filename().wstring();
        if(dirty)t+=L" *";
        SetWindowTextW(window,t.c_str());
    }
    void markDirty(){dirty=true;updateTitle();}
    void applyFonts() {
        const auto set=[](HWND control,HFONT font){if(control&&font)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);};
        for(std::size_t i=0;i<toolbar.size();++i)set(toolbar[i],i==0?titleFont:i==1?smallFont:uiFont);
        set(resetPlotButton,uiFont);set(fftButton,uiFont);
        set(scanCombo,panelFont);set(nominal,panelFont);set(showSimulation,panelFont);
        for(std::size_t i=0;i<layerButtons.size();++i)set(layerButtons[i],i==0?titleFont:panelFont);
        set(layerHelp,panelSmallFont);set(table,panelFont);
        if(table)set(ListView_GetHeader(table),panelFont);
        set(details,panelSmallFont);set(status,smallFont);set(watermark,panelSmallFont);set(edit,panelFont);
    }
    void rebuildFonts(UINT dpi) {
        HFONT nextUi=makeFont(dpi,100,FW_NORMAL),nextTitle=makeFont(dpi,170,FW_SEMIBOLD),
              nextSmall=makeFont(dpi,90,FW_NORMAL),nextPanel=makeFont(dpi,120,FW_NORMAL),
              nextPanelSmall=makeFont(dpi,105,FW_NORMAL);
        if(!nextUi||!nextTitle||!nextSmall||!nextPanel||!nextPanelSmall) {
            for(HFONT font:{nextUi,nextTitle,nextSmall,nextPanel,nextPanelSmall})if(font)DeleteObject(font);
            return;
        }
        const HFONT oldUi=uiFont,oldTitle=titleFont,oldSmall=smallFont,oldPanel=panelFont,oldPanelSmall=panelSmallFont;
        uiFont=nextUi;titleFont=nextTitle;smallFont=nextSmall;panelFont=nextPanel;panelSmallFont=nextPanelSmall;
        applyFonts();
        for(HFONT font:{oldUi,oldTitle,oldSmall,oldPanel,oldPanelSmall})if(font)DeleteObject(font);
    }
    void createControls() {
        rebuildFonts(windowDpi(window));
        background=CreateSolidBrush(RGB(244,247,250));white=CreateSolidBrush(RGB(255,255,255));
        auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id)->HWND{
            HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
            SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);return h;
        };
        auto title=control(L"STATIC",L"XRR Layer Studio",SS_LEFT,0);toolbar.push_back(title);
        SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        auto subtitle=control(L"STATIC",L"Bruker D8 Advance · RAW4.00 / TXT · measured + Parratt-simulated XRR",SS_LEFT,0);toolbar.push_back(subtitle);
        SendMessageW(subtitle,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
        for(const auto& item:std::vector<std::pair<int,const wchar_t*>>{{ID_OPEN_DATA,L"Open data"},{ID_OPEN_PROJECT,L"Open project"},{ID_SAVE_PROJECT,L"Save project"},{ID_EXPORT_PNG,L"Export plot"}}) {
            auto h=control(L"BUTTON",item.second,BS_PUSHBUTTON|BS_FLAT,item.first);toolbar.push_back(h);
        }
        resetPlotButton=control(L"BUTTON",L"Reset view",BS_PUSHBUTTON|BS_FLAT,ID_RESET_PLOT);
        fftButton=control(L"BUTTON",L"FFT thickness",BS_PUSHBUTTON|BS_FLAT,ID_FFT);
        scanCombo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,ID_SCAN_COMBO);
        nominal=control(L"BUTTON",L"Show full nominal scan range",BS_AUTOCHECKBOX,ID_NOMINAL);
        showSimulation=control(L"BUTTON",L"Show layer-stack simulation",BS_AUTOCHECKBOX,ID_SHOW_SIMULATION);
        Button_SetCheck(showSimulation,BST_CHECKED);
        plot=CreateWindowExW(0,plotClass,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PLOT)),instance,&plotController);
        auto stackTitle=control(L"STATIC",L"Layer stack",SS_LEFT,0);layerButtons.push_back(stackTitle);
        SendMessageW(stackTitle,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        layerHelp=control(L"STATIC",L"Top surface → substrate · Insert opens searchable presets · Fit opens variable and 2θ filters",SS_LEFT,ID_LAYER_HELP);
        SendMessageW(layerHelp,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
        table=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
                            0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABLE)),instance,nullptr);
        SendMessageW(table,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);
        ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(table,L"Explorer",nullptr);
        const wchar_t* headers[]={L"Position",L"Material / note",L"Density (g/cm³)",L"Expected t (nm)",L"Roughness (nm)"};
        for(int i=0;i<5;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;c.pszText=const_cast<wchar_t*>(headers[i]);c.cx=100;c.fmt=i<2?LVCFMT_LEFT:LVCFMT_RIGHT;ListView_InsertColumn(table,i,&c);}
        for(const auto& item:std::vector<std::pair<int,const wchar_t*>>{
            {ID_INSERT_ABOVE,L"+ Above"},{ID_INSERT_BELOW,L"+ Below"},{ID_DELETE_LAYER,L"Delete"},
            {ID_MOVE_UP,L"Move up"},{ID_MOVE_DOWN,L"Move down"},{ID_EXPORT_LAYERS,L"Export table"},
            {ID_FIT,L"Fit selected variables..."}}) {
            auto h=control(L"BUTTON",item.second,BS_PUSHBUTTON,item.first);layerButtons.push_back(h);
        }
        details=control(L"STATIC",L"No measurement loaded.",SS_LEFT,ID_DETAILS);
        SendMessageW(details,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
        status=control(L"STATIC",L"Open a Bruker file · drag a rectangle on the plot to zoom · Ctrl-click two measured points to estimate thickness.",SS_LEFT|SS_CENTERIMAGE,ID_STATUS);
        SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
        watermark=control(L"STATIC",L"@KHSO5 All rights reserved.",SS_RIGHT|SS_CENTERIMAGE,ID_WATERMARK);
        SendMessageW(watermark,WM_SETFONT,reinterpret_cast<WPARAM>(panelSmallFont),TRUE);
        applyFonts();DragAcceptFiles(window,TRUE);refreshTable(0);refreshScans();layout();
    }
    void createMenu() {
        HMENU bar=CreateMenu(),file=CreatePopupMenu(),view=CreatePopupMenu(),analysis=CreatePopupMenu(),fit=CreatePopupMenu(),help=CreatePopupMenu();
        AppendMenuW(file,MF_STRING,ID_NEW,L"&New project\tCtrl+N");
        AppendMenuW(file,MF_STRING,ID_OPEN_DATA,L"&Open measurement...\tCtrl+O");
        AppendMenuW(file,MF_STRING,ID_OPEN_PROJECT,L"Open &project...\tCtrl+Shift+O");
        AppendMenuW(file,MF_SEPARATOR,0,nullptr);
        AppendMenuW(file,MF_STRING,ID_SAVE_PROJECT,L"&Save project\tCtrl+S");
        AppendMenuW(file,MF_STRING,ID_SAVE_AS,L"Save project &as...");
        AppendMenuW(file,MF_SEPARATOR,0,nullptr);
        AppendMenuW(file,MF_STRING,ID_EXPORT_PNG,L"Export plot as &PNG...");
        AppendMenuW(file,MF_STRING,ID_EXPORT_SVG,L"Export plot as S&VG...");
        AppendMenuW(file,MF_STRING,ID_EXPORT_DATA,L"Export plot &data CSV...");
        AppendMenuW(file,MF_STRING,ID_EXPORT_LAYERS,L"Export &layer table CSV...");
        AppendMenuW(file,MF_STRING,ID_EXPORT_WINDOW,L"Export whole window PNG...");
        AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,ID_EXIT,L"E&xit");
        AppendMenuW(view,MF_STRING,ID_RESET_PLOT,L"&Reset plot\tR");
        AppendMenuW(view,MF_STRING|MF_UNCHECKED,ID_NOMINAL,L"Show full nominal scan range");
        AppendMenuW(view,MF_STRING|MF_CHECKED,ID_SHOW_SIMULATION,L"Show layer-stack simulation");
        AppendMenuW(analysis,MF_STRING,ID_FFT,L"&FFT thickness analysis...\tCtrl+T");
        AppendMenuW(fit,MF_STRING,ID_FIT,L"Run &Genetic Algorithm...\tCtrl+F");
        AppendMenuW(help,MF_STRING,ID_ABOUT,L"&About...");
        AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(file),L"&File");
        AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"&View");
        AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(analysis),L"&Analysis");
        AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(fit),L"&Fit");
        AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(help),L"&Help");
        SetMenu(window,bar);
    }
    void layout() {
        if(!window)return;
        RECT r{};GetClientRect(window,&r);const int w=r.right,h=r.bottom;const double s=dpiScale(window);
        const auto px=[&](double logical){return static_cast<int>(std::lround(logical*s));};
        const int margin=px(16),toolsY=px(72),toolsH=px(40),statusH=px(32);
        auto move=[&](HWND c,int x,int y,int cw,int ch){if(c)MoveWindow(c,x,y,std::max(1,cw),std::max(1,ch),TRUE);};
        if(toolbar.size()>=6) {
            move(toolbar[0],margin,px(10),px(500),px(34));
            move(toolbar[1],margin,px(43),px(650),px(20));
            const int bw=px(116),gap=px(8);int x=w-margin-4*bw-3*gap;
            for(std::size_t i=2;i<6;++i){move(toolbar[i],x,toolsY,bw,toolsH);x+=bw+gap;}
        }
        move(resetPlotButton,margin,toolsY,px(128),toolsH);
        move(fftButton,margin+px(136),toolsY,px(142),toolsH);
        const int panelY=toolsY+toolsH+px(10),panelH=h-panelY-statusH-margin;
        int rightW=static_cast<int>(std::lround(static_cast<double>(w)*0.43));
        const int minimumRight=w<px(1100)?px(500):px(580);rightW=std::clamp(rightW,minimumRight,px(820));
        const int leftW=w-rightW-3*margin,rightX=leftW+2*margin;
        move(plot,margin,panelY,leftW,panelH);
        if(layerButtons.size()>=8) {
            const bool compact=panelH<px(600);
            const int titleH=px(compact?32:38),helpH=px(compact?28:34);
            move(layerButtons[0],rightX,panelY,rightW,titleH);
            move(layerHelp,rightX,panelY+titleH,rightW,helpH);
            const int selectorY=panelY+px(compact?64:76);
            move(scanCombo,rightX,selectorY,rightW*45/100,px(230));
            move(nominal,rightX+rightW*47/100,selectorY,rightW*53/100,px(compact?28:32));
            move(showSimulation,rightX,selectorY+px(compact?32:36),rightW,px(compact?28:32));
            const int tableY=selectorY+px(compact?62:74),detailH=px(compact?48:68);
            const int buttonH=px(compact?30:36),gap=px(compact?6:8),fitH=px(compact?36:42);
            const int footerH=gap+buttonH+gap+buttonH+gap+fitH+gap+detailH;
            const int tableH=std::max(px(48),panelH-(tableY-panelY)-footerH);
            move(table,rightX,tableY,rightW,tableH);
            const int by=tableY+tableH+gap,bw=(rightW-2*gap)/3;
            move(layerButtons[1],rightX,by,bw,buttonH);move(layerButtons[2],rightX+bw+gap,by,bw,buttonH);
            move(layerButtons[3],rightX+2*(bw+gap),by,bw,buttonH);
            const int secondY=by+buttonH+gap;
            move(layerButtons[4],rightX,secondY,bw,buttonH);move(layerButtons[5],rightX+bw+gap,secondY,bw,buttonH);
            move(layerButtons[6],rightX+2*(bw+gap),secondY,bw,buttonH);
            const int fitY=secondY+buttonH+gap;
            move(layerButtons[7],rightX,fitY,rightW,fitH);
            move(details,rightX,fitY+fitH+gap,rightW,detailH);
            RECT tr{};GetClientRect(table,&tr);int tw=std::max(300,static_cast<int>(tr.right)-GetSystemMetrics(SM_CXVSCROLL)-2);
            const int cols[]={static_cast<int>(tw*.14),static_cast<int>(tw*.26),static_cast<int>(tw*.20),static_cast<int>(tw*.20),0};
            int used=0;for(int i=0;i<4;++i){ListView_SetColumnWidth(table,i,cols[i]);used+=cols[i];}ListView_SetColumnWidth(table,4,std::max(90,tw-used));
        }
        const int watermarkW=px(245),statusGap=px(12);
        move(status,margin,h-statusH,std::max(px(120),w-2*margin-watermarkW-statusGap),statusH);
        move(watermark,w-margin-watermarkW,h-statusH,watermarkW,statusH);
        InvalidateRect(plot,nullptr,FALSE);
    }
    void refreshTable(int select=-1) {
        if(edit)finishEdit(false);
        ListView_DeleteAllItems(table);
        for(std::size_t i=0;i<project.layers.rows.size();++i)
            for(int c=0;c<5;++c)setItem(table,static_cast<int>(i),c,rowText(project.layers.rows[i],static_cast<int>(i),c));
        if(select>=0)selectRow(table,std::min(select,static_cast<int>(project.layers.rows.size()-1)));
        updateLayerButtons();
    }
    void updateLayerButtons() {
        if(layerButtons.size()<8)return;
        const int row=selectedRow(table),last=static_cast<int>(project.layers.rows.size())-1;
        const bool film=row>=0&&row<last,room=project.layers.rows.size()<257;
        EnableWindow(layerButtons[1],row>=0&&room);
        EnableWindow(layerButtons[2],film&&room); // Never insert below Substrate.
        EnableWindow(layerButtons[3],film);
        EnableWindow(layerButtons[4],film&&row>0);
        EnableWindow(layerButtons[5],film&&row+1<last);
        EnableWindow(layerButtons[6],TRUE);
        EnableWindow(layerButtons[7],active()!=nullptr&&simulated.has_value());
    }
    void refreshScans() {
        selectedPoint1=selectedPoint2=-1;currentHover=-1;
        SendMessageW(scanCombo,CB_RESETCONTENT,0,0);
        for(const auto& s:project.scans)SendMessageW(scanCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(s.name).c_str()));
        if(!project.scans.empty()){project.activeScan=std::min(project.activeScan,project.scans.size()-1);SendMessageW(scanCombo,CB_SETCURSEL,project.activeScan,0);}
        EnableWindow(scanCombo,project.scans.size()>1);
        updateDetails();InvalidateRect(plot,nullptr,FALSE);
    }
    void updateDetails() {
        const auto* s=active();std::wstring t;
        if(!s)t=L"No measurement loaded. Complete the layer table now or after opening data.";
        else {
            double lo=0,hi=0;if(!s->points.empty()){lo=s->points.front().twoTheta;hi=s->points.back().twoTheta;}
            t=L"Format: "+wide(s->format)+L"     Measured: "+commaNumber(s->points.size())+
              L"     −9999 skipped: "+commaNumber(s->unmeasured)+L"\r\n2θ data range: "+
              (s->points.empty()?L"—":formatNumber(lo)+L"–"+formatNumber(hi)+L"°")+
              L"     Step: "+formatNumber(s->step)+L"°"+
              (s->secondaryRatio>0?L"     Spectrum: Kα1 + Kα2":L"")+L"\r\n";
            if(simulated)t+=(project.poisson.enabled?L"Fitted model: Parratt + Poisson nuisance + resolution · same ":L"Simulation: Parratt + Nevot–Croce · same ")+commaNumber(simulated->points.size())+
                            L" coordinates · "+formatNumber(simulationMilliseconds,3)+L" ms";
            else t+=L"Simulation pending: "+wide(simulationIssue);
        }
        SetWindowTextW(details,t.c_str());
    }
    void recalculateSimulation() {
        simulated.reset();simulationMilliseconds=0;
        if(!active())simulationIssue="load a measurement";
        else {
            const auto started=std::chrono::steady_clock::now();
            try {
                simulated=xrr::simulateXrr(*active(),project.layers,project.poisson);simulationIssue.clear();
                simulationMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            }catch(const std::exception& e){simulationIssue=e.what();}
        }
        updateDetails();updateLayerButtons();InvalidateRect(plot,nullptr,FALSE);
    }
    void resetPlot(){project.view=xrr::autoView(active(),Button_GetCheck(nominal)==BST_CHECKED);selectedPoint1=selectedPoint2=-1;currentHover=-1;InvalidateRect(plot,nullptr,FALSE);}
    xrr::PlotState plotState()const {
        const auto* visible=(showSimulation&&Button_GetCheck(showSimulation)==BST_CHECKED&&simulated)?&*simulated:nullptr;
        xrr::PlotState state;state.scan=active();state.simulated=visible;state.view=project.view;
        state.hovered=plotController.dragging?-1:currentHover;state.mouse=mouse;
        state.selectedPoint1=selectedPoint1;state.selectedPoint2=selectedPoint2;
        state.zoomSelecting=plotController.dragging;state.zoomStart=plotController.start;state.zoomEnd=plotController.current;
        return state;
    }
    std::ptrdiff_t currentHover=-1,selectedPoint1=-1,selectedPoint2=-1;POINT mouse{};
    void paintPlotWindow(HDC target,int w,int h) {
        HDC mem=CreateCompatibleDC(target);HBITMAP bmp=CreateCompatibleBitmap(target,w,h);
        HBITMAP old=static_cast<HBITMAP>(SelectObject(mem,bmp));
        {Gdiplus::Graphics g(mem);xrr::paintPlot(g,w,h,dpiScale(plot),plotState());}
        BitBlt(target,0,0,w,h,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);
    }
    void hoverAt(POINT p) {
        mouse=p;currentHover=-1;const auto* s=active();if(!s){InvalidateRect(plot,nullptr,FALSE);return;}
        RECT r{};GetClientRect(plot,&r);const auto box=xrr::plotBox(r.right,r.bottom,dpiScale(plot));
        if(p.x<box.left||p.x>box.left+box.width||p.y<box.top||p.y>box.top+box.height){InvalidateRect(plot,nullptr,FALSE);return;}
        const double x=project.view.xmin+(p.x-box.left)/box.width*(project.view.xmax-project.view.xmin);
        double best=1e300;
        for(std::size_t i=0;i<s->points.size();++i)if(s->points[i].intensity>0) {
            const double d=std::abs(s->points[i].twoTheta-x);
            if(d<best){best=d;currentHover=static_cast<std::ptrdiff_t>(i);}
        }
        if(best/(project.view.xmax-project.view.xmin)*box.width>20)currentHover=-1;
        InvalidateRect(plot,nullptr,FALSE);
    }
    bool insidePlot(POINT p)const {
        RECT rectangle{};GetClientRect(plot,&rectangle);const auto box=xrr::plotBox(rectangle.right,rectangle.bottom,dpiScale(plot));
        return p.x>=box.left&&p.x<=box.left+box.width&&p.y>=box.top&&p.y<=box.top+box.height;
    }
    void updateZoomBox(POINT p) {
        plotController.current=p;mouse=p;currentHover=-1;
        InvalidateRect(plot,nullptr,FALSE);
    }
    void finishZoomBox(POINT p) {
        plotController.current=p;mouse=p;RECT rectangle{};GetClientRect(plot,&rectangle);
        const auto box=xrr::plotBox(rectangle.right,rectangle.bottom,dpiScale(plot));const double threshold=8.0*dpiScale(plot);
        const double left=std::clamp(static_cast<double>(std::min(plotController.start.x,p.x)),box.left,box.left+box.width);
        const double right=std::clamp(static_cast<double>(std::max(plotController.start.x,p.x)),box.left,box.left+box.width);
        const double top=std::clamp(static_cast<double>(std::min(plotController.start.y,p.y)),box.top,box.top+box.height);
        const double bottom=std::clamp(static_cast<double>(std::max(plotController.start.y,p.y)),box.top,box.top+box.height);
        if(right-left<threshold||bottom-top<threshold) {
            hoverAt(p);setStatus(status,L"Zoom unchanged. Drag a rectangle to zoom, or Ctrl-click two measured points to estimate thickness.");return;
        }
        const double xSpan=project.view.xmax-project.view.xmin,ySpan=project.view.ymax-project.view.ymin;
        xrr::View next{
            project.view.xmin+(left-box.left)/box.width*xSpan,
            project.view.xmin+(right-box.left)/box.width*xSpan,
            project.view.ymax-(bottom-box.top)/box.height*ySpan,
            project.view.ymax-(top-box.top)/box.height*ySpan
        };
        if(next.xmax-next.xmin<=1.0e-10||next.ymax-next.ymin<=1.0e-6||next.ymin< -300||next.ymax>300) {
            setStatus(status,L"The selected zoom rectangle is too small.");InvalidateRect(plot,nullptr,FALSE);return;
        }
        project.view=next;currentHover=-1;InvalidateRect(plot,nullptr,FALSE);
        setStatus(status,L"Box zoom: 2θ "+formatNumber(next.xmin)+L"–"+formatNumber(next.xmax)+L"°. Double-click or use Reset view to restore the full range.");
    }
    std::ptrdiff_t nearestMeasuredPoint(POINT p)const {
        const auto* scan=active();if(!scan||!insidePlot(p))return -1;
        RECT rectangle{};GetClientRect(plot,&rectangle);const auto box=xrr::plotBox(rectangle.right,rectangle.bottom,dpiScale(plot));
        const double xSpan=project.view.xmax-project.view.xmin,ySpan=project.view.ymax-project.view.ymin;
        const double maximumDistance=26.0*dpiScale(plot),maximumSquared=maximumDistance*maximumDistance;double best=maximumSquared;
        std::ptrdiff_t selected=-1;
        for(std::size_t i=0;i<scan->points.size();++i) {
            const auto& point=scan->points[i];if(point.intensity<=0||!std::isfinite(point.intensity))continue;
            const double logIntensity=std::log10(point.intensity);
            if(point.twoTheta<project.view.xmin||point.twoTheta>project.view.xmax||logIntensity<project.view.ymin||logIntensity>project.view.ymax)continue;
            const double x=box.left+(point.twoTheta-project.view.xmin)/xSpan*box.width;
            const double y=box.top+(project.view.ymax-logIntensity)/ySpan*box.height;
            const double dx=x-p.x,dy=y-p.y,distance=dx*dx+dy*dy;
            if(distance<best){best=distance;selected=static_cast<std::ptrdiff_t>(i);}
        }
        return selected;
    }
    void selectThicknessPoint(POINT p) {
        const auto* scan=active();if(!scan){setStatus(status,L"Open a measurement before selecting fringe points.");return;}
        const auto index=nearestMeasuredPoint(p);
        if(index<0){setStatus(status,L"Ctrl-click closer to the measured curve to select a point.");MessageBeep(MB_ICONINFORMATION);return;}
        const auto& point=scan->points[static_cast<std::size_t>(index)];
        if(selectedPoint1<0||selectedPoint2>=0) {
            selectedPoint1=index;selectedPoint2=-1;currentHover=-1;InvalidateRect(plot,nullptr,FALSE);
            setStatus(status,L"Point 1 selected at 2θ = "+formatNumber(point.twoTheta,9)+L"°. Ctrl-click a second adjacent fringe point.");return;
        }
        if(index==selectedPoint1){setStatus(status,L"Select a different measured point for point 2.");MessageBeep(MB_ICONINFORMATION);return;}
        selectedPoint2=index;currentHover=-1;InvalidateRect(plot,nullptr,FALSE);UpdateWindow(plot);
        try {
            const auto& first=scan->points[static_cast<std::size_t>(selectedPoint1)];
            const auto result=xrr::thicknessFromFringePoints(first.twoTheta,point.twoTheta,scan->wavelength);
            const std::wstring resultText=L"Point 1: 2θ = "+formatNumber(first.twoTheta,10)+L"°\nPoint 2: 2θ = "+formatNumber(point.twoTheta,10)+
                L"°\n\nΔ(2θ) = "+formatNumber(result.twoThetaDifferenceDegrees,10)+L"°\n|Δqz| = "+formatNumber(result.qDifferenceInverseAngstrom,10)+
                L" Å⁻¹\n\nEstimated single-layer thickness = "+formatNumber(result.thicknessNm,10)+
                L" nm\n\nUsing t = 2π / |Δqz|. This assumes the selected points are adjacent Kiessig fringes (Δm = 1). If they span N fringe periods, multiply the displayed thickness by N.";
            message(window,resultText,L"Two-point thickness estimate",MB_OK|MB_ICONINFORMATION);
            setStatus(status,L"Two-point thickness estimate: "+formatNumber(result.thicknessNm,8)+L" nm from Δ(2θ) = "+formatNumber(result.twoThetaDifferenceDegrees,8)+L"°.");
        }catch(const std::exception& e){message(window,L"Could not calculate thickness.\n\n"+errorText(e),L"Two-point thickness estimate");}
    }
    bool finishEdit(bool commit) {
        if(!edit||endingEdit)return true;
        endingEdit=true;
        if(commit) {
            const int n=GetWindowTextLengthW(edit);std::wstring w(static_cast<std::size_t>(n)+1,L'\0');
            if(n)GetWindowTextW(edit,w.data(),n+1);
            w.resize(static_cast<std::size_t>(n));
            auto value=xrr::utf8(w);auto& layer=project.layers.rows[static_cast<std::size_t>(editRow)];
            std::string* cell=editColumn==1?&layer.material:editColumn==2?&layer.density:editColumn==3?&layer.thickness:&layer.roughness;
            const auto old=*cell;*cell=value;const auto error=project.layers.cellError(static_cast<std::size_t>(editRow),editColumn);
            if(!error.empty()) {
                *cell=old;endingEdit=false;MessageBeep(MB_ICONWARNING);
                setStatus(status,L"Invalid value: "+wide(error));SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);return false;
            }
            project.poisson=xrr::PoissonModel{};setItem(table,editRow,editColumn,rowText(layer,editRow,editColumn));markDirty();recalculateSimulation();
            setStatus(status,simulated?L"Layer table updated; XRR simulation recalculated.":
                      L"Layer table updated. Simulation pending: "+wide(simulationIssue));
        }
        HWND old=edit;edit=nullptr;editRow=editColumn=-1;RemoveWindowSubclass(old,editProc,1);DestroyWindow(old);endingEdit=false;return true;
    }
    void startEdit(int row,int column) {
        if(row<0||static_cast<std::size_t>(row)>=project.layers.rows.size()||column<1||column>4)return;
        if(project.layers.rows[static_cast<std::size_t>(row)].substrate&&column==3) {
            setStatus(status,L"Substrate is semi-infinite; its thickness is fixed as blank.");return;
        }
        if(edit&&!finishEdit(true))return;
        RECT cell{};ListView_GetSubItemRect(table,row,column,LVIR_BOUNDS,&cell);
        editRow=row;editColumn=column;
        edit=CreateWindowExW(0,L"EDIT",rowText(project.layers.rows[static_cast<std::size_t>(row)],row,column).c_str(),
                           WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,cell.left+1,cell.top+1,
                           std::max(50,static_cast<int>(cell.right-cell.left-2)),std::max(22,static_cast<int>(cell.bottom-cell.top-2)),table,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CELL_EDIT)),instance,nullptr);
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(panelFont),TRUE);
        SendMessageW(edit,EM_SETLIMITTEXT,column==1?4096:128,0);
        SetWindowSubclass(edit,editProc,1,reinterpret_cast<DWORD_PTR>(this));
        SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);
    }
    static LRESULT CALLBACK editProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref) {
        auto* a=reinterpret_cast<App*>(ref);
        if(msg==WM_KEYDOWN) {
            if(wp==VK_ESCAPE){a->finishEdit(false);return 0;}
            if(wp==VK_RETURN){a->finishEdit(true);return 0;}
            if(wp==VK_TAB) {
                const int row=a->editRow,col=a->editColumn;const bool back=GetKeyState(VK_SHIFT)<0;
                if(a->finishEdit(true)){int nr=row,nc=col+(back?-1:1);if(nc<1){nc=4;--nr;}if(nc>4){nc=1;++nr;}
                    if(nr>=0&&nr<static_cast<int>(a->project.layers.rows.size())&&a->project.layers.rows[static_cast<std::size_t>(nr)].substrate&&nc==3)
                        nc+=back?-1:1;
                    if(nr>=0&&nr<static_cast<int>(a->project.layers.rows.size()))a->startEdit(nr,nc);}
                return 0;
            }
        }
        return DefSubclassProc(h,msg,wp,lp);
    }
    bool maybeSave() {
        if(!dirty)return true;
        const int answer=MessageBoxW(window,L"The current project has unsaved changes.\n\nSave them now?",appTitle,MB_YESNOCANCEL|MB_ICONQUESTION);
        if(answer==IDCANCEL)return false;
        if(answer==IDYES)return save(false);
        return true;
    }
    bool save(bool choose) {
        if(!finishEdit(true))return false;
        try {
            if(choose||projectPath.empty()) {
                auto p=dialog(window,true,L"Save XRR project",L"XRR Studio Project (*.xrrproj)\0*.xrrproj\0All files\0*.*\0\0",L"xrrproj",projectPath);
                if(p.empty())return false;
                projectPath=p;
            }
            xrr::saveProject(projectPath,project);dirty=false;updateTitle();
            setStatus(status,L"Project saved: "+projectPath.wstring());return true;
        }catch(const std::exception& e){message(window,L"Could not save the project.\n\n"+errorText(e));return false;}
    }
    void newProject() {
        if(!maybeSave())return;
        finishEdit(false);project=xrr::Project{};projectPath.clear();measurementPath.clear();dirty=false;
        Button_SetCheck(nominal,BST_UNCHECKED);resetPlot();refreshTable(0);recalculateSimulation();refreshScans();updateTitle();
        setStatus(status,L"New project. Add film rows above the fixed Substrate row.");
    }
    void openData(const fs::path& selected={}) {
        try {
            fs::path p=selected.empty()?dialog(window,false,L"Open one Bruker measurement",
                    L"Bruker RAW4 / TXT (*.raw;*.txt)\0*.raw;*.txt\0RAW4 files (*.raw)\0*.raw\0TXT exports (*.txt)\0*.txt\0All files\0*.*\0\0"):selected;
            if(p.empty())return;
            auto scans=xrr::readBruker(p);
            project.scans=std::move(scans);project.activeScan=0;project.poisson=xrr::PoissonModel{};measurementPath=p;projectPath.clear();
            Button_SetCheck(nominal,BST_UNCHECKED);resetPlot();recalculateSimulation();refreshScans();markDirty();
            const auto* s=active();setStatus(status,L"Loaded "+p.filename().wstring()+L": "+commaNumber(s?s->points.size():0)+
                      L" measured points; "+commaNumber(s?s->unmeasured:0)+L" unmeasured (−9999) records skipped. "+
                      (simulated?L"Simulation updated.":L"Complete the layer table to simulate."));
        }catch(const std::exception& e){message(window,L"Could not open the measurement.\n\n"+errorText(e));}
    }
    void openProject(const fs::path& selected={}) {
        try {
            if(!maybeSave())return;
            fs::path p=selected.empty()?dialog(window,false,L"Open XRR project",L"XRR Studio Project (*.xrrproj)\0*.xrrproj\0All files\0*.*\0\0"):selected;
            if(p.empty())return;
            finishEdit(false);auto loaded=xrr::loadProject(p);project=std::move(loaded);
            projectPath=p;measurementPath.clear();dirty=false;Button_SetCheck(nominal,BST_UNCHECKED);
            refreshTable(0);recalculateSimulation();refreshScans();updateTitle();
            setStatus(status,L"Project loaded: "+p.filename().wstring()+(simulated?L" · simulation ready":L" · simulation pending"));
        }catch(const std::exception& e){message(window,L"Could not open the project.\n\n"+errorText(e));}
    }
    void openAny(const fs::path& p) {
        auto ext=p.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
        if(ext==L".xrrproj")openProject(p);else openData(p);
    }
    fs::path plotCsvPath(const fs::path& imagePath)const {
        auto csv=imagePath;csv.replace_extension(L".csv");return csv;
    }
    bool allowCsvReplacement(const fs::path& csv)const {
        if(!fs::exists(csv))return true;
        const auto text=L"The matching plot-data file already exists:\n\n"+csv.wstring()+
                        L"\n\nReplace it together with the exported image?";
        return MessageBoxW(window,text.c_str(),L"Replace CSV?",MB_YESNO|MB_ICONWARNING)==IDYES;
    }
    void saveWindowImageWithoutWatermark(const fs::path& path) {
        const bool restore=watermark&&IsWindowVisible(watermark);
        if(restore)ShowWindow(watermark,SW_HIDE);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW|RDW_ALLCHILDREN);
        try{xrr::saveWindowPng(window,path);}
        catch(...){if(restore)ShowWindow(watermark,SW_SHOWNA);RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);throw;}
        if(restore)ShowWindow(watermark,SW_SHOWNA);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void exportFile(int id) {
        if(!finishEdit(true))return;
        try {
            if(id==ID_EXPORT_PNG||id==ID_EXPORT_SVG||id==ID_EXPORT_DATA) {
                if(!active()){message(window,L"Open a measurement before exporting data or a plot.",L"Nothing to export",MB_OK|MB_ICONINFORMATION);return;}
            }
            if(id==ID_EXPORT_PNG) {
                auto p=dialog(window,true,L"Export plot as PNG",L"PNG image (*.png)\0*.png\0\0",L"png",L"XRR_plot.png");if(p.empty())return;
                const auto csv=plotCsvPath(p);if(!allowCsvReplacement(csv))return;
                xrr::savePlotPng(p,plotState());xrr::exportDataCsv(csv,*active(),simulated?&*simulated:nullptr);
                setStatus(status,L"Plot and three-column CSV exported: "+p.wstring()+L" · "+csv.filename().wstring());
            }else if(id==ID_EXPORT_SVG) {
                auto p=dialog(window,true,L"Export plot as SVG",L"SVG image (*.svg)\0*.svg\0\0",L"svg",L"XRR_plot.svg");if(p.empty())return;
                const auto csv=plotCsvPath(p);if(!allowCsvReplacement(csv))return;
                xrr::savePlotSvg(p,plotState());xrr::exportDataCsv(csv,*active(),simulated?&*simulated:nullptr);
                setStatus(status,L"Plot and three-column CSV exported: "+p.wstring()+L" · "+csv.filename().wstring());
            }else if(id==ID_EXPORT_DATA) {
                auto p=dialog(window,true,L"Export plot data",L"CSV file (*.csv)\0*.csv\0\0",L"csv",L"XRR_plot_data.csv");if(p.empty())return;
                xrr::exportDataCsv(p,*active(),simulated?&*simulated:nullptr);
                setStatus(status,L"Three-column plot data exported: "+p.wstring()+(simulated?L" · fitted curve included":L" · fitted column is blank"));
            }else if(id==ID_EXPORT_LAYERS) {
                auto p=dialog(window,true,L"Export model layer table",L"CSV file (*.csv)\0*.csv\0\0",L"csv",L"XRR_layer_stack.csv");if(p.empty())return;
                xrr::exportLayersCsv(p,project.layers);setStatus(status,L"Layer table exported: "+p.wstring());
            }else if(id==ID_EXPORT_WINDOW) {
                auto p=dialog(window,true,L"Export whole window",L"PNG image (*.png)\0*.png\0\0",L"png",L"XRR_studio_window.png");if(p.empty())return;
                saveWindowImageWithoutWatermark(p);
                setStatus(status,L"Window exported: "+p.wstring());
            }
        }catch(const std::exception& e){message(window,L"Export failed.\n\n"+errorText(e));}
    }
    void layerCommand(int id) {
        if(!finishEdit(true))return;
        int row=selectedRow(table);if(row<0)row=static_cast<int>(project.layers.rows.size()-1);
        try {
            std::optional<xrr::MaterialInsertChoice> insertion;
            if(id==ID_INSERT_ABOVE||id==ID_INSERT_BELOW) {
                insertion=xrr::chooseMaterialForInsertion(window,instance,panelFont);
                if(!insertion)return;
            }
            if(id==ID_INSERT_ABOVE)row=static_cast<int>(project.layers.insertBefore(static_cast<std::size_t>(row)));
            else if(id==ID_INSERT_BELOW)row=static_cast<int>(project.layers.insertAfter(static_cast<std::size_t>(row)));
            else if(id==ID_DELETE_LAYER) {
                if(!project.layers.erase(static_cast<std::size_t>(row))){message(window,L"Substrate is fixed as the final row and cannot be deleted.",L"Layer stack",MB_OK|MB_ICONINFORMATION);return;}
                row=std::min(row,static_cast<int>(project.layers.rows.size()-1));
            }else if(id==ID_MOVE_UP) {
                if(project.layers.move(static_cast<std::size_t>(row),-1))--row;else return;
            }else if(id==ID_MOVE_DOWN) {
                if(project.layers.move(static_cast<std::size_t>(row),1))++row;else return;
            }
            if(insertion) {
                auto& layer=project.layers.rows[static_cast<std::size_t>(row)];
                layer.material=insertion->material;layer.density=insertion->density;
            }
            project.poisson=xrr::PoissonModel{};markDirty();refreshTable(row);recalculateSimulation();
            setStatus(status,simulated?L"Layer stack updated; XRR simulation recalculated.":
                      L"Layer stack updated. Simulation pending: "+wide(simulationIssue));
            if(insertion&&row>=0)startEdit(row,insertion->fromPreset?3:1);
        }catch(const std::exception& e){message(window,errorText(e));}
    }
    void fit() {
        if(!finishEdit(true))return;
        if(!active()){message(window,L"Open a measurement before fitting.",L"Nothing to fit",MB_OK|MB_ICONINFORMATION);return;}
        if(!simulated){message(window,L"Complete every material, density, film thickness, and roughness entry before fitting.\n\n"+wide(simulationIssue),L"Model is incomplete",MB_OK|MB_ICONINFORMATION);return;}
        try {
            auto selection=xrr::chooseFitVariables(window,instance,uiFont,*active(),project.layers);if(!selection)return;
            auto fitStack=project.layers;
            for(auto& layer:fitStack.rows)layer.fitDensity=layer.fitThickness=layer.fitRoughness=false;
            for(const auto& variable:selection->variables)if(variable.selected) {
                auto& layer=fitStack.rows[variable.row];
                if(variable.parameter==xrr::FitParameter::Density)layer.fitDensity=true;
                else if(variable.parameter==xrr::FitParameter::Thickness)layer.fitThickness=true;
                else layer.fitRoughness=true;
            }
            setStatus(status,L"Genetic Algorithm fitting in progress...");
            auto result=xrr::runGeneticFitDialog(window,instance,uiFont,*active(),fitStack,*selection,project.poisson);
            if(result.cancelled){setStatus(status,L"Fitting cancelled; the layer table was not changed.");return;}
            if(!xrr::confirmFitResult(window,project.layers,result)){setStatus(status,L"Fitting result discarded; the layer table was not changed.");return;}
            project.layers=std::move(result.fittedLayers);project.poisson=result.poisson;
            Button_SetCheck(showSimulation,BST_CHECKED);CheckMenuItem(GetMenu(window),ID_SHOW_SIMULATION,MF_BYCOMMAND|MF_CHECKED);
            refreshTable(0);recalculateSimulation();markDirty();
            setStatus(status,L"Fitted values applied to the layer table; Poisson diffuse/detector model is shown.");
        }catch(const std::exception& e){message(window,L"Fitting failed.\n\n"+errorText(e),L"XRR fitting");setStatus(status,L"Fitting failed; the layer table was not changed.");}
    }
    void fft() {
        if(!active()){message(window,L"Open a measurement before running FFT thickness analysis.",L"Nothing to transform",MB_OK|MB_ICONINFORMATION);return;}
        try {
            if(xrr::showThicknessFftDialog(window,instance,uiFont,*active()))setStatus(status,L"FFT thickness spectrum calculated from the selected measured 2θ range.");
            else setStatus(status,L"FFT thickness analysis cancelled.");
        }catch(const std::exception& e){message(window,L"FFT thickness analysis failed.\n\n"+errorText(e),L"FFT thickness analysis");setStatus(status,L"FFT thickness analysis failed.");}
    }
    void command(int id,int notification=0,HWND source=nullptr) {
        if(id==ID_SCAN_COMBO&&notification==CBN_SELCHANGE) {
            int i=ComboBox_GetCurSel(scanCombo);if(i>=0){project.activeScan=static_cast<std::size_t>(i);project.poisson=xrr::PoissonModel{};resetPlot();recalculateSimulation();markDirty();}return;
        }
        switch(id) {
        case ID_NEW:newProject();break;case ID_OPEN_DATA:openData();break;case ID_OPEN_PROJECT:openProject();break;
        case ID_SAVE_PROJECT:save(false);break;case ID_SAVE_AS:save(true);break;
        case ID_EXPORT_PNG:case ID_EXPORT_SVG:case ID_EXPORT_DATA:case ID_EXPORT_LAYERS:case ID_EXPORT_WINDOW:exportFile(id);break;
        case ID_RESET_PLOT:resetPlot();setStatus(status,L"Plot view reset.");break;
        case ID_FFT:fft();break;
        case ID_NOMINAL:{
            bool checked=Button_GetCheck(nominal)==BST_CHECKED;
            if(source!=nominal){checked=!checked;Button_SetCheck(nominal,checked?BST_CHECKED:BST_UNCHECKED);}
            CheckMenuItem(GetMenu(window),ID_NOMINAL,MF_BYCOMMAND|(checked?MF_CHECKED:MF_UNCHECKED));
            resetPlot();break;}
        case ID_SHOW_SIMULATION:{
            bool checked=Button_GetCheck(showSimulation)==BST_CHECKED;
            if(source!=showSimulation){checked=!checked;Button_SetCheck(showSimulation,checked?BST_CHECKED:BST_UNCHECKED);}
            CheckMenuItem(GetMenu(window),ID_SHOW_SIMULATION,MF_BYCOMMAND|(checked?MF_CHECKED:MF_UNCHECKED));
            InvalidateRect(plot,nullptr,FALSE);setStatus(status,checked?L"Layer-stack simulation shown.":L"Layer-stack simulation hidden.");break;}
        case ID_INSERT_ABOVE:case ID_INSERT_BELOW:case ID_DELETE_LAYER:case ID_MOVE_UP:case ID_MOVE_DOWN:layerCommand(id);break;
        case ID_FIT:fit();break;
        case ID_ABOUT:message(window,L"XRR Layer Studio v2.5.1\n\nC++17 native Windows application\nBruker RAW4.00 and sectioned TXT reader\n2θ vs. logarithmic Intensity\nBox-selection zoom and two-point thickness tool\nPNG/SVG export with an automatic three-column CSV sidecar\nPer-Monitor V2 High-DPI interface\nFFT thickness-spectrum analysis\nSearchable material preset catalog\n\nLayer-stack XRR simulation uses iterative Parratt recursion, Nevot–Croce roughness, Bruker Kα doublet metadata and fitted instrumental broadening. Genetic fitting supports robust high-angle-balanced or pure Poisson objectives.\n\n@KHSO5 All rights reserved.",L"About",MB_OK|MB_ICONINFORMATION);break;
        case ID_EXIT:SendMessageW(window,WM_CLOSE,0,0);break;
        }
    }
    bool close(){return maybeSave();}
    void dropped(HDROP drop) {
        wchar_t p[32768]{};if(DragQueryFileW(drop,0,p,32768))openAny(p);DragFinish(drop);
    }
    void smoke(const fs::path& png,const fs::path& saved) {
        // Exercises the same model and refresh path used by UI commands; retained only as an automated QA hook.
        project.layers.rows={
            {false,"Ru","12.379","8.423","0.230"},
            {false,"Co80Tb20","8.397","50.428","0.491"},
            {true,"SiO2","2.650","","0.481"}
        };
        refreshTable(1);recalculateSimulation();markDirty();projectPath=saved;xrr::saveProject(saved,project);dirty=false;updateTitle();
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
        saveWindowImageWithoutWatermark(png);
    }
};
LRESULT CALLBACK plotProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    auto* c=reinterpret_cast<PlotController*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){c=reinterpret_cast<PlotController*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c));}
    if(!c||!c->app)return DefWindowProcW(h,msg,wp,lp);
    auto& a=*c->app;
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);a.paintPlotWindow(dc,r.right,r.bottom);EndPaint(h,&ps);return 0;}
    case WM_MOUSEMOVE:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(c->dragging)a.updateZoomBox(p);else a.hoverAt(p);
        TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);return 0;}
    case WM_MOUSELEAVE:a.currentHover=-1;InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN:{SetFocus(h);POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if((wp&MK_CONTROL)!=0||GetKeyState(VK_CONTROL)<0){a.selectThicknessPoint(p);return 0;}
        if(a.insidePlot(p)){SetCapture(h);c->dragging=true;c->start=c->current=p;a.currentHover=-1;
            setStatus(a.status,L"Drag to define a rectangular zoom region; release to apply.");InvalidateRect(h,nullptr,FALSE);}return 0;}
    case WM_LBUTTONUP:if(c->dragging){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};c->current=p;c->dragging=false;ReleaseCapture();a.finishZoomBox(p);}return 0;
    case WM_CAPTURECHANGED:if(c->dragging){c->dragging=false;InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_LBUTTONDBLCLK:a.resetPlot();return 0;
    case WM_MOUSEWHEEL:return 0;
    case WM_SETCURSOR:SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
LRESULT CALLBACK mainProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    auto* a=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){a=reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);a->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
    if(!a)return DefWindowProcW(h,msg,wp,lp);
    switch(msg) {
    case WM_CREATE:a->createMenu();a->createControls();return 0;
    case WM_SIZE:a->layout();return 0;
    case WM_GETMINMAXINFO:{
        auto* m=reinterpret_cast<MINMAXINFO*>(lp);const double s=dpiScale(h);
        LONG minimumWidth=static_cast<LONG>(1100*s),minimumHeight=static_cast<LONG>(680*s);
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)) {
            minimumWidth=std::min(minimumWidth,(monitor.rcWork.right-monitor.rcWork.left)*98/100);
            minimumHeight=std::min(minimumHeight,(monitor.rcWork.bottom-monitor.rcWork.top)*98/100);
        }
        m->ptMinTrackSize.x=minimumWidth;m->ptMinTrackSize.y=minimumHeight;return 0;}
    case WM_DPICHANGED:{
        const UINT dpi=LOWORD(wp);const RECT* r=reinterpret_cast<RECT*>(lp);
        a->rebuildFonts(dpi);
        SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        a->layout();RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==ID_CELL_EDIT&&HIWORD(wp)==EN_KILLFOCUS){a->finishEdit(true);return 0;}
        a->command(LOWORD(wp),HIWORD(wp),reinterpret_cast<HWND>(lp));return 0;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->idFrom==ID_TABLE) {
        auto* n=reinterpret_cast<NMITEMACTIVATE*>(lp);
        if(n->hdr.code==LVN_ITEMCHANGED){a->updateLayerButtons();return 0;}
        if(n->hdr.code==NM_DBLCLK){a->startEdit(n->iItem,n->iSubItem);return 0;}
        if(n->hdr.code==NM_RETURN){const int r=selectedRow(a->table);a->startEdit(r,1);return 0;}
    }break;
    case WM_DROPFILES:a->dropped(reinterpret_cast<HDROP>(wp));return 0;
    case WM_CTLCOLORSTATIC:{
        SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)==a->watermark?RGB(104,119,129):RGB(39,70,90));
        return reinterpret_cast<LRESULT>(a->background);}
    case WM_ERASEBKGND:{RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,a->background);return 1;}
    case WM_CLOSE:if(a->close())DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
void dpiAware() {
    using Fn=BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const auto f=user32Function<Fn>("SetProcessDpiAwarenessContext");
    if(f)f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);else SetProcessDPIAware();
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    try {
        dpiAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        Gdiplus::GdiplusStartupInput gdip;ULONG_PTR token=0;
        if(Gdiplus::GdiplusStartup(&token,&gdip,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Could not initialize graphics");
        WNDCLASSEXW plotW{};plotW.cbSize=sizeof(plotW);plotW.style=CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;plotW.lpfnWndProc=plotProc;plotW.hInstance=instance;
        plotW.hCursor=LoadCursorW(nullptr,IDC_CROSS);plotW.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);plotW.lpszClassName=plotClass;
        if(!RegisterClassExW(&plotW))throw std::runtime_error("Could not register plot control");
        WNDCLASSEXW mainW{};mainW.cbSize=sizeof(mainW);mainW.style=CS_HREDRAW|CS_VREDRAW;mainW.lpfnWndProc=mainProc;mainW.hInstance=instance;
        mainW.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));if(!mainW.hIcon)mainW.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        mainW.hCursor=LoadCursorW(nullptr,IDC_ARROW);mainW.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);mainW.lpszClassName=mainClass;mainW.hIconSm=mainW.hIcon;
        if(!RegisterClassExW(&mainW))throw std::runtime_error("Could not register the application window");
        auto app=std::make_unique<App>();app->instance=instance;
        int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        bool smoke=false;
        for(int i=1;i<argc;++i) {
            const std::wstring arg=argv[i];
            if(arg==L"--ui-smoke"&&i+2<argc){smoke=true;app->smokeScreenshot=argv[++i];app->smokeProject=argv[++i];}
            else if(!arg.empty()&&arg[0]!=L'-')app->startupInput=arg;
        }
        LocalFree(argv);
        const UINT initialDpi=windowDpi(nullptr);
        RECT wanted{0,0,MulDiv(1500,static_cast<int>(initialDpi),96),MulDiv(900,static_cast<int>(initialDpi),96)};
        adjustWindowRectForDpi(wanted,WS_OVERLAPPEDWINDOW,TRUE,0,initialDpi);
        int width=wanted.right-wanted.left,height=wanted.bottom-wanted.top;RECT work{};
        if(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)) {
            const int workWidth=static_cast<int>(work.right-work.left),workHeight=static_cast<int>(work.bottom-work.top);
            width=std::min(width,workWidth*98/100);height=std::min(height,workHeight*98/100);
        }
        HWND window=CreateWindowExW(0,mainClass,appTitle,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
                                    width,height,nullptr,nullptr,instance,app.get());
        if(!window)throw std::runtime_error("Could not create the application window");
        ShowWindow(window,show);UpdateWindow(window);
        if(!app->startupInput.empty())app->openAny(app->startupInput);
        if(smoke) {
            app->smoke(app->smokeScreenshot,app->smokeProject);DestroyWindow(window);
            Gdiplus::GdiplusShutdown(token);return 0;
        }
        ACCEL keys[]={{FVIRTKEY|FCONTROL,'N',ID_NEW},{FVIRTKEY|FCONTROL,'O',ID_OPEN_DATA},
                      {FVIRTKEY|FCONTROL|FSHIFT,'O',ID_OPEN_PROJECT},{FVIRTKEY|FCONTROL,'S',ID_SAVE_PROJECT},
                      {FVIRTKEY|FCONTROL,'T',ID_FFT},{FVIRTKEY|FCONTROL,'F',ID_FIT},{FVIRTKEY,'R',ID_RESET_PLOT}};
        HACCEL accel=CreateAcceleratorTableW(keys,static_cast<int>(std::size(keys)));
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0)if(!TranslateAcceleratorW(window,accel,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        DestroyAcceleratorTable(accel);Gdiplus::GdiplusShutdown(token);return static_cast<int>(msg.wParam);
    }catch(const std::exception& e) {
        message(nullptr,L"XRR Layer Studio could not start.\n\n"+errorText(e));return 1;
    }
}
