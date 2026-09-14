#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "fit_dialog_win.hpp"
#include "plot_win.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace xrr {
namespace {
constexpr wchar_t setupClass[]=L"XrrFitSetup_v251";
constexpr wchar_t progressClass[]=L"XrrFitProgress_v251";
constexpr UINT fitProgressMessage=WM_APP+20,fitDoneMessage=WM_APP+21;
enum SetupId {
    SetupTable=500,SetupStart,SetupCancel,SetupShownAll,SetupShownNone,SetupEdit,
    SetupParameterFilter,SetupLayerFilter,SetupRangeMinimum,SetupRangeMaximum,SetupObjective
};
enum ProgressId { ProgressStatus=600,ProgressBar,ProgressCancel };

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

std::wstring displayNumber(double value,int precision=8) {
    std::wostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(precision)<<value;return out.str();
}

std::wstring minimumDecimals(std::wstring text,std::size_t count) {
    const auto exponent=text.find_first_of(L"eE");const auto end=exponent==std::wstring::npos?text.size():exponent;
    const auto decimal=text.find(L'.');const auto existing=decimal==std::wstring::npos||decimal>end?0:end-decimal-1;
    if(decimal==std::wstring::npos||decimal>end)text.insert(end,L"."+std::wstring(count,L'0'));
    else if(existing<count)text.insert(end,std::wstring(count-existing,L'0'));
    return text;
}

std::wstring displayParameterNumber(double value,FitParameter parameter) {
    auto text=displayNumber(value,10);
    return parameter==FitParameter::Roughness?minimumDecimals(std::move(text),3):text;
}

bool parseNumber(const std::wstring& text,double& value) {
    std::wistringstream in(text);in.imbue(std::locale::classic());
    if(!(in>>value)||!std::isfinite(value))return false;
    in>>std::ws;return in.eof();
}

void setCell(HWND table,int row,int column,const std::wstring& text) {
    if(column==0) {
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=const_cast<wchar_t*>(text.c_str());ListView_InsertItem(table,&item);
    }else ListView_SetItemText(table,row,column,const_cast<wchar_t*>(text.c_str()));
}

std::wstring rowLabel(const LayerStack& stack,std::size_t row) {
    const auto& layer=stack.rows[row];
    std::wstring label=layer.substrate?L"Substrate":L"Film "+std::to_wstring(row+1);
    if(!layer.material.empty())label+=L" · "+wide(layer.material);
    return label;
}

std::wstring parameterLabel(FitParameter parameter) {
    if(parameter==FitParameter::Density)return L"Density ρ";
    if(parameter==FitParameter::Thickness)return L"Thickness t";
    return L"Roughness σ";
}

std::wstring unit(FitParameter parameter){return parameter==FitParameter::Density?L"g/cm³":L"nm";}

struct SetupDialog {
    HINSTANCE instance{};HWND owner{},window{},table{},help{},parameterCaption{},parameterFilter{},layerCaption{},layerFilter{},summary{},
        rangeCaption{},rangeMinimum{},rangeSeparator{},rangeMaximum{},rangeHint{},objectiveCaption{},objectiveCombo{},objectiveHint{},
        start{},cancel{},shownAll{},shownNone{},edit{};HFONT font{},ownedFont{};UINT dpi=96;
    const Scan* measured{};const LayerStack* stack{};std::vector<FitVariable> variables;std::vector<std::size_t> visibleVariables;
    std::optional<double> twoThetaMinimum,twoThetaMaximum;
    FitObjective objective=FitObjective::RobustPoisson;
    bool accepted=false,finished=false,endingEdit=false,rebuilding=false;
    int editRow=-1,editColumn=-1;

    ~SetupDialog(){if(ownedFont)DeleteObject(ownedFont);}

    int px(int logical)const{return MulDiv(logical,static_cast<int>(dpi),96);}

    void applyFont() {
        for(HWND control:{help,parameterCaption,parameterFilter,layerCaption,layerFilter,summary,rangeCaption,rangeMinimum,
                          rangeSeparator,rangeMaximum,rangeHint,objectiveCaption,objectiveCombo,objectiveHint,table,
                          start,cancel,shownAll,shownNone,edit})
            if(control)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        if(table)SendMessageW(ListView_GetHeader(table),WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }

    void updateDpi(UINT nextDpi) {
        if(!nextDpi||nextDpi==dpi)return;
        HFONT next=resizedFont(font,dpi,nextDpi);
        HFONT previousOwned=ownedFont;
        if(next){font=next;ownedFont=next;}
        dpi=nextDpi;
        if(next){applyFont();if(previousOwned)DeleteObject(previousOwned);}
    }

    void create() {
        auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id)->HWND{
            auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
            SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;
        };
        help=control(L"STATIC",L"Filter the list, then choose this round's variables. Double-click Lower/Upper to edit; defaults are ±20% (a zero value uses 0–0.2 nm).",SS_LEFT,0);
        parameterCaption=control(L"STATIC",L"Parameter:",SS_LEFT|SS_CENTERIMAGE,0);
        parameterFilter=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,SetupParameterFilter);
        for(const auto* text:{L"All parameters",L"Density only",L"Thickness only",L"Roughness only"})
            SendMessageW(parameterFilter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        ComboBox_SetCurSel(parameterFilter,0);
        layerCaption=control(L"STATIC",L"Layers:",SS_LEFT|SS_CENTERIMAGE,0);
        layerFilter=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,SetupLayerFilter);
        for(const auto* text:{L"All layers",L"Films only",L"Substrate only"})
            SendMessageW(layerFilter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        ComboBox_SetCurSel(layerFilter,0);
        summary=control(L"STATIC",L"",SS_RIGHT|SS_CENTERIMAGE,0);
        rangeCaption=control(L"STATIC",L"Fit 2θ range:",SS_LEFT|SS_CENTERIMAGE,0);
        rangeMinimum=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SetupRangeMinimum);
        rangeSeparator=control(L"STATIC",L"to",SS_CENTER|SS_CENTERIMAGE,0);
        rangeMaximum=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SetupRangeMaximum);
        double first=0,last=0;
        if(measured&&!measured->points.empty()) {
            const auto bounds=std::minmax_element(measured->points.begin(),measured->points.end(),
                [](const Point& a,const Point& b){return a.twoTheta<b.twoTheta;});
            first=bounds.first->twoTheta;last=bounds.second->twoTheta;
        }
        const auto rangeText=L"degrees; blank = all imported points ("+displayNumber(first)+L"–"+displayNumber(last)+L"°)";
        rangeHint=control(L"STATIC",rangeText.c_str(),SS_LEFT|SS_CENTERIMAGE,0);
        SendMessageW(rangeMinimum,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"minimum"));
        SendMessageW(rangeMaximum,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"maximum"));
        objectiveCaption=control(L"STATIC",L"Objective:",SS_LEFT|SS_CENTERIMAGE,0);
        objectiveCombo=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,SetupObjective);
        SendMessageW(objectiveCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Robust Poisson · high-angle balanced (recommended)"));
        SendMessageW(objectiveCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Pure Poisson deviance · legacy maximum likelihood"));
        ComboBox_SetCurSel(objectiveCombo,0);
        objectiveHint=control(L"STATIC",L"Robust mode limits domination by model mismatch in high-count, low-angle points.",SS_LEFT|SS_CENTERIMAGE,0);
        table=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(SetupTable)),instance,nullptr);
        SendMessageW(table,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER|LVS_EX_CHECKBOXES);
        const wchar_t* headers[]={L"Layer",L"Parameter",L"Current",L"Lower",L"Upper",L"Unit"};
        for(int i=0;i<6;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;c.pszText=const_cast<wchar_t*>(headers[i]);c.cx=100;c.fmt=i<2?LVCFMT_LEFT:LVCFMT_RIGHT;ListView_InsertColumn(table,i,&c);}
        shownAll=control(L"BUTTON",L"Select shown",BS_PUSHBUTTON,SetupShownAll);
        shownNone=control(L"BUTTON",L"Clear shown",BS_PUSHBUTTON,SetupShownNone);
        start=control(L"BUTTON",L"Start Genetic Algorithm",BS_DEFPUSHBUTTON,SetupStart);cancel=control(L"BUTTON",L"Cancel",BS_PUSHBUTTON,SetupCancel);
        applyFont();rebuildTable();
        layout();
    }
    void captureVisibleChecks() {
        if(rebuilding)return;
        for(std::size_t row=0;row<visibleVariables.size();++row)
            variables[visibleVariables[row]].selected=ListView_GetCheckState(table,static_cast<int>(row))!=FALSE;
    }
    bool visibleByFilters(const FitVariable& variable)const {
        const int parameter=ComboBox_GetCurSel(parameterFilter),layers=ComboBox_GetCurSel(layerFilter);
        if(parameter>0&&static_cast<int>(variable.parameter)!=parameter-1)return false;
        const bool substrate=stack->rows[variable.row].substrate;
        if((layers==1&&substrate)||(layers==2&&!substrate))return false;
        return true;
    }
    void updateSummary() {
        captureVisibleChecks();
        const auto selected=static_cast<std::size_t>(std::count_if(variables.begin(),variables.end(),[](const FitVariable& variable){return variable.selected;}));
        const auto text=L"Showing "+std::to_wstring(visibleVariables.size())+L" of "+std::to_wstring(variables.size())+
            L" · "+std::to_wstring(selected)+L" selected overall";
        SetWindowTextW(summary,text.c_str());EnableWindow(shownAll,!visibleVariables.empty());EnableWindow(shownNone,!visibleVariables.empty());
    }
    void rebuildTable() {
        if(edit&&!finishEdit(true))return;
        captureVisibleChecks();rebuilding=true;ListView_DeleteAllItems(table);visibleVariables.clear();
        for(std::size_t index=0;index<variables.size();++index) {
            const auto& variable=variables[index];if(!visibleByFilters(variable))continue;
            const int row=static_cast<int>(visibleVariables.size());visibleVariables.push_back(index);
            setCell(table,row,0,rowLabel(*stack,variable.row));setCell(table,row,1,parameterLabel(variable.parameter));
            setCell(table,row,2,displayParameterNumber(variable.initial,variable.parameter));
            setCell(table,row,3,displayParameterNumber(variable.lower,variable.parameter));
            setCell(table,row,4,displayParameterNumber(variable.upper,variable.parameter));setCell(table,row,5,unit(variable.parameter));
            ListView_SetCheckState(table,row,variable.selected?TRUE:FALSE);
        }
        rebuilding=false;updateSummary();
    }
    void selectShown(bool selected) {
        if(edit&&!finishEdit(true))return;
        rebuilding=true;
        for(std::size_t row=0;row<visibleVariables.size();++row) {
            variables[visibleVariables[row]].selected=selected;
            ListView_SetCheckState(table,static_cast<int>(row),selected?TRUE:FALSE);
        }
        rebuilding=false;updateSummary();
    }
    void layout() {
        RECT r{};GetClientRect(window,&r);const int w=r.right,h=r.bottom,m=px(16);
        MoveWindow(help,m,px(12),w-2*m,px(34),TRUE);
        const int filterY=px(49);MoveWindow(parameterCaption,m,filterY,px(76),px(26),TRUE);MoveWindow(parameterFilter,m+px(78),filterY,px(160),px(220),TRUE);
        MoveWindow(layerCaption,m+px(254),filterY,px(54),px(26),TRUE);MoveWindow(layerFilter,m+px(310),filterY,px(150),px(180),TRUE);
        MoveWindow(summary,m+px(470),filterY,std::max(px(100),w-2*m-px(470)),px(26),TRUE);
        const int rangeY=px(82);MoveWindow(rangeCaption,m,rangeY,px(92),px(26),TRUE);MoveWindow(rangeMinimum,m+px(94),rangeY,px(100),px(26),TRUE);
        MoveWindow(rangeSeparator,m+px(198),rangeY,px(28),px(26),TRUE);MoveWindow(rangeMaximum,m+px(230),rangeY,px(100),px(26),TRUE);
        MoveWindow(rangeHint,m+px(340),rangeY,std::max(px(100),w-2*m-px(340)),px(26),TRUE);
        const int objectiveY=px(115);MoveWindow(objectiveCaption,m,objectiveY,px(76),px(26),TRUE);MoveWindow(objectiveCombo,m+px(78),objectiveY,px(330),px(220),TRUE);
        MoveWindow(objectiveHint,m+px(420),objectiveY,std::max(px(100),w-2*m-px(420)),px(26),TRUE);
        MoveWindow(table,m,px(150),w-2*m,std::max(px(150),h-px(220)),TRUE);
        const int y=h-px(50);MoveWindow(shownAll,m,y,px(112),px(30),TRUE);MoveWindow(shownNone,m+px(120),y,px(104),px(30),TRUE);
        MoveWindow(cancel,w-m-px(92),y,px(92),px(30),TRUE);MoveWindow(start,w-m-px(298),y,px(198),px(30),TRUE);
        RECT tr{};GetClientRect(table,&tr);const int tw=std::max(500,static_cast<int>(tr.right)-GetSystemMetrics(SM_CXVSCROLL)-2);
        const int widths[]={static_cast<int>(tw*.23),static_cast<int>(tw*.19),static_cast<int>(tw*.15),static_cast<int>(tw*.15),static_cast<int>(tw*.15),0};
        int used=0;for(int i=0;i<5;++i){ListView_SetColumnWidth(table,i,widths[i]);used+=widths[i];}ListView_SetColumnWidth(table,5,std::max(55,tw-used));
    }
    bool finishEdit(bool commit) {
        if(!edit||endingEdit)return true;
        endingEdit=true;
        if(commit) {
            const int length=GetWindowTextLengthW(edit);std::wstring text(static_cast<std::size_t>(length)+1,L'\0');
            if(length)GetWindowTextW(edit,text.data(),length+1);
            text.resize(static_cast<std::size_t>(length));double value=0;
            if(!parseNumber(text,value)) {
                endingEdit=false;MessageBeep(MB_ICONWARNING);SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);return false;
            }
            auto& variable=variables[visibleVariables[static_cast<std::size_t>(editRow)]];if(editColumn==3)variable.lower=value;else variable.upper=value;
            setCell(table,editRow,editColumn,displayParameterNumber(value,variable.parameter));
        }
        auto old=edit;edit=nullptr;editRow=editColumn=-1;RemoveWindowSubclass(old,editProc,1);DestroyWindow(old);endingEdit=false;return true;
    }
    void startEdit(int row,int column) {
        if(row<0||static_cast<std::size_t>(row)>=visibleVariables.size()||(column!=3&&column!=4))return;
        if(edit&&!finishEdit(true))return;
        RECT cell{};ListView_GetSubItemRect(table,row,column,LVIR_BOUNDS,&cell);
        editRow=row;editColumn=column;const auto variableIndex=visibleVariables[static_cast<std::size_t>(row)];
        const double value=column==3?variables[variableIndex].lower:variables[variableIndex].upper;
        edit=CreateWindowExW(0,L"EDIT",displayParameterNumber(value,variables[variableIndex].parameter).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            cell.left+1,cell.top+1,std::max(50,static_cast<int>(cell.right-cell.left)-2),std::max(22,static_cast<int>(cell.bottom-cell.top)-2),table,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(SetupEdit)),instance,nullptr);
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(edit,EM_SETLIMITTEXT,64,0);
        SetWindowSubclass(edit,editProc,1,reinterpret_cast<DWORD_PTR>(this));SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);
    }
    bool readOptionalRange(HWND field,std::optional<double>& value,const wchar_t* label) {
        const int length=GetWindowTextLengthW(field);std::wstring text(static_cast<std::size_t>(length)+1,L'\0');
        if(length)GetWindowTextW(field,text.data(),length+1);
        text.resize(static_cast<std::size_t>(length));
        if(text.find_first_not_of(L" \t\r\n")==std::wstring::npos){value.reset();return true;}
        double parsed=0;
        if(!parseNumber(text,parsed)) {
            MessageBoxW(window,(std::wstring(label)+L" must be a finite number or left blank.").c_str(),L"Invalid 2θ range",MB_OK|MB_ICONWARNING);
            SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);return false;
        }
        value=parsed;return true;
    }
    bool validate() {
        if(!finishEdit(true))return false;
        captureVisibleChecks();
        if(!readOptionalRange(rangeMinimum,twoThetaMinimum,L"2θ minimum")||
           !readOptionalRange(rangeMaximum,twoThetaMaximum,L"2θ maximum"))return false;
        objective=ComboBox_GetCurSel(objectiveCombo)==1?FitObjective::PurePoisson:FitObjective::RobustPoisson;
        if(twoThetaMinimum&&twoThetaMaximum&&!(*twoThetaMaximum>*twoThetaMinimum)) {
            MessageBoxW(window,L"2θ Maximum must be greater than 2θ Minimum.",L"Invalid 2θ range",MB_OK|MB_ICONWARNING);
            SetFocus(rangeMaximum);SendMessageW(rangeMaximum,EM_SETSEL,0,-1);return false;
        }
        const auto usable=static_cast<std::size_t>(std::count_if(measured->points.begin(),measured->points.end(),[&](const Point& point){
            return std::isfinite(point.intensity)&&point.intensity>=0&&(!twoThetaMinimum||point.twoTheta>=*twoThetaMinimum)&&
                (!twoThetaMaximum||point.twoTheta<=*twoThetaMaximum);
        }));
        if(usable<8) {
            MessageBoxW(window,L"The selected 2θ range contains fewer than 8 usable measurement points.",L"2θ range is too small",MB_OK|MB_ICONWARNING);
            return false;
        }
        std::size_t selected=0;
        for(std::size_t i=0;i<variables.size();++i) {
            auto& variable=variables[i];if(!variable.selected)continue;++selected;
            const auto name=wide(fitVariableName(*stack,variable));
            if(!std::isfinite(variable.lower)||!std::isfinite(variable.upper)||!(variable.upper>variable.lower)) {
                MessageBoxW(window,(name+L": Lower must be less than Upper.").c_str(),L"Invalid fit bounds",MB_OK|MB_ICONWARNING);return false;
            }
            if((variable.parameter==FitParameter::Density&&variable.lower<=0)||(variable.parameter!=FitParameter::Density&&variable.lower<0)) {
                MessageBoxW(window,(name+L": the lower bound is physically invalid.").c_str(),L"Invalid fit bounds",MB_OK|MB_ICONWARNING);return false;
            }
            if(variable.initial<variable.lower||variable.initial>variable.upper) {
                MessageBoxW(window,(name+L": the current value must lie between Lower and Upper.").c_str(),L"Invalid fit bounds",MB_OK|MB_ICONWARNING);return false;
            }
        }
        if(!selected){MessageBoxW(window,L"Select at least one density, thickness, or roughness variable. Filters only change what is shown; selections hidden by a filter are preserved.",L"Nothing selected",MB_OK|MB_ICONINFORMATION);return false;}
        if(selected>24){MessageBoxW(window,L"Select no more than 24 structural variables in one fitting round.",L"Too many variables",MB_OK|MB_ICONWARNING);return false;}
        return true;
    }
    void close(bool apply) {
        if(apply&&!validate())return;
        finishEdit(false);accepted=apply;DestroyWindow(window);
    }
    static LRESULT CALLBACK editProc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR reference) {
        auto* dialog=reinterpret_cast<SetupDialog*>(reference);
        if(message==WM_KEYDOWN) {
            if(wp==VK_ESCAPE){dialog->finishEdit(false);return 0;}
            if(wp==VK_RETURN){dialog->finishEdit(true);return 0;}
            if(wp==VK_TAB){const int row=dialog->editRow,column=dialog->editColumn;
                if(dialog->finishEdit(true))dialog->startEdit(row,column==3?4:3);
                return 0;}
        }
        return DefSubclassProc(h,message,wp,lp);
    }
};

LRESULT CALLBACK setupProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* dialog=reinterpret_cast<SetupDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){dialog=reinterpret_cast<SetupDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);dialog->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
    if(!dialog)return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:dialog->create();return 0;
    case WM_SIZE:dialog->layout();return 0;
    case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={dialog->px(700),dialog->px(460)};return 0;}
    case WM_DPICHANGED:{
        const RECT* rectangle=reinterpret_cast<RECT*>(lp);dialog->updateDpi(LOWORD(wp));
        SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,
                     SWP_NOZORDER|SWP_NOACTIVATE);dialog->layout();return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==SetupEdit&&HIWORD(wp)==EN_KILLFOCUS){dialog->finishEdit(true);return 0;}
        if(LOWORD(wp)==SetupStart){dialog->close(true);return 0;}
        if(LOWORD(wp)==SetupCancel){dialog->close(false);return 0;}
        if((LOWORD(wp)==SetupParameterFilter||LOWORD(wp)==SetupLayerFilter)&&HIWORD(wp)==CBN_SELCHANGE){dialog->rebuildTable();return 0;}
        if(LOWORD(wp)==SetupShownAll||LOWORD(wp)==SetupShownNone){dialog->selectShown(LOWORD(wp)==SetupShownAll);return 0;}
        break;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->idFrom==SetupTable) {
        auto* item=reinterpret_cast<NMITEMACTIVATE*>(lp);
        if(item->hdr.code==NM_DBLCLK){dialog->startEdit(item->iItem,item->iSubItem);return 0;}
        if(item->hdr.code==LVN_ITEMCHANGED&&!dialog->rebuilding){dialog->updateSummary();return 0;}
    }break;
    case WM_CLOSE:dialog->close(false);return 0;
    case WM_DESTROY:dialog->finished=true;return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

struct ProgressDialog {
    HINSTANCE instance{};HWND owner{},window{},status{},bar{},cancelButton{};HFONT font{},ownedFont{};UINT dpi=96;
    FitObjective objective=FitObjective::RobustPoisson;
    std::atomic_bool cancelled{false},done{false};std::exception_ptr error;FitResult result;
    ~ProgressDialog(){if(ownedFont)DeleteObject(ownedFont);}
    int px(int logical)const{return MulDiv(logical,static_cast<int>(dpi),96);}
    void layout() {
        RECT rectangle{};GetClientRect(window,&rectangle);const int width=rectangle.right,margin=px(18);
        MoveWindow(status,margin,px(18),std::max(px(100),width-2*margin),px(24),TRUE);
        MoveWindow(bar,margin,px(52),std::max(px(100),width-2*margin),px(24),TRUE);
        MoveWindow(cancelButton,width-margin-px(92),px(88),px(92),px(30),TRUE);
    }
    void applyFont() {
        for(HWND control:{status,bar,cancelButton})if(control)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }
    void updateDpi(UINT nextDpi) {
        if(!nextDpi||nextDpi==dpi)return;
        HFONT next=resizedFont(font,dpi,nextDpi);HFONT previousOwned=ownedFont;
        if(next){font=next;ownedFont=next;}
        dpi=nextDpi;
        if(next){applyFont();if(previousOwned)DeleteObject(previousOwned);}
    }
    void create() {
        status=CreateWindowExW(0,L"STATIC",L"Preparing Genetic Algorithm...",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,1,1,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProgressStatus)),instance,nullptr);
        bar=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE|PBS_SMOOTH,0,0,1,1,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProgressBar)),instance,nullptr);
        cancelButton=CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,1,1,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProgressCancel)),instance,nullptr);
        applyFont();SendMessageW(bar,PBM_SETRANGE32,0,1000);layout();
    }
    void requestCancel(){cancelled.store(true,std::memory_order_relaxed);EnableWindow(cancelButton,FALSE);SetWindowTextW(status,L"Cancelling after the current population evaluation...");}
};

LRESULT CALLBACK progressProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* dialog=reinterpret_cast<ProgressDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){dialog=reinterpret_cast<ProgressDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);dialog->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
    if(!dialog)return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:dialog->create();return 0;
    case WM_SIZE:dialog->layout();return 0;
    case WM_DPICHANGED:{
        const RECT* rectangle=reinterpret_cast<RECT*>(lp);dialog->updateDpi(LOWORD(wp));
        SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,
                     SWP_NOZORDER|SWP_NOACTIVATE);dialog->layout();return 0;}
    case fitProgressMessage:{const int value=static_cast<int>(std::min<WPARAM>(1000,wp));SendMessageW(dialog->bar,PBM_SETPOS,value,0);
        const auto mode=dialog->objective==FitObjective::RobustPoisson?L"Robust Poisson":L"Pure Poisson";
        const auto text=L"Genetic Algorithm · "+std::to_wstring(value/10)+L"% · "+mode;SetWindowTextW(dialog->status,text.c_str());return 0;}
    case fitDoneMessage:return 0;
    case WM_COMMAND:if(LOWORD(wp)==ProgressCancel){dialog->requestCancel();return 0;}break;
    case WM_CLOSE:dialog->requestCancel();return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

void registerClass(HINSTANCE instance,const wchar_t* name,WNDPROC procedure,HBRUSH brush) {
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=procedure;type.hInstance=instance;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    type.hbrBackground=brush;type.lpszClassName=name;
    if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("could not register fitting window");
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

SIZE fitWindowSize(HWND owner,int logicalWidth,int logicalHeight) {
    const UINT dpi=dpiForWindow(owner);SIZE size{MulDiv(logicalWidth,static_cast<int>(dpi),96),MulDiv(logicalHeight,static_cast<int>(dpi),96)};
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)) {
        const int workWidth=static_cast<int>(monitor.rcWork.right-monitor.rcWork.left);
        const int workHeight=static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top);
        size.cx=std::min(size.cx,static_cast<LONG>(workWidth*94/100));
        size.cy=std::min(size.cy,static_cast<LONG>(workHeight*94/100));
    }
    return size;
}

bool modalLoop(HWND dialog,HWND owner,const std::function<bool()>& complete) {
    EnableWindow(owner,FALSE);bool quit=false;MSG message{};
    while(!complete()) {
        const int code=GetMessageW(&message,nullptr,0,0);if(code<=0){quit=code==0;break;}
        if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(quit)PostQuitMessage(static_cast<int>(message.wParam));return !quit;
}
}

std::optional<FitDialogSelection> chooseFitVariables(HWND owner,HINSTANCE instance,HFONT font,const Scan& measured,const LayerStack& stack) {
    SetupDialog dialog;dialog.owner=owner;dialog.instance=instance;dialog.dpi=dpiForWindow(owner);
    dialog.ownedFont=resizedFont(font,dialog.dpi,dialog.dpi);dialog.font=dialog.ownedFont?dialog.ownedFont:font;
    dialog.measured=&measured;dialog.stack=&stack;
    dialog.variables=availableFitVariables(stack);
    registerClass(instance,setupClass,setupProc,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
    const SIZE size=fitWindowSize(owner,900,660);
    auto window=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,setupClass,L"Configure this fitting round",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,0,0,size.cx,size.cy,owner,nullptr,instance,&dialog);
    if(!window)throw std::runtime_error("could not create fit configuration window");
    center(window,owner);
    modalLoop(window,owner,[&]{return dialog.finished;});
    if(!dialog.accepted)return std::nullopt;
    return FitDialogSelection{std::move(dialog.variables),dialog.twoThetaMinimum,dialog.twoThetaMaximum,dialog.objective};
}

FitResult runGeneticFitDialog(HWND owner,HINSTANCE instance,HFONT font,const Scan& measured,const LayerStack& stack,
                              const FitDialogSelection& selection,const PoissonModel& initialPoisson) {
    ProgressDialog dialog;dialog.owner=owner;dialog.instance=instance;dialog.dpi=dpiForWindow(owner);
    dialog.ownedFont=resizedFont(font,dialog.dpi,dialog.dpi);dialog.font=dialog.ownedFont?dialog.ownedFont:font;
    dialog.objective=selection.objective;
    registerClass(instance,progressClass,progressProc,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
    const SIZE size=fitWindowSize(owner,540,165);
    auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,progressClass,L"Fitting XRR",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,0,0,size.cx,size.cy,owner,nullptr,instance,&dialog);
    if(!window)throw std::runtime_error("could not create fit progress window");
    center(window,owner);
    std::thread worker([&]{
        try {
            FitOptions options;options.twoThetaMinimum=selection.twoThetaMinimum;options.twoThetaMaximum=selection.twoThetaMaximum;
            options.objective=selection.objective;
            dialog.result=fitXrrGenetic(measured,stack,selection.variables,initialPoisson,options,
                [&](double fraction){PostMessageW(window,fitProgressMessage,static_cast<WPARAM>(std::clamp(fraction,0.0,1.0)*1000),0);},
                [&]{return dialog.cancelled.load(std::memory_order_relaxed);});
        }catch(...){dialog.error=std::current_exception();}
        dialog.done.store(true,std::memory_order_release);PostMessageW(window,fitDoneMessage,0,0);
    });
    modalLoop(window,owner,[&]{return dialog.done.load(std::memory_order_acquire);});worker.join();
    if(IsWindow(window))DestroyWindow(window);
    if(dialog.error)std::rethrow_exception(dialog.error);
    return dialog.result;
}

bool confirmFitResult(HWND owner,const LayerStack& initial,const FitResult& result) {
    std::wstring details;
    for(const auto& variable:result.variables)details+=wide(fitVariableName(initial,variable))+L":  "+displayParameterNumber(variable.initial,variable.parameter)+
        L" → "+displayParameterNumber(variable.fitted,variable.parameter)+L" "+unit(variable.parameter)+L"    ["+
        displayParameterNumber(variable.lower,variable.parameter)+L", "+displayParameterNumber(variable.upper,variable.parameter)+L"]\r\n";
    details+=L"\r\nDiffuse-scattering attenuation strength: "+displayNumber(result.diffuseStrength)+L"\r\n";
    details+=L"Diffuse exponent: "+displayNumber(result.poisson.diffuseExponent)+L"\r\n";
    details+=L"Detector background mean: "+displayNumber(result.detectorMeanCounts)+L" counts\r\n";
    details+=L"Instrument resolution FWHM: "+displayParameterNumber(result.poisson.resolutionFwhmDegrees,FitParameter::Roughness)+L"° (2θ)\r\n";
    if(result.poisson.resolutionFwhmDegrees>=0.0495)
        details+=L"Resolution warning: the fitted value reached the 0.050° search ceiling; inspect absorption, grading or additional interfaces.\r\n";
    const double change=result.initialObjective>0?100.0*(1-result.finalObjective/result.initialObjective):0;
    const auto objectiveName=result.objective==FitObjective::RobustPoisson?L"Robust Poisson":L"Pure Poisson deviance";
    const std::wstring content=L"Fitted 2θ range: "+displayNumber(result.fitTwoThetaMinimum)+L"–"+displayNumber(result.fitTwoThetaMaximum)+
        L"° · "+std::to_wstring(result.pointsInFitRange)+L" usable points · "+std::to_wstring(result.sampledPoints)+L" sampled per GA evaluation\r\n"+
        objectiveName+L" objective: "+displayNumber(result.initialObjective)+L" → "+displayNumber(result.finalObjective)+
        L"  ("+displayNumber(change,5)+L"% improvement)\r\n"+
        L"Raw Poisson deviance/point: "+displayNumber(result.initialPoissonDeviance)+L" → "+displayNumber(result.finalPoissonDeviance)+L"\r\n"+
        L"High-angle diagnostic ("+displayNumber(result.highAngleStart)+L"–"+displayNumber(result.fitTwoThetaMaximum)+L"°) log-RMSE: "+
        displayNumber(result.initialHighAngleLogRmse)+L" → "+displayNumber(result.finalHighAngleLogRmse)+L" decades\r\n"+
        std::to_wstring(result.evaluations)+L" model evaluations · "+
        displayNumber(result.elapsedMilliseconds/1000.0,5)+L" s · "+std::to_wstring(result.workerThreads)+L" worker threads\r\n\r\nApply these fitted structural values to the layer table?";
    TASKDIALOG_BUTTON buttons[]={{IDYES,L"Apply to layer table"},{IDNO,L"Discard result"}};TASKDIALOGCONFIG config{};config.cbSize=sizeof(config);
    config.hwndParent=owner;config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_EXPANDED_BY_DEFAULT|TDF_SIZE_TO_CONTENT;config.pszWindowTitle=L"XRR fit result";
    config.pszMainIcon=TD_INFORMATION_ICON;config.pszMainInstruction=L"Genetic Algorithm fitting completed";config.pszContent=content.c_str();
    config.cButtons=2;config.pButtons=buttons;config.nDefaultButton=IDYES;config.pszExpandedInformation=details.c_str();
    config.pszExpandedControlText=L"Show all fitted parameters";config.pszCollapsedControlText=L"Hide fitted parameters";int button=IDNO;
    const auto status=TaskDialogIndirect(&config,&button,nullptr,nullptr);
    if(FAILED(status))button=MessageBoxW(owner,(content+L"\r\n\r\n"+details).c_str(),L"XRR fit result",MB_YESNO|MB_ICONINFORMATION);
    return button==IDYES;
}
}
