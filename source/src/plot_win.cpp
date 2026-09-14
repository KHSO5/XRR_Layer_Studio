#include "plot_win.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace xrr {
std::wstring wide(const std::string& s) {
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)return L"[invalid UTF-8]";
    std::wstring r(static_cast<std::size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n);return r;
}
std::string utf8(const std::wstring& s) {
    if(s.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(static_cast<std::size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;
}
PlotBox plotBox(int w,int h,double s) {
    return {87*s,66*s,std::max(1.0,w-112*s),std::max(1.0,h-140*s)};
}
namespace {
using namespace Gdiplus;
const Color ink(255,32,58,79), muted(255,94,118,135), measuredCurve(255,18,119,160),
            simulatedCurve(255,226,119,45), selectedOne(255,150,70,190),selectedTwo(255,35,150,105),
            grid(255,226,236,242);
double niceStep(double span) {
    const double a=span/7,b=std::pow(10,std::floor(std::log10(a))),n=a/b;
    return b*(n<=1?1:n<=2?2:n<=5?5:10);
}
std::wstring num(double x,int digits=6) {
    std::wostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(digits)<<x;return o.str();
}
std::wstring power(int n) {
    const std::wstring digits=L"⁰¹²³⁴⁵⁶⁷⁸⁹";std::wstring r=L"10";
    for(wchar_t c:std::to_wstring(n))r+=c==L'-'?L'⁻':digits[static_cast<std::size_t>(c-L'0')];
    return r;
}
void text(Graphics& g,const std::wstring& t,double x,double y,double w,double h,double size,
          Color color=ink,int align=0,bool bold=false) {
    Font f(L"Segoe UI",static_cast<REAL>(size),bold?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush brush(color);StringFormat format;
    format.SetAlignment(align<0?StringAlignmentFar:align>0?StringAlignmentCenter:StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    const RectF r(static_cast<REAL>(x),static_cast<REAL>(y),static_cast<REAL>(w),static_cast<REAL>(h));
    g.DrawString(t.c_str(),static_cast<INT>(t.size()),&f,r,&format,&brush);
}
CLSID pngEncoder() {
    UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);
    std::vector<BYTE> buffer(bytes);auto* enc=reinterpret_cast<ImageCodecInfo*>(buffer.data());
    GetImageEncoders(count,bytes,enc);
    for(UINT i=0;i<count;++i)if(wcscmp(enc[i].MimeType,L"image/png")==0)return enc[i].Clsid;
    throw std::runtime_error("PNG encoder is unavailable");
}
}
void paintPlot(Gdiplus::Graphics& g,int w,int h,double s,const PlotState& state,bool cursor) {
    using namespace Gdiplus;
    g.Clear(Color::White);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    const auto b=plotBox(w,h,s);const auto& v=state.view;
    const auto xx=[&](double x){return b.left+(x-v.xmin)/(v.xmax-v.xmin)*b.width;};
    const auto yy=[&](double y){return b.top+(v.ymax-y)/(v.ymax-v.ymin)*b.height;};
    text(g,L"Intensity vs. 2θ",b.left,8*s,b.width,25*s,18*s,ink,0,true);
    if(state.scan)text(g,wide(state.scan->name),b.left,33*s,state.simulated?std::max(1.0,b.width-285*s):b.width,21*s,11*s,muted);
    else text(g,L"Bruker D8 Advance · RAW4.00 / TXT",b.left,33*s,b.width,21*s,11*s,muted);
    if(state.scan&&state.simulated) {
        const double lx=b.left+b.width-275*s,ly=43*s;
        Pen mp(measuredCurve,static_cast<REAL>(1.4*s)),sp(simulatedCurve,static_cast<REAL>(1.8*s));sp.SetDashStyle(DashStyleDash);
        g.DrawLine(&mp,static_cast<REAL>(lx),static_cast<REAL>(ly),static_cast<REAL>(lx+22*s),static_cast<REAL>(ly));
        text(g,L"Measured",lx+28*s,31*s,76*s,24*s,10*s,muted);
        g.DrawLine(&sp,static_cast<REAL>(lx+112*s),static_cast<REAL>(ly),static_cast<REAL>(lx+134*s),static_cast<REAL>(ly));
        text(g,L"Simulated",lx+140*s,31*s,88*s,24*s,10*s,muted);
    }
    Pen gridPen(grid,static_cast<REAL>(s)),minor(Color(255,243,247,250),static_cast<REAL>(s)),axis(Color(255,125,151,169),static_cast<REAL>(s));
    const double step=niceStep(v.xmax-v.xmin);
    for(double x=std::ceil(v.xmin/step)*step; x<=v.xmax+step*1e-6; x+=step) {
        const auto xp=static_cast<REAL>(xx(x));g.DrawLine(&gridPen,xp,static_cast<REAL>(b.top),xp,static_cast<REAL>(b.top+b.height));
        text(g,num(std::abs(x)<step*1e-6?0:x),xp-45*s,b.top+b.height+7*s,90*s,22*s,12*s,muted,1);
    }
    const double span=v.ymax-v.ymin;const int every=std::max(1,static_cast<int>(std::ceil(span/12)));
    for(int e=static_cast<int>(std::floor(v.ymin));e<=std::ceil(v.ymax);++e) {
        if(span<12)for(int m:{2,5}) {
            const double y=e+std::log10(m);
            if(y>v.ymin&&y<v.ymax)g.DrawLine(&minor,static_cast<REAL>(b.left),static_cast<REAL>(yy(y)),static_cast<REAL>(b.left+b.width),static_cast<REAL>(yy(y)));
        }
        if(e>=v.ymin&&e<=v.ymax&&e%every==0) {
            const auto yp=static_cast<REAL>(yy(e));g.DrawLine(&gridPen,static_cast<REAL>(b.left),yp,static_cast<REAL>(b.left+b.width),yp);
            text(g,power(e),8*s,yp-11*s,b.left-20*s,22*s,12*s,muted,-1);
        }
    }
    if(span<0.9) {
        const double inc=niceStep(span);
        for(double e=std::ceil(v.ymin/inc)*inc;e<=v.ymax;e+=inc) {
            g.DrawLine(&gridPen,static_cast<REAL>(b.left),static_cast<REAL>(yy(e)),static_cast<REAL>(b.left+b.width),static_cast<REAL>(yy(e)));
            text(g,num(std::pow(10,e),3),8*s,yy(e)-11*s,b.left-20*s,22*s,11*s,muted,-1);
        }
    }
    g.DrawLine(&axis,static_cast<REAL>(b.left),static_cast<REAL>(b.top),static_cast<REAL>(b.left),static_cast<REAL>(b.top+b.height));
    g.DrawLine(&axis,static_cast<REAL>(b.left),static_cast<REAL>(b.top+b.height),static_cast<REAL>(b.left+b.width),static_cast<REAL>(b.top+b.height));
    text(g,L"2θ (°)",b.left,b.top+b.height+37*s,b.width,28*s,16*s,ink,1);
    const auto saved=g.Save();g.TranslateTransform(static_cast<REAL>(20*s),static_cast<REAL>(b.top+b.height/2));g.RotateTransform(-90);
    text(g,L"Intensity (log scale)",-b.height/2,-12*s,b.height,26*s,15*s,ink,1);g.Restore(saved);
    std::size_t valid=0;
    if(state.scan) {
        const auto old=g.Save();g.SetClip(RectF(static_cast<REAL>(b.left),static_cast<REAL>(b.top),static_cast<REAL>(b.width),static_cast<REAL>(b.height)));
        const auto map=[&](const Point& p){return PointF(static_cast<REAL>(std::clamp(xx(p.twoTheta),-1000000.0,1000000.0)),
                                                            static_cast<REAL>(std::clamp(yy(std::log10(p.intensity)),-1000000.0,1000000.0)));};
        const auto drawSeries=[&](const Scan& series,Color color,double width,bool dashed,bool dots) {
            GraphicsPath path;bool penDown=false;PointF previous;
            for(const auto& p:series.points) {
                if(p.intensity<=0||!std::isfinite(p.intensity)){penDown=false;continue;}
                if(&series==state.scan)++valid;
                if(p.gapBefore)penDown=false;
                const PointF point=map(p);
                if(penDown)path.AddLine(previous,point);else path.StartFigure();
                previous=point;penDown=true;
            }
            Pen seriesPen(color,static_cast<REAL>(width*s));if(dashed)seriesPen.SetDashStyle(DashStyleDash);
            if(path.GetPointCount()>0)g.DrawPath(&seriesPen,&path);
            if(dots) {
                SolidBrush dot(color);
                for(std::size_t i=0;i<series.points.size();++i) {
                    const auto& p=series.points[i];if(p.intensity<=0)continue;
                    const bool prev=i>0&&series.points[i-1].intensity>0&&!p.gapBefore;
                    const bool next=i+1<series.points.size()&&series.points[i+1].intensity>0&&!series.points[i+1].gapBefore;
                    if(!prev&&!next){const auto point=map(p);g.FillEllipse(&dot,point.X-static_cast<REAL>(2.5*s),point.Y-static_cast<REAL>(2.5*s),static_cast<REAL>(5*s),static_cast<REAL>(5*s));}
                }
            }
        };
        if(state.simulated)drawSeries(*state.simulated,simulatedCurve,1.8,true,false);
        drawSeries(*state.scan,measuredCurve,1.25,false,true);
        if(cursor) {
            const auto selectedMarker=[&](std::ptrdiff_t index,const wchar_t* label,Color color) {
                if(index<0||static_cast<std::size_t>(index)>=state.scan->points.size())return;
                const auto& p=state.scan->points[static_cast<std::size_t>(index)];if(p.intensity<=0||!std::isfinite(p.intensity))return;
                const auto point=map(p);Pen guide(Color(145,color.GetR(),color.GetG(),color.GetB()),static_cast<REAL>(1.1*s));guide.SetDashStyle(DashStyleDash);
                g.DrawLine(&guide,point.X,static_cast<REAL>(b.top),point.X,static_cast<REAL>(b.top+b.height));
                SolidBrush fill(color);Pen outline(Color::White,static_cast<REAL>(1.4*s));
                g.FillEllipse(&fill,point.X-static_cast<REAL>(7*s),point.Y-static_cast<REAL>(7*s),static_cast<REAL>(14*s),static_cast<REAL>(14*s));
                g.DrawEllipse(&outline,point.X-static_cast<REAL>(7*s),point.Y-static_cast<REAL>(7*s),static_cast<REAL>(14*s),static_cast<REAL>(14*s));
                text(g,label,point.X-7*s,point.Y-7*s,14*s,14*s,9*s,Color::White,1,true);
            };
            selectedMarker(state.selectedPoint1,L"1",selectedOne);selectedMarker(state.selectedPoint2,L"2",selectedTwo);
        }
        if(cursor&&state.hovered>=0&&static_cast<std::size_t>(state.hovered)<state.scan->points.size()) {
            const auto& p=state.scan->points[static_cast<std::size_t>(state.hovered)];const auto point=map(p);
            Pen cross(Color(255,137,161,178),static_cast<REAL>(s));cross.SetDashStyle(DashStyleDash);
            g.DrawLine(&cross,point.X,static_cast<REAL>(b.top),point.X,static_cast<REAL>(b.top+b.height));
            SolidBrush white(Color::White);g.FillEllipse(&white,point.X-static_cast<REAL>(4*s),point.Y-static_cast<REAL>(4*s),static_cast<REAL>(8*s),static_cast<REAL>(8*s));
            Pen marker(measuredCurve,static_cast<REAL>(1.35*s));
            g.DrawEllipse(&marker,point.X-static_cast<REAL>(4*s),point.Y-static_cast<REAL>(4*s),static_cast<REAL>(8*s),static_cast<REAL>(8*s));
        }
        g.Restore(old);
    }
    if(!valid)text(g,state.scan?L"没有可绘制的正强度数据":L"打开一个 RAW 或 TXT 文件开始预览",b.left,b.top+b.height/2-20*s,b.width,40*s,15*s,muted,1);
    if(cursor&&state.zoomSelecting) {
        const double left=std::clamp(static_cast<double>(std::min(state.zoomStart.x,state.zoomEnd.x)),b.left,b.left+b.width);
        const double right=std::clamp(static_cast<double>(std::max(state.zoomStart.x,state.zoomEnd.x)),b.left,b.left+b.width);
        const double top=std::clamp(static_cast<double>(std::min(state.zoomStart.y,state.zoomEnd.y)),b.top,b.top+b.height);
        const double bottom=std::clamp(static_cast<double>(std::max(state.zoomStart.y,state.zoomEnd.y)),b.top,b.top+b.height);
        if(right>left&&bottom>top) {
            SolidBrush fill(Color(40,45,112,210));Pen border(Color(220,45,112,210),static_cast<REAL>(1.4*s));border.SetDashStyle(DashStyleDash);
            g.FillRectangle(&fill,static_cast<REAL>(left),static_cast<REAL>(top),static_cast<REAL>(right-left),static_cast<REAL>(bottom-top));
            g.DrawRectangle(&border,static_cast<REAL>(left),static_cast<REAL>(top),static_cast<REAL>(right-left),static_cast<REAL>(bottom-top));
        }
    }
    if(cursor&&state.scan&&state.hovered>=0&&static_cast<std::size_t>(state.hovered)<state.scan->points.size()) {
        const auto& p=state.scan->points[static_cast<std::size_t>(state.hovered)];
        const double tw=std::min(265*s,b.width),th=46*s;
        const double tx=std::clamp(double(state.mouse.x)+14*s,b.left,b.left+b.width-tw);
        const double ty=std::clamp(double(state.mouse.y)-th-10*s,b.top,b.top+b.height-th);
        SolidBrush bg(Color(240,30,58,77));g.FillRectangle(&bg,static_cast<REAL>(tx),static_cast<REAL>(ty),static_cast<REAL>(tw),static_cast<REAL>(th));
        text(g,L"2θ  "+num(p.twoTheta,7)+L"°",tx+10*s,ty+3*s,tw-20*s,20*s,12*s,Color::White);
        text(g,L"Intensity  "+num(p.intensity,8),tx+10*s,ty+23*s,tw-20*s,20*s,12*s,Color::White);
    }
}
void savePlotPng(const std::filesystem::path& file,const PlotState& state) {
    Gdiplus::Bitmap bitmap(2400,1500,PixelFormat32bppARGB);
    {Gdiplus::Graphics g(&bitmap);paintPlot(g,2400,1500,2.0,state,false);}
    const auto encoder=pngEncoder();
    if(bitmap.Save(file.c_str(),&encoder,nullptr)!=Gdiplus::Ok)throw std::runtime_error("PNG export failed");
}
void saveWindowPng(HWND window,const std::filesystem::path& file) {
    RECT r{};GetClientRect(window,&r);
    HDC screen=GetDC(window),memory=CreateCompatibleDC(screen);
    HBITMAP bmp=CreateCompatibleBitmap(screen,r.right,r.bottom),old=static_cast<HBITMAP>(SelectObject(memory,bmp));
    BitBlt(memory,0,0,r.right,r.bottom,screen,0,0,SRCCOPY|CAPTUREBLT);
    SelectObject(memory,old);
    Gdiplus::Bitmap image(bmp,nullptr);const auto encoder=pngEncoder();
    const auto result=image.Save(file.c_str(),&encoder,nullptr);
    DeleteObject(bmp);DeleteDC(memory);ReleaseDC(window,screen);
    if(result!=Gdiplus::Ok)throw std::runtime_error("Window screenshot failed");
}
void savePlotSvg(const std::filesystem::path& file,const PlotState& state) {
    const int w=1200,h=750;const auto b=plotBox(w,h,1);const auto& v=state.view;
    const auto xx=[&](double x){return b.left+(x-v.xmin)/(v.xmax-v.xmin)*b.width;};
    const auto yy=[&](double y){return b.top+(v.ymax-y)/(v.ymax-v.ymin)*b.height;};
    std::ofstream o(file,std::ios::binary);if(!o)throw std::runtime_error("Cannot create SVG");
    o.imbue(std::locale::classic());o<<std::fixed<<std::setprecision(4);
    o<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"750\" viewBox=\"0 0 1200 750\"><rect width=\"1200\" height=\"750\" fill=\"white\"/>";
    auto line=[&](double x1,double y1,double x2,double y2,const char* color){o<<"<line x1=\""<<x1<<"\" y1=\""<<y1<<"\" x2=\""<<x2<<"\" y2=\""<<y2<<"\" stroke=\""<<color<<"\"/>";};
    auto label=[&](double x,double y,const std::string& t,const char* anchor="start",int size=14){
        o<<"<text x=\""<<x<<"\" y=\""<<y<<"\" font-family=\"Segoe UI,Arial,sans-serif\" font-size=\""<<size<<"\" text-anchor=\""<<anchor<<"\" fill=\"#294d63\">"<<xmlEscape(t)<<"</text>";};
    label(b.left,28,"Intensity vs. 2θ","start",19);
    if(state.scan)label(b.left,50,state.scan->name,"start",12);
    if(state.scan&&state.simulated) {
        const double lx=b.left+b.width-270;
        o<<"<line x1=\""<<lx<<"\" y1=\"44\" x2=\""<<lx+22<<"\" y2=\"44\" stroke=\"#1277a0\" stroke-width=\"1.4\"/>";
        label(lx+28,49,"Measured","start",11);
        o<<"<line x1=\""<<lx+112<<"\" y1=\"44\" x2=\""<<lx+134<<"\" y2=\"44\" stroke=\"#e2772d\" stroke-width=\"1.8\" stroke-dasharray=\"7 5\"/>";
        label(lx+140,49,"Simulated","start",11);
    }
    const double step=niceStep(v.xmax-v.xmin);
    for(double x=std::ceil(v.xmin/step)*step;x<=v.xmax+step*1e-6;x+=step){
        line(xx(x),b.top,xx(x),b.top+b.height,"#e2ecf2");label(xx(x),b.top+b.height+26,utf8(num(std::abs(x)<step*1e-6?0:x)),"middle");
    }
    const int every=std::max(1,static_cast<int>(std::ceil((v.ymax-v.ymin)/12)));
    for(int e=static_cast<int>(std::ceil(v.ymin));e<=std::floor(v.ymax);++e)if(e%every==0){
        line(b.left,yy(e),b.left+b.width,yy(e),"#e2ecf2");label(b.left-16,yy(e)+5,utf8(power(e)),"end");
    }
    line(b.left,b.top,b.left,b.top+b.height,"#7d97a9");line(b.left,b.top+b.height,b.left+b.width,b.top+b.height,"#7d97a9");
    label(b.left+b.width/2,h-20,"2θ (°)","middle",19);
    o<<"<text transform=\"translate(24 "<<b.top+b.height/2<<") rotate(-90)\" text-anchor=\"middle\" font-family=\"Segoe UI,Arial,sans-serif\" font-size=\"19\" fill=\"#294d63\">Intensity (log scale)</text>";
    o<<"<defs><clipPath id=\"plot\"><rect x=\""<<b.left<<"\" y=\""<<b.top<<"\" width=\""<<b.width<<"\" height=\""<<b.height<<"\"/></clipPath></defs><g clip-path=\"url(#plot)\">";
    if(state.scan) {
        const auto series=[&](const Scan& scan,const char* color,double width,const char* dash) {
            o<<"<path fill=\"none\" stroke=\""<<color<<"\" stroke-width=\""<<width
             <<"\" stroke-linejoin=\"round\""<<dash<<" d=\"";bool pen=false;
            for(const auto& p:scan.points){if(p.intensity<=0||!std::isfinite(p.intensity)){pen=false;continue;}if(p.gapBefore)pen=false;
                o<<(pen?'L':'M')<<xx(p.twoTheta)<<','<<yy(std::log10(p.intensity));pen=true;}
            o<<"\"/>";
        };
        if(state.simulated)series(*state.simulated,"#e2772d",1.8," stroke-dasharray=\"7 5\"");
        series(*state.scan,"#1277a0",1.3,"");
        for(std::size_t i=0;i<state.scan->points.size();++i){const auto& p=state.scan->points[i];if(p.intensity<=0)continue;
            const bool prev=i>0&&state.scan->points[i-1].intensity>0&&!p.gapBefore;
            const bool next=i+1<state.scan->points.size()&&state.scan->points[i+1].intensity>0&&!state.scan->points[i+1].gapBefore;
            if(!prev&&!next)o<<"<circle cx=\""<<xx(p.twoTheta)<<"\" cy=\""<<yy(std::log10(p.intensity))<<"\" r=\"3\" fill=\"#1277a0\"/>";
        }
    }
    o<<"</g></svg>";o.close();if(!o)throw std::runtime_error("SVG export failed");
}
}
