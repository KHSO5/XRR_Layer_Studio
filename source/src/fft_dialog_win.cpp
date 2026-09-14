#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "fft_dialog_win.hpp"
#include "fft.hpp"
#include <commctrl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace xrr {
namespace {
constexpr wchar_t setupClass[]=L"XrrFftSetup_v251";
constexpr wchar_t resultClass[]=L"XrrFftResult_v251";
constexpr wchar_t spectrumClass[]=L"XrrFftSpectrum_v251";
enum SetupId { SetupMinimum=700,SetupMaximum,SetupThicknessMaximum,SetupCalculate,SetupCancel };
enum ResultId { ResultSpectrum=730,ResultPeaks,ResultClose };

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

std::wstring number(double value,int precision=7) {
    std::wostringstream output;output.imbue(std::locale::classic());output<<std::setprecision(precision)<<value;return output.str();
}

std::wstring fixed(double value,int digits=3) {
    std::wostringstream output;output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(digits)<<value;return output.str();
}

bool parseNumber(HWND field,double& value,bool optional,bool& blank) {
    const int length=GetWindowTextLengthW(field);std::wstring text(static_cast<std::size_t>(length)+1,L'\0');
    if(length)GetWindowTextW(field,text.data(),length+1);
    text.resize(static_cast<std::size_t>(length));
    blank=text.find_first_not_of(L" \t\r\n")==std::wstring::npos;
    if(blank)return optional;
    std::wistringstream input(text);input.imbue(std::locale::classic());
    if(!(input>>value)||!std::isfinite(value))return false;
    input>>std::ws;return input.eof();
}

void setFont(HWND control,HFONT font){if(control&&font)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}

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

void registerClass(HINSTANCE instance,const wchar_t* name,WNDPROC procedure,HBRUSH brush) {
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=procedure;type.hInstance=instance;
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=brush;type.lpszClassName=name;
    if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("could not register FFT window");
}

struct SetupDialog {
    HINSTANCE instance{};HWND owner{},window{},intro{},minimumLabel{},minimum{},maximumLabel{},maximum{},rangeHint{},
        thicknessLabel{},thicknessMaximum{},thicknessHint{},method{},calculate{},cancel{};
    HFONT font{},ownedFont{};UINT dpi=96;const Scan* scan{};ThicknessFftOptions options;bool accepted=false,finished=false;
    ~SetupDialog(){if(ownedFont)DeleteObject(ownedFont);}
    int px(int logical)const{return MulDiv(logical,static_cast<int>(dpi),96);}
    void applyFont(){for(HWND control:{intro,minimumLabel,minimum,maximumLabel,maximum,rangeHint,thicknessLabel,thicknessMaximum,
                                      thicknessHint,method,calculate,cancel})setFont(control,font);}
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
        intro=control(L"STATIC",L"Choose the measured 2θ interval used for thickness analysis. Leave both limits blank to use the complete measured domain.",SS_LEFT,0);
        minimumLabel=control(L"STATIC",L"2θ Minimum (°)",SS_LEFT|SS_CENTERIMAGE,0);
        minimum=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SetupMinimum);
        maximumLabel=control(L"STATIC",L"2θ Maximum (°)",SS_LEFT|SS_CENTERIMAGE,0);
        maximum=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SetupMaximum);
        double first=0,last=0;bool found=false;
        for(const auto& point:scan->points)if(std::isfinite(point.intensity)&&point.intensity>0) {
            if(!found){first=point.twoTheta;found=true;}last=point.twoTheta;
        }
        const auto range=L"Available measured domain: "+number(first)+L"–"+number(last)+L"° · blank = all positive measured points";
        rangeHint=control(L"STATIC",range.c_str(),SS_LEFT|SS_CENTERIMAGE,0);
        SendMessageW(minimum,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"blank = first measured point"));
        SendMessageW(maximum,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"blank = last measured point"));
        thicknessLabel=control(L"STATIC",L"Plot thickness up to (nm)",SS_LEFT|SS_CENTERIMAGE,0);
        thicknessMaximum=control(L"EDIT",L"200",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SetupThicknessMaximum);
        thicknessHint=control(L"STATIC",L"This only changes the displayed/searchable thickness interval; it does not change the measured 2θ range.",SS_LEFT|SS_CENTERIMAGE,0);
        method=control(L"STATIC",L"Method: equal-qz resampling → log₁₀(I) quadratic detrending → Hann window → zero-padded radix-2 FFT. Unmeasured −9999 gaps are not interpolated.",SS_LEFT,0);
        calculate=control(L"BUTTON",L"Calculate FFT",BS_DEFPUSHBUTTON|WS_TABSTOP,SetupCalculate);
        cancel=control(L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP,SetupCancel);
        applyFont();layout();
    }
    void layout() {
        RECT rectangle{};GetClientRect(window,&rectangle);const int width=rectangle.right,height=rectangle.bottom,m=px(18);
        MoveWindow(intro,m,px(16),width-2*m,px(40),TRUE);
        const int row=px(66),labelWidth=px(116),fieldWidth=px(170),gap=px(26);
        MoveWindow(minimumLabel,m,row,labelWidth,px(30),TRUE);MoveWindow(minimum,m+labelWidth,row,fieldWidth,px(30),TRUE);
        const int second=m+labelWidth+fieldWidth+gap;MoveWindow(maximumLabel,second,row,labelWidth,px(30),TRUE);
        MoveWindow(maximum,second+labelWidth,row,fieldWidth,px(30),TRUE);
        MoveWindow(rangeHint,m,px(101),width-2*m,px(28),TRUE);
        MoveWindow(thicknessLabel,m,px(137),px(176),px(30),TRUE);MoveWindow(thicknessMaximum,m+px(180),px(137),px(130),px(30),TRUE);
        MoveWindow(thicknessHint,m+px(322),px(137),std::max(px(100),width-2*m-px(322)),px(30),TRUE);
        MoveWindow(method,m,px(179),width-2*m,px(48),TRUE);
        const int buttonY=height-px(50);MoveWindow(cancel,width-m-px(96),buttonY,px(96),px(32),TRUE);
        MoveWindow(calculate,width-m-px(238),buttonY,px(132),px(32),TRUE);
    }
    bool read() {
        bool blank=false;double value=0;
        if(!parseNumber(minimum,value,true,blank)){MessageBoxW(window,L"2θ Minimum must be a finite number or left blank.",L"Invalid FFT range",MB_OK|MB_ICONWARNING);SetFocus(minimum);return false;}
        options.twoThetaMinimum=blank?std::nullopt:std::optional<double>(value);
        if(!parseNumber(maximum,value,true,blank)){MessageBoxW(window,L"2θ Maximum must be a finite number or left blank.",L"Invalid FFT range",MB_OK|MB_ICONWARNING);SetFocus(maximum);return false;}
        options.twoThetaMaximum=blank?std::nullopt:std::optional<double>(value);
        if(options.twoThetaMinimum&&options.twoThetaMaximum&&!(*options.twoThetaMaximum>*options.twoThetaMinimum)) {
            MessageBoxW(window,L"2θ Maximum must be greater than 2θ Minimum.",L"Invalid FFT range",MB_OK|MB_ICONWARNING);SetFocus(maximum);return false;
        }
        if(!parseNumber(thicknessMaximum,value,false,blank)||blank||value<=0||value>10000) {
            MessageBoxW(window,L"The displayed maximum thickness must be between 0 and 10000 nm.",L"Invalid thickness interval",MB_OK|MB_ICONWARNING);SetFocus(thicknessMaximum);return false;
        }
        options.maximumThicknessNm=value;options.maximumPeaks=8;return true;
    }
    void close(bool use) {
        if(use&&!read())return;
        accepted=use;DestroyWindow(window);
    }
};

LRESULT CALLBACK setupProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* dialog=reinterpret_cast<SetupDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){dialog=reinterpret_cast<SetupDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);dialog->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
    if(!dialog)return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:dialog->create();return 0;
    case WM_SIZE:dialog->layout();return 0;
    case WM_DPICHANGED:{const RECT* rectangle=reinterpret_cast<RECT*>(lp);dialog->updateDpi(LOWORD(wp));
        SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOZORDER|SWP_NOACTIVATE);dialog->layout();return 0;}
    case WM_COMMAND:if(LOWORD(wp)==SetupCalculate){dialog->close(true);return 0;}if(LOWORD(wp)==SetupCancel){dialog->close(false);return 0;}break;
    case WM_CLOSE:dialog->close(false);return 0;
    case WM_DESTROY:dialog->finished=true;return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

using namespace Gdiplus;
const Color ink(255,32,58,79),muted(255,92,116,133),curve(255,48,88,230),peakColor(255,224,121,39),grid(255,224,234,241);

void drawText(Graphics& graphics,const std::wstring& value,double x,double y,double width,double height,double size,
              Color color=ink,int alignment=0,bool bold=false) {
    Font font(L"Segoe UI",static_cast<REAL>(size),bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush brush(color);StringFormat format;
    format.SetAlignment(alignment<0?StringAlignmentFar:alignment>0?StringAlignmentCenter:StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetFormatFlags(StringFormatFlagsNoWrap);
    graphics.DrawString(value.c_str(),static_cast<INT>(value.size()),&font,RectF(static_cast<REAL>(x),static_cast<REAL>(y),static_cast<REAL>(width),static_cast<REAL>(height)),&format,&brush);
}

double niceStep(double range) {
    const double rough=range/7.0,base=std::pow(10.0,std::floor(std::log10(rough))),scaled=rough/base;
    return base*(scaled<=1?1:scaled<=2?2:scaled<=5?5:10);
}

struct ResultDialog {
    HINSTANCE instance{};HWND owner{},window{},summary{},spectrum{},peaks{},note{},closeButton{};
    HFONT font{},ownedFont{};UINT dpi=96;const ThicknessFftResult* result{};bool finished=false;
    ~ResultDialog(){if(ownedFont)DeleteObject(ownedFont);}
    int px(int logical)const{return MulDiv(logical,static_cast<int>(dpi),96);}
    void applyFont() {
        for(HWND control:{summary,peaks,note,closeButton})setFont(control,font);
        if(peaks)setFont(ListView_GetHeader(peaks),font);
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
        const auto text=L"Measured 2θ: "+number(result->twoThetaMinimum)+L"–"+number(result->twoThetaMaximum)+L"°   ·   qz: "+
            number(result->qMinimumInverseAngstrom)+L"–"+number(result->qMaximumInverseAngstrom)+L" Å⁻¹   ·   "+
            std::to_wstring(result->selectedPoints)+L" points → "+std::to_wstring(result->resampledPoints)+L" q-grid → "+
            std::to_wstring(result->fftSize)+L" FFT\r\nIntrinsic thickness resolution ≈ "+fixed(result->thicknessResolutionNm)+
            L" nm   ·   reliable display from "+fixed(result->minimumReliableThicknessNm)+L" to "+fixed(result->displayedMaximumThicknessNm)+L" nm";
        summary=control(L"STATIC",text.c_str(),SS_LEFT,0);
        spectrum=CreateWindowExW(WS_EX_CLIENTEDGE,spectrumClass,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ResultSpectrum)),instance,this);
        peaks=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ResultPeaks)),instance,nullptr);setFont(peaks,font);
        ListView_SetExtendedListViewStyle(peaks,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);SetWindowTheme(peaks,L"Explorer",nullptr);
        const wchar_t* headers[]={L"Rank",L"Candidate thickness (nm)",L"Relative amplitude"};
        for(int column=0;column<3;++column){LVCOLUMNW item{};item.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;item.pszText=const_cast<wchar_t*>(headers[column]);item.cx=100;item.fmt=column?LVCFMT_RIGHT:LVCFMT_LEFT;ListView_InsertColumn(peaks,column,&item);}
        for(std::size_t row=0;row<result->peaks.size();++row) {
            const std::wstring rank=std::to_wstring(row+1),thickness=fixed(result->peaks[row].thicknessNm),amplitude=fixed(result->peaks[row].relativeAmplitude*100.0,1)+L"%";
            LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(row);item.pszText=const_cast<wchar_t*>(rank.c_str());ListView_InsertItem(peaks,&item);
            ListView_SetItemText(peaks,static_cast<int>(row),1,const_cast<wchar_t*>(thickness.c_str()));ListView_SetItemText(peaks,static_cast<int>(row),2,const_cast<wchar_t*>(amplitude.c_str()));
        }
        std::wstring warning=L"FFT peaks are periodic interface distances: a peak may represent one layer, a multilayer sum/difference, or a harmonic. Use these values as starting estimates for Parratt/GA fitting, not as a unique structural solution.";
        if(result->missingGridPoints)warning+=L"\r\n"+std::to_wstring(result->missingGridPoints)+L" unmeasured q-grid positions were kept at zero residual rather than interpolated.";
        note=control(L"STATIC",warning.c_str(),SS_LEFT,0);closeButton=control(L"BUTTON",L"Close",BS_DEFPUSHBUTTON,ResultClose);
        applyFont();layout();
    }
    void layout() {
        RECT rectangle{};GetClientRect(window,&rectangle);const int width=rectangle.right,height=rectangle.bottom,m=px(16);
        MoveWindow(summary,m,px(12),width-2*m,px(54),TRUE);
        int rightWidth=std::clamp(width*31/100,px(285),px(365));const int contentY=px(72),bottom=px(58);
        const int leftWidth=width-3*m-rightWidth;MoveWindow(spectrum,m,contentY,leftWidth,height-contentY-bottom,TRUE);
        const int rightX=2*m+leftWidth,available=height-contentY-bottom,tableHeight=std::max(px(190),available*58/100);
        MoveWindow(peaks,rightX,contentY,rightWidth,tableHeight,TRUE);MoveWindow(note,rightX,contentY+tableHeight+px(10),rightWidth,std::max(px(70),available-tableHeight-px(10)),TRUE);
        MoveWindow(closeButton,width-m-px(100),height-px(46),px(100),px(32),TRUE);
        RECT tableRectangle{};GetClientRect(peaks,&tableRectangle);const int tableWidth=std::max(px(250),static_cast<int>(tableRectangle.right)-GetSystemMetrics(SM_CXVSCROLL)-2);
        ListView_SetColumnWidth(peaks,0,tableWidth*16/100);ListView_SetColumnWidth(peaks,1,tableWidth*49/100);ListView_SetColumnWidth(peaks,2,tableWidth-tableWidth*65/100);
        InvalidateRect(spectrum,nullptr,FALSE);
    }
    void paint(HDC target,int width,int height)const {
        HDC memory=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,width,height);HBITMAP old=static_cast<HBITMAP>(SelectObject(memory,bitmap));
        {Graphics graphics(memory);graphics.Clear(Color::White);graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            const double scale=static_cast<double>(dpiForWindow(spectrum))/96.0,left=75*scale,top=42*scale,right=24*scale,bottom=66*scale;
            const double plotWidth=std::max(1.0,width-left-right),plotHeight=std::max(1.0,height-top-bottom),xMaximum=result->displayedMaximumThicknessNm;
            const auto xPixel=[&](double x){return left+x/xMaximum*plotWidth;};const auto yPixel=[&](double y){return top+(1.05-y)/1.05*plotHeight;};
            drawText(graphics,L"FFT thickness spectrum",left,5*scale,plotWidth,28*scale,18*scale,ink,0,true);
            Pen gridPen(grid,static_cast<REAL>(scale)),axisPen(Color(255,121,145,162),static_cast<REAL>(scale));
            const double xStep=niceStep(xMaximum);
            for(double x=0;x<=xMaximum+xStep*1e-6;x+=xStep) {
                const double pixel=xPixel(x);graphics.DrawLine(&gridPen,static_cast<REAL>(pixel),static_cast<REAL>(top),static_cast<REAL>(pixel),static_cast<REAL>(top+plotHeight));
                drawText(graphics,number(x,5),pixel-35*scale,top+plotHeight+5*scale,70*scale,22*scale,11*scale,muted,1);
            }
            for(int tick=0;tick<=5;++tick) {
                const double y=tick*.2,pixel=yPixel(y);graphics.DrawLine(&gridPen,static_cast<REAL>(left),static_cast<REAL>(pixel),static_cast<REAL>(left+plotWidth),static_cast<REAL>(pixel));
                drawText(graphics,fixed(y,1),5*scale,pixel-11*scale,left-13*scale,22*scale,11*scale,muted,-1);
            }
            graphics.DrawLine(&axisPen,static_cast<REAL>(left),static_cast<REAL>(top),static_cast<REAL>(left),static_cast<REAL>(top+plotHeight));
            graphics.DrawLine(&axisPen,static_cast<REAL>(left),static_cast<REAL>(top+plotHeight),static_cast<REAL>(left+plotWidth),static_cast<REAL>(top+plotHeight));
            drawText(graphics,L"Candidate thickness (nm)",left,top+plotHeight+31*scale,plotWidth,27*scale,14*scale,ink,1);
            const auto saved=graphics.Save();graphics.TranslateTransform(static_cast<REAL>(19*scale),static_cast<REAL>(top+plotHeight/2));graphics.RotateTransform(-90);
            drawText(graphics,L"Normalized FFT amplitude",-plotHeight/2,-12*scale,plotHeight,25*scale,13*scale,ink,1);graphics.Restore(saved);
            if(result->spectrum.size()>1) {
                GraphicsPath path;PointF previous(static_cast<REAL>(xPixel(result->spectrum.front().thicknessNm)),static_cast<REAL>(yPixel(result->spectrum.front().relativeAmplitude)));
                for(std::size_t i=1;i<result->spectrum.size();++i) {const PointF point(static_cast<REAL>(xPixel(result->spectrum[i].thicknessNm)),static_cast<REAL>(yPixel(result->spectrum[i].relativeAmplitude)));path.AddLine(previous,point);previous=point;}
                Pen curvePen(curve,static_cast<REAL>(1.8*scale));graphics.DrawPath(&curvePen,&path);
            }
            Pen marker(peakColor,static_cast<REAL>(1.1*scale));marker.SetDashStyle(DashStyleDash);SolidBrush markerBrush(peakColor);
            const std::size_t labels=std::min<std::size_t>(5,result->peaks.size());
            for(std::size_t i=0;i<labels;++i) {const auto& peak=result->peaks[i];if(peak.thicknessNm>xMaximum)continue;
                const double x=xPixel(peak.thicknessNm),y=yPixel(peak.relativeAmplitude);graphics.DrawLine(&marker,static_cast<REAL>(x),static_cast<REAL>(top+plotHeight),static_cast<REAL>(x),static_cast<REAL>(y));
                graphics.FillEllipse(&markerBrush,static_cast<REAL>(x-3*scale),static_cast<REAL>(y-3*scale),static_cast<REAL>(6*scale),static_cast<REAL>(6*scale));
                const double labelY=std::max(top+2*scale,y-(i%2?22:39)*scale);drawText(graphics,fixed(peak.thicknessNm)+L" nm",x-48*scale,labelY,96*scale,20*scale,10*scale,peakColor,1,true);
            }
        }
        BitBlt(target,0,0,width,height,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
    }
    void close(){DestroyWindow(window);}
};

LRESULT CALLBACK spectrumProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* dialog=reinterpret_cast<ResultDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){dialog=reinterpret_cast<ResultDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
    if(!dialog)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rectangle{};GetClientRect(window,&rectangle);dialog->paint(dc,rectangle.right,rectangle.bottom);EndPaint(window,&paint);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}

LRESULT CALLBACK resultProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* dialog=reinterpret_cast<ResultDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){dialog=reinterpret_cast<ResultDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);dialog->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
    if(!dialog)return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:dialog->create();return 0;
    case WM_SIZE:dialog->layout();return 0;
    case WM_GETMINMAXINFO:{auto* information=reinterpret_cast<MINMAXINFO*>(lp);information->ptMinTrackSize={dialog->px(800),dialog->px(520)};return 0;}
    case WM_DPICHANGED:{const RECT* rectangle=reinterpret_cast<RECT*>(lp);dialog->updateDpi(LOWORD(wp));
        SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOZORDER|SWP_NOACTIVATE);dialog->layout();return 0;}
    case WM_COMMAND:if(LOWORD(wp)==ResultClose){dialog->close();return 0;}break;
    case WM_CLOSE:dialog->close();return 0;
    case WM_DESTROY:dialog->finished=true;return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}

bool showThicknessFftDialog(HWND owner,HINSTANCE instance,HFONT font,const Scan& scan) {
    registerClass(instance,setupClass,setupProcedure,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
    SetupDialog setup;setup.owner=owner;setup.instance=instance;setup.dpi=dpiForWindow(owner);setup.scan=&scan;
    setup.ownedFont=resizedFont(font,setup.dpi,setup.dpi);setup.font=setup.ownedFont?setup.ownedFont:font;
    const SIZE setupSize=windowSize(owner,760,310);
    HWND setupWindow=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,setupClass,L"FFT thickness analysis",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,0,0,setupSize.cx,setupSize.cy,owner,nullptr,instance,&setup);
    if(!setupWindow)throw std::runtime_error("could not create FFT setup window");
    center(setupWindow,owner);modalLoop(setupWindow,owner,[&]{return setup.finished;});if(!setup.accepted)return false;

    const auto result=estimateThicknessFft(scan,setup.options);
    registerClass(instance,spectrumClass,spectrumProcedure,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
    registerClass(instance,resultClass,resultProcedure,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
    ResultDialog display;display.owner=owner;display.instance=instance;display.dpi=dpiForWindow(owner);display.result=&result;
    display.ownedFont=resizedFont(font,display.dpi,display.dpi);display.font=display.ownedFont?display.ownedFont:font;
    const SIZE resultSize=windowSize(owner,1120,730);
    HWND resultWindow=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,resultClass,L"FFT thickness spectrum",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,0,0,resultSize.cx,resultSize.cy,owner,nullptr,instance,&display);
    if(!resultWindow)throw std::runtime_error("could not create FFT result window");
    center(resultWindow,owner);modalLoop(resultWindow,owner,[&]{return display.finished;});return true;
}

}
