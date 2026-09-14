#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "material_picker_win.hpp"
#include "material_presets.hpp"
#include "plot_win.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrr {
namespace {

constexpr wchar_t pickerClass[]=L"XrrMaterialPicker_v251";
enum PickerId { PickerSearch=800,PickerCategory,PickerClear,PickerTable,PickerSummary,PickerInfo };

template<class Function>
Function user32Function(const char* name) {
    const auto raw=GetProcAddress(GetModuleHandleW(L"user32.dll"),name);Function function=nullptr;
    static_assert(sizeof(function)==sizeof(raw));std::memcpy(&function,&raw,sizeof(function));return function;
}

UINT dpiForWindow(HWND window) {
    using GetDpiForWindowFn=UINT(WINAPI*)(HWND);
    const auto function=user32Function<GetDpiForWindowFn>("GetDpiForWindow");
    if(function&&window)return function(window);
    HDC dc=GetDC(window);const UINT dpi=dc?static_cast<UINT>(GetDeviceCaps(dc,LOGPIXELSX)):96;
    if(dc)ReleaseDC(window,dc);
    return dpi?dpi:96;
}

HFONT resizedFont(HFONT source,UINT oldDpi,UINT newDpi) {
    if(!source||!oldDpi||!newDpi)return nullptr;
    LOGFONTW description{};
    if(GetObjectW(source,sizeof(description),&description)!=sizeof(description))return nullptr;
    description.lfHeight=MulDiv(description.lfHeight,static_cast<int>(newDpi),static_cast<int>(oldDpi));
    return CreateFontIndirectW(&description);
}

void setFont(HWND control,HFONT font){if(control&&font)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}

std::wstring textOf(HWND control) {
    const int length=GetWindowTextLengthW(control);std::wstring result(static_cast<std::size_t>(length)+1,L'\0');
    if(length)GetWindowTextW(control,result.data(),length+1);
    result.resize(static_cast<std::size_t>(length));return result;
}

void setCell(HWND table,int row,int column,const std::wstring& text) {
    if(column==0) {
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=const_cast<wchar_t*>(text.c_str());ListView_InsertItem(table,&item);
    }else ListView_SetItemText(table,row,column,const_cast<wchar_t*>(text.c_str()));
}

void selectRow(HWND table,int row) {
    if(row<0)return;
    ListView_SetItemState(table,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_EnsureVisible(table,row,FALSE);
}

SIZE windowSize(HWND owner,int logicalWidth,int logicalHeight) {
    const UINT dpi=dpiForWindow(owner);SIZE size{MulDiv(logicalWidth,static_cast<int>(dpi),96),MulDiv(logicalHeight,static_cast<int>(dpi),96)};
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)) {
        const int width=static_cast<int>(monitor.rcWork.right-monitor.rcWork.left),height=static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top);
        size.cx=std::min(size.cx,static_cast<LONG>(width*94/100));size.cy=std::min(size.cy,static_cast<LONG>(height*94/100));
    }
    return size;
}

void center(HWND window,HWND owner) {
    RECT target{},parent{};GetWindowRect(window,&target);GetWindowRect(owner,&parent);
    const int width=target.right-target.left,height=target.bottom-target.top;
    int x=parent.left+(parent.right-parent.left-width)/2,y=parent.top+(parent.bottom-parent.top-height)/2;
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)) {
        x=std::clamp(x,static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width));
        y=std::clamp(y,static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height));
    }
    SetWindowPos(window,HWND_TOP,x,y,width,height,SWP_SHOWWINDOW);
}

bool modalLoop(HWND dialog,HWND owner,const std::function<bool()>& complete) {
    EnableWindow(owner,FALSE);bool quit=false;MSG message{};
    while(!complete()) {
        const int code=GetMessageW(&message,nullptr,0,0);if(code<=0){quit=code==0;break;}
        if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(quit)PostQuitMessage(static_cast<int>(message.wParam));return !quit;
}

struct PickerDialog {
    HINSTANCE instance{};HWND owner{},window{},intro{},searchLabel{},search{},categoryLabel{},category{},clear{},table{},summary{},info{},insert{},cancel{};
    HFONT font{},ownedFont{};UINT dpi=96;std::vector<const MaterialPreset*> visible;
    std::optional<MaterialInsertChoice> result;bool finished=false,rebuilding=false;

    ~PickerDialog(){if(ownedFont)DeleteObject(ownedFont);}
    int px(int logical)const{return MulDiv(logical,static_cast<int>(dpi),96);}

    void applyFont() {
        for(HWND control:{intro,searchLabel,search,categoryLabel,category,clear,table,summary,info,insert,cancel})setFont(control,font);
        if(table)setFont(ListView_GetHeader(table),font);
    }

    void updateDpi(UINT nextDpi) {
        if(!nextDpi||nextDpi==dpi)return;
        HFONT next=resizedFont(font,dpi,nextDpi),previous=ownedFont;
        if(next){font=next;ownedFont=next;}dpi=nextDpi;
        if(next){applyFont();if(previous)DeleteObject(previous);}
    }

    void create() {
        const auto control=[&](const wchar_t* type,const wchar_t* text,DWORD style,int id) {
            HWND created=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);setFont(created,font);return created;
        };
        intro=control(L"STATIC",L"Choose a preset or keep the default Custom / empty layer. A preset fills only Material and nominal density; thickness and roughness remain blank.",SS_LEFT,0);
        searchLabel=control(L"STATIC",L"Search element or material:",SS_LEFT|SS_CENTERIMAGE,0);
        search=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,PickerSearch);
        SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"e.g. Co, cobalt, SiO2, Permalloy"));
        categoryLabel=control(L"STATIC",L"Category:",SS_LEFT|SS_CENTERIMAGE,0);
        category=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,PickerCategory);
        SendMessageW(category,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All categories"));
        for(const auto value:{MaterialCategory::Element,MaterialCategory::Oxide,MaterialCategory::NitrideCarbide,
                              MaterialCategory::SemiconductorCompound,MaterialCategory::AlloyMagnetic}) {
            const auto label=wide(materialCategoryLabel(value));SendMessageW(category,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        ComboBox_SetCurSel(category,0);
        clear=control(L"BUTTON",L"Clear filters",BS_PUSHBUTTON|WS_TABSTOP,PickerClear);
        table=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(PickerTable)),instance,nullptr);setFont(table,font);
        ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(table,L"Explorer",nullptr);
        const wchar_t* headers[]={L"Material",L"Common name",L"Density (g/cm³)",L"Category",L"Reference note"};
        for(int column=0;column<5;++column){LVCOLUMNW item{};item.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;item.pszText=const_cast<wchar_t*>(headers[column]);item.cx=120;item.fmt=column==2?LVCFMT_RIGHT:LVCFMT_LEFT;ListView_InsertColumn(table,column,&item);}
        summary=control(L"STATIC",L"",SS_LEFT|SS_CENTERIMAGE,PickerSummary);
        info=control(L"STATIC",L"",SS_LEFT,PickerInfo);
        insert=control(L"BUTTON",L"Insert layer",BS_DEFPUSHBUTTON|WS_TABSTOP,IDOK);
        cancel=control(L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP,IDCANCEL);
        applyFont();rebuild();layout();SetFocus(search);
    }

    std::optional<MaterialCategory> selectedCategory()const {
        const int selected=ComboBox_GetCurSel(category);
        if(selected<=0)return std::nullopt;
        return static_cast<MaterialCategory>(selected-1);
    }

    void updateInfo() {
        const int row=ListView_GetNextItem(table,-1,LVNI_SELECTED);
        std::wstring text;
        if(row<=0)text=L"Custom layer: Material and density stay blank for manual entry.";
        else if(static_cast<std::size_t>(row)<=visible.size()) {
            const auto& preset=*visible[static_cast<std::size_t>(row-1)];
            text=L"Will insert “"+wide(preset.material)+L"” with nominal density "+wide(formatPresetDensity(preset.densityGcm3))+
                 L" g/cm³. This is an editable bulk/reference value; deposited-film density may differ.";
        }
        SetWindowTextW(info,text.c_str());EnableWindow(insert,row>=0);
    }

    void rebuild() {
        rebuilding=true;ListView_DeleteAllItems(table);
        visible=filterMaterialPresets(utf8(textOf(search)),selectedCategory());
        setCell(table,0,0,L"Custom / empty layer");setCell(table,0,1,L"Manual entry");setCell(table,0,2,L"—");
        setCell(table,0,3,L"Custom");setCell(table,0,4,L"Material and density remain blank");
        for(std::size_t index=0;index<visible.size();++index) {
            const int row=static_cast<int>(index+1);const auto& preset=*visible[index];
            setCell(table,row,0,wide(preset.material));setCell(table,row,1,wide(preset.name));
            setCell(table,row,2,wide(formatPresetDensity(preset.densityGcm3)));
            setCell(table,row,3,wide(materialCategoryLabel(preset.category)));setCell(table,row,4,wide(preset.note));
        }
        const auto count=std::to_wstring(visible.size());
        SetWindowTextW(summary,(count+L" preset"+(visible.size()==1?L"":L"s")+L" shown · Custom / empty layer is always available").c_str());
        selectRow(table,0);rebuilding=false;updateInfo();
    }

    void layout() {
        RECT rectangle{};GetClientRect(window,&rectangle);const int width=rectangle.right,height=rectangle.bottom,m=px(18);
        MoveWindow(intro,m,px(14),width-2*m,px(42),TRUE);
        const int filterY=px(62),labelWidth=px(155),searchWidth=std::max(px(160),width*30/100);
        MoveWindow(searchLabel,m,filterY,labelWidth,px(32),TRUE);MoveWindow(search,m+labelWidth,filterY,searchWidth,px(32),TRUE);
        const int clearWidth=px(110),clearX=width-m-clearWidth;
        const int categoryX=m+labelWidth+searchWidth+px(12),categoryLabelWidth=px(68),categoryComboX=categoryX+categoryLabelWidth;
        MoveWindow(categoryLabel,categoryX,filterY,categoryLabelWidth,px(32),TRUE);
        MoveWindow(category,categoryComboX,filterY,std::max(px(130),clearX-px(8)-categoryComboX),px(260),TRUE);
        MoveWindow(clear,clearX,filterY,clearWidth,px(32),TRUE);
        const int tableY=px(105),footer=px(112);MoveWindow(table,m,tableY,width-2*m,std::max(px(190),height-tableY-footer),TRUE);
        const int below=std::max(tableY+px(190),height-footer+px(8));MoveWindow(summary,m,below,width-2*m,px(26),TRUE);
        MoveWindow(info,m,below+px(27),std::max(px(200),width-2*m-px(226)),px(50),TRUE);
        MoveWindow(cancel,width-m-px(96),height-px(48),px(96),px(32),TRUE);
        MoveWindow(insert,width-m-px(216),height-px(48),px(110),px(32),TRUE);
        RECT tableRectangle{};GetClientRect(table,&tableRectangle);const int tableWidth=std::max(px(700),static_cast<int>(tableRectangle.right)-GetSystemMetrics(SM_CXVSCROLL)-2);
        const int columns[]={tableWidth*20/100,tableWidth*20/100,tableWidth*15/100,tableWidth*20/100,0};int used=0;
        for(int i=0;i<4;++i){ListView_SetColumnWidth(table,i,columns[i]);used+=columns[i];}ListView_SetColumnWidth(table,4,std::max(px(120),tableWidth-used));
    }

    void accept() {
        const int row=ListView_GetNextItem(table,-1,LVNI_SELECTED);if(row<0)return;
        MaterialInsertChoice choice;
        if(row>0&&static_cast<std::size_t>(row)<=visible.size()) {
            const auto& preset=*visible[static_cast<std::size_t>(row-1)];choice.material=preset.material;
            choice.density=formatPresetDensity(preset.densityGcm3);choice.fromPreset=true;
        }
        result=std::move(choice);DestroyWindow(window);
    }

    void close(){result.reset();DestroyWindow(window);}
};

LRESULT CALLBACK pickerProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* dialog=reinterpret_cast<PickerDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){dialog=reinterpret_cast<PickerDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);dialog->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
    if(!dialog)return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:dialog->create();return 0;
    case WM_SIZE:dialog->layout();return 0;
    case WM_GETMINMAXINFO:{auto* information=reinterpret_cast<MINMAXINFO*>(lp);information->ptMinTrackSize={dialog->px(760),dialog->px(500)};return 0;}
    case WM_DPICHANGED:{const RECT* rectangle=reinterpret_cast<RECT*>(lp);dialog->updateDpi(LOWORD(wp));
        SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOZORDER|SWP_NOACTIVATE);dialog->layout();return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==PickerSearch&&HIWORD(wp)==EN_CHANGE){dialog->rebuild();return 0;}
        if(LOWORD(wp)==PickerCategory&&HIWORD(wp)==CBN_SELCHANGE){dialog->rebuild();return 0;}
        if(LOWORD(wp)==PickerClear){SetWindowTextW(dialog->search,L"");ComboBox_SetCurSel(dialog->category,0);dialog->rebuild();SetFocus(dialog->search);return 0;}
        if(LOWORD(wp)==IDOK){dialog->accept();return 0;}if(LOWORD(wp)==IDCANCEL){dialog->close();return 0;}break;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->idFrom==PickerTable) {
        const auto code=reinterpret_cast<NMHDR*>(lp)->code;
        if(code==LVN_ITEMCHANGED&&!dialog->rebuilding){dialog->updateInfo();return 0;}
        if(code==NM_DBLCLK||code==NM_RETURN){dialog->accept();return 0;}
    }break;
    case WM_CLOSE:dialog->close();return 0;
    case WM_DESTROY:dialog->finished=true;return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

}

std::optional<MaterialInsertChoice> chooseMaterialForInsertion(HWND owner,HINSTANCE instance,HFONT font) {
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=pickerProcedure;type.hInstance=instance;
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);type.lpszClassName=pickerClass;
    if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("could not register material picker window");
    PickerDialog dialog;dialog.owner=owner;dialog.instance=instance;dialog.dpi=dpiForWindow(owner);
    dialog.ownedFont=resizedFont(font,dialog.dpi,dialog.dpi);dialog.font=dialog.ownedFont?dialog.ownedFont:font;
    const SIZE size=windowSize(owner,1080,690);
    HWND picker=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,pickerClass,L"Insert layer · material presets",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,0,0,size.cx,size.cy,owner,nullptr,instance,&dialog);
    if(!picker)throw std::runtime_error("could not create material picker window");
    center(picker,owner);modalLoop(picker,owner,[&]{return dialog.finished;});return dialog.result;
}

}
