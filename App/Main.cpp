#include "../SDK/AudioLink.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr COLORREF Background=RGB(13,17,23),Panel=RGB(22,28,38),Blue=RGB(55,120,255),Text=RGB(236,241,249),Muted=RGB(151,165,187);
constexpr UINT Finished=WM_APP+1;
enum { Refresh=100,RunTest,StopTest,Setup,CopyReport,PairBox };
struct Pair { AudioLink::Endpoint playback,recording; };
struct State {
    HWND window=nullptr,pairs=nullptr;
    HFONT font=nullptr,title=nullptr,small=nullptr,number=nullptr;
    HBRUSH background=CreateSolidBrush(Background),panel=CreateSolidBrush(Panel);
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread worker;
    std::mutex mutex;
    AudioLink::Inventory incoming;
    AudioLink::TestResult incomingTest,result;
    std::vector<Pair> inventory;
    bool busy=false,testing=false,readyInventory=false,preview=false,hasResult=false;
    ULONGLONG started=0;
    std::wstring status=L"Reading audio endpoints. No audio is played or recorded.",report;
    ~State() { if(worker.joinable()) worker.join(); if(stop)CloseHandle(stop); for(auto f:{font,title,small,number})if(f)DeleteObject(f); DeleteObject(background);DeleteObject(panel); }
} state;
void TextAt(HDC dc,HFONT font,COLORREF color,const std::wstring& text,RECT rect,UINT flags=DT_LEFT|DT_TOP|DT_WORDBREAK) {
    SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text.c_str(),-1,&rect,flags);
}
void Box(HDC dc,RECT rect,COLORREF color,int radius=16) {
    HBRUSH brush=CreateSolidBrush(color);HPEN pen=CreatePen(PS_SOLID,1,color);
    const auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
    SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(brush);
}
std::wstring Number(double value,unsigned precision=2) { wchar_t text[80]{};swprintf_s(text,L"%.*f",precision,value);return text; }
std::wstring Format(const AudioLink::Endpoint& endpoint) {
    return std::to_wstring(endpoint.sample_rate)+L" Hz  /  "+std::to_wstring(endpoint.channels)+L" channels  /  "+std::to_wstring(endpoint.bits_per_sample)+L"-bit shared engine";
}
const Pair* Selected() { const auto i=SendMessageW(state.pairs,CB_GETCURSEL,0,0);return i>=0 && static_cast<std::size_t>(i)<state.inventory.size()?&state.inventory[static_cast<std::size_t>(i)]:nullptr; }
void Buttons() {
    EnableWindow(GetDlgItem(state.window,Refresh),!state.busy && !state.preview);
    EnableWindow(GetDlgItem(state.window,RunTest),!state.busy && !state.preview && Selected()!=nullptr);
    EnableWindow(GetDlgItem(state.window,StopTest),state.busy && state.testing);
    EnableWindow(GetDlgItem(state.window,Setup),!state.busy && !state.preview);
    EnableWindow(state.pairs,!state.busy && !state.preview);
    EnableWindow(GetDlgItem(state.window,CopyReport),!state.report.empty());
}
void Layout(int width,int height) {
    MoveWindow(GetDlgItem(state.window,Refresh),width-268,30,100,38,TRUE);
    MoveWindow(GetDlgItem(state.window,Setup),width-154,30,126,38,TRUE);
    MoveWindow(state.pairs,28,132,width-56,240,TRUE);
    MoveWindow(GetDlgItem(state.window,RunTest),28,348,170,42,TRUE);
    MoveWindow(GetDlgItem(state.window,StopTest),210,348,90,42,TRUE);
    MoveWindow(GetDlgItem(state.window,CopyReport),width-154,height-64,126,36,TRUE);
}
void BeginInventory() {
    if(state.busy)return;
    state.busy=true;state.testing=false;state.hasResult=false;state.report.clear();state.status=L"Reading endpoint identities and shared-engine formats...";Buttons();InvalidateRect(state.window,nullptr,FALSE);
    state.worker=std::thread([] {
        auto inventory=AudioLink::List();
        {std::lock_guard<std::mutex> lock(state.mutex);state.incoming=std::move(inventory);state.readyInventory=true;}
        PostMessageW(state.window,Finished,0,0);
    });
}
void BeginTest() {
    const Pair* pair=Selected();if(!pair||state.busy||state.preview)return;
    const auto render=pair->playback.id,capture=pair->recording.id;
    ResetEvent(state.stop);state.busy=true;state.testing=true;state.hasResult=false;state.report.clear();state.started=GetTickCount64();
    state.status=L"Testing this virtual cable. A quiet known signal is generated; only its paired virtual microphone is read.";
    Buttons();SetTimer(state.window,1,33,nullptr);InvalidateRect(state.window,nullptr,FALSE);
    state.worker=std::thread([render,capture] {
        auto result=AudioLink::Test(render,capture,1500,state.stop);
        {std::lock_guard<std::mutex> lock(state.mutex);state.incomingTest=std::move(result);state.readyInventory=false;}
        PostMessageW(state.window,Finished,0,0);
    });
}
void MakeReport() {
    if(!state.hasResult)return;
    const auto& r=state.result;
    state.report=L"Titan Audio Link - virtual cable test\r\n";
    state.report+=r.cancelled?L"Stopped by user.\r\n":(r.error?L"FAILED\r\n":L"PASSED\r\n");
    state.report+=L"Playback endpoint: "+r.render_id+L"\r\nVirtual microphone: "+r.capture_id+L"\r\n";
    state.report+=L"Rendered frames: "+std::to_wstring(r.rendered_frames)+L"\r\nCaptured frames: "+std::to_wstring(r.captured_frames)+L"\r\n";
    state.report+=L"Signal matched: "+std::wstring(r.matched?L"yes":L"no")+L"\r\nCorrelation: "+Number(r.correlation,6)+L"\r\nCapture RMS: "+Number(r.capture_rms,6)+L"\r\n";
    state.report+=L"Independent stereo channels matched: "+std::to_wstring(r.matched_channels)+L" / 2\r\n";
    state.report+=L"Complete stereo frames verified: "+std::to_wstring(r.verified_frames)+L"\r\nMaximum gain-fitted sample error: "+Number(r.max_sample_error,8)+L"\r\n";
    state.report+=L"Discontinuous packets: "+std::to_wstring(r.discontinuities)+L"\r\nTimestamp errors: "+std::to_wstring(r.timestamp_errors)+L"\r\nSilent packets: "+std::to_wstring(r.silent_packets)+L"\r\n";
    state.report+=L"Sample-index offset: "+std::to_wstring(r.sample_offset_frames)+L" frames / "+Number(r.sample_offset_ms,3)+L" ms\r\n";
    state.report+=L"Observed render submission to capture timestamp: "+Number(r.observed_delivery_ms,3)+L" ms\r\nElapsed: "+Number(r.elapsed_ms,1)+L" ms\r\n";
    state.report+=L"These software observations include Windows audio-engine buffering and timestamp/scheduler uncertainty. They are not hardware, acoustic, or application end-to-end latency. No audio was saved to disk.\r\n";
    if(r.error)state.report+=L"Error: "+AudioLink::Describe(r.error)+L"\r\n";
}
void Complete() {
    if(state.worker.joinable())state.worker.join();KillTimer(state.window,1);
    std::lock_guard<std::mutex> lock(state.mutex);
    if(state.readyInventory) {
        SendMessageW(state.pairs,CB_RESETCONTENT,0,0);state.inventory.clear();
        if(state.incoming.error)state.status=AudioLink::Describe(state.incoming.error);
        else {
            for(const auto& out:state.incoming.endpoints)for(const auto& in:state.incoming.endpoints)
                if(out.direction==AudioLink::Direction::Playback && in.direction==AudioLink::Direction::Recording &&
                   out.adapter_instance==in.adapter_instance && out.state==1 && in.state==1)state.inventory.push_back({out,in});
            for(std::size_t i=0;i<state.inventory.size();++i) {
                const auto label=L"Virtual cable "+std::to_wstring(i+1)+L"  /  "+state.inventory[i].playback.adapter_instance;
                SendMessageW(state.pairs,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            }
            if(!state.inventory.empty()) {SendMessageW(state.pairs,CB_SETCURSEL,0,0);state.status=L"Ready. Select this playback destination in the sending app and its virtual microphone in the receiving app.";}
            else {SendMessageW(state.pairs,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No active Titan Audio Link virtual cable"));SendMessageW(state.pairs,CB_SETCURSEL,0,0);state.status=L"Driver not available, disabled, or endpoints are still preparing. Install the signed driver, restart if requested, then refresh.";}
        }
    } else {
        state.result=std::move(state.incomingTest);state.hasResult=true;MakeReport();
        state.status=state.result.cancelled?L"Test stopped by user.":(state.result.error?AudioLink::Describe(state.result.error):L"Test passed. The same virtual cable reproduced the generated signal.");
    }
    state.busy=false;state.testing=false;Buttons();InvalidateRect(state.window,nullptr,FALSE);
}
void Copy() {
    if(state.report.empty()||!OpenClipboard(state.window))return;
    const auto bytes=(state.report.size()+1)*sizeof(wchar_t);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(memory) {void* target=GlobalLock(memory);if(target){memcpy(target,state.report.c_str(),bytes);GlobalUnlock(memory);if(EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory))memory=nullptr;}if(memory)GlobalFree(memory);}
    CloseClipboard();
}
void OpenSetup() {
    wchar_t module[32768]{};const DWORD count=GetModuleFileNameW(nullptr,module,static_cast<DWORD>(std::size(module)));
    if(!count||count==std::size(module))return;
    std::wstring path(module);const auto slash=path.find_last_of(L"\\/");if(slash==std::wstring::npos)return;
    path.resize(slash+1);path+=L"Dependencies\\TitanAudioLinkSetup.exe";
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){MessageBoxW(state.window,L"Driver setup is missing. Extract the complete signed evaluation ZIP and keep Dependencies\\TitanAudioLinkSetup.exe beside this application.",L"Driver setup",MB_OK|MB_ICONINFORMATION);return;}
    SHELLEXECUTEINFOW launch{sizeof(launch)};launch.hwnd=state.window;launch.lpVerb=L"open";launch.lpFile=path.c_str();launch.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&launch))MessageBoxW(state.window,L"Windows could not open the driver installer.",L"Driver setup",MB_OK|MB_ICONERROR);
}
void Paint(HDC dc,int width,int height) {
    RECT all{0,0,width,height};FillRect(dc,&all,state.background);
    Box(dc,{28,30,66,68},Blue,12);TextAt(dc,state.title,Text,L"A",{28,28,66,68},DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    TextAt(dc,state.title,Text,L"Titan Audio Link",{80,26,width-285,60},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    TextAt(dc,state.small,Muted,L"Virtual playback → virtual microphone",{81,65,width-285,90},DT_LEFT|DT_SINGLELINE);
    TextAt(dc,state.font,Text,state.preview?L"OFFLINE PREVIEW  /  synthetic display data only":L"SELECT A VIRTUAL CABLE",{28,106,width-28,132},DT_SINGLELINE);
    const int half=(width-68)/2;
    Box(dc,{28,182,28+half,326},Panel);Box(dc,{40+half,182,width-28,326},Panel);
    const Pair* pair=Selected();
    TextAt(dc,state.small,Muted,L"PLAYBACK DESTINATION  /  audio enters here",{46,199,18+half,222});
    TextAt(dc,state.font,Text,pair?pair->playback.name:L"Not connected",{46,234,18+half,262},DT_SINGLELINE|DT_END_ELLIPSIS);
    TextAt(dc,state.small,Muted,pair?Format(pair->playback):L"Choose this device as output in the sending app.",{46,274,18+half,313});
    TextAt(dc,state.small,Muted,L"VIRTUAL MICROPHONE  /  audio leaves here",{58+half,199,width-46,222});
    TextAt(dc,state.font,Text,pair?pair->recording.name:L"Not connected",{58+half,234,width-46,262},DT_SINGLELINE|DT_END_ELLIPSIS);
    TextAt(dc,state.small,Muted,pair?Format(pair->recording):L"Choose this device as input in the receiving app.",{58+half,274,width-46,313});
    if(state.testing) {
        RECT track{320,364,width-28,372};Box(dc,track,Panel,8);
        const auto elapsed=static_cast<double>(GetTickCount64()-state.started);
        const int span=std::max(40,static_cast<int>((track.right-track.left)/4));const auto t=(std::sin(elapsed/600-1.570796)+1)/2;
        const int at=track.left+static_cast<int>(t*(track.right-track.left-span));Box(dc,{at,track.top,at+span,track.bottom},Blue,8);
    } else TextAt(dc,state.small,Muted,L"Explicit test only. No background microphone capture.",{320,357,width-28,390});
    TextAt(dc,state.font,state.hasResult&&state.result.error?Text:Muted,state.status,{28,410,width-28,480});
    const int y=493,third=(width-80)/3;
    const wchar_t* labels[]={L"KNOWN SIGNAL",L"PACKET DISCONTINUITIES",L"OBSERVED DELIVERY"};
    std::wstring values[]={state.hasResult?(state.result.matched?L"Matched":L"No match"):L"—",state.hasResult?std::to_wstring(state.result.discontinuities):L"—",state.hasResult&&state.result.matched?Number(state.result.observed_delivery_ms,2)+L" ms":L"—"};
    for(int i=0;i!=3;++i) {const int x=28+i*(third+12);Box(dc,{x,y,x+third,y+105},Panel);TextAt(dc,state.small,Muted,labels[i],{x+16,y+14,x+third-12,y+38});TextAt(dc,state.number,Text,values[i],{x+16,y+44,x+third-12,y+86},DT_SINGLELINE);}
    TextAt(dc,state.small,Muted,state.preview?L"Preview does not enumerate, open, play, or record audio. Real verification requires the signed driver.":L"Software endpoint observation only; includes Windows audio-engine buffering. No claim of hardware or acoustic latency.",{28,615,width-28,height-76});
    TextAt(dc,state.small,Muted,L"No default devices are changed. No audio is saved to disk.",{28,height-57,width-178,height-25});
}
LRESULT CALLBACK Window(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE: {
        state.window=window;
        state.font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        state.small=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        state.title=CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        state.number=CreateFontW(-25,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(const auto& button:std::vector<std::pair<int,const wchar_t*>>{{Refresh,L"Refresh"},{RunTest,L"Test virtual cable"},{StopTest,L"Stop"},{Setup,L"Driver setup"},{CopyReport,L"Copy report"}}) {
            HWND control=CreateWindowW(L"BUTTON",button.second,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.first)),nullptr,nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(state.font),FALSE);
        }
        state.pairs=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,0,0,window,reinterpret_cast<HMENU>(PairBox),nullptr,nullptr);SendMessageW(state.pairs,WM_SETFONT,reinterpret_cast<WPARAM>(state.font),FALSE);SendMessageW(state.pairs,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),24);SendMessageW(state.pairs,CB_SETITEMHEIGHT,0,24);
        if(state.preview) {
            Pair sample;sample.playback.name=L"Titan Audio Link Playback";sample.recording.name=L"Titan Audio Link Recording";
            sample.playback.sample_rate=sample.recording.sample_rate=48000;sample.playback.channels=sample.recording.channels=2;sample.playback.bits_per_sample=sample.recording.bits_per_sample=16;
            state.inventory.push_back(sample);SendMessageW(state.pairs,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Synthetic virtual cable  /  offline preview"));SendMessageW(state.pairs,CB_SETCURSEL,0,0);
            state.status=L"Offline design preview. No driver was opened and no audio was generated, captured, or saved.";
            Buttons();
        } else PostMessageW(window,WM_COMMAND,Refresh,0);
        return 0;
    }
    case WM_SIZE:Layout(LOWORD(lParam),HIWORD(lParam));InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize={870,780};return 0;
    case WM_COMMAND:
        if(LOWORD(wParam)==Refresh)BeginInventory();
        else if(LOWORD(wParam)==RunTest)BeginTest();
        else if(LOWORD(wParam)==StopTest){SetEvent(state.stop);state.status=L"Stopping test...";InvalidateRect(window,nullptr,FALSE);}
        else if(LOWORD(wParam)==Setup)OpenSetup();
        else if(LOWORD(wParam)==CopyReport)Copy();
        else if(LOWORD(wParam)==PairBox && HIWORD(wParam)==CBN_SELCHANGE){state.hasResult=false;state.report.clear();Buttons();InvalidateRect(window,nullptr,FALSE);}
        return 0;
    case Finished:Complete();return 0;
    case WM_TIMER:{RECT area{};GetClientRect(window,&area);area.left=320;area.top=348;area.bottom=390;InvalidateRect(window,&area,FALSE);return 0;}
    case WM_SETCURSOR:
        if(LOWORD(lParam)==HTCLIENT && reinterpret_cast<HWND>(wParam)!=window && IsWindowEnabled(reinterpret_cast<HWND>(wParam))){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
        break;
    case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight=24;return TRUE;
    case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lParam);const bool enabled=(item.itemState&ODS_DISABLED)==0;
        if(item.CtlType==ODT_COMBOBOX){FillRect(item.hDC,&item.rcItem,state.panel);std::wstring text;const auto index=item.itemID==static_cast<UINT>(-1)?SendMessageW(item.hwndItem,CB_GETCURSEL,0,0):item.itemID;const auto length=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,index,0);if(length>=0 && length<1024){text.resize(static_cast<std::size_t>(length)+1);SendMessageW(item.hwndItem,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(text.data()));text.resize(static_cast<std::size_t>(length));}RECT r=item.rcItem;r.left+=10;TextAt(item.hDC,state.font,enabled?Text:Muted,text,r,DT_SINGLELINE|DT_LEFT|DT_VCENTER|DT_END_ELLIPSIS);return TRUE;}
        FillRect(item.hDC,&item.rcItem,state.background);const COLORREF fill=enabled&&item.CtlID==RunTest?Blue:Panel;Box(item.hDC,item.rcItem,fill,10);wchar_t text[80]{};GetWindowTextW(item.hwndItem,text,80);TextAt(item.hDC,state.font,enabled?Text:Muted,text,item.rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER);if(item.itemState&ODS_FOCUS){RECT r=item.rcItem;InflateRect(&r,-4,-4);DrawFocusRect(item.hDC,&r);}return TRUE;}
    case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wParam),Text);SetBkColor(reinterpret_cast<HDC>(wParam),Panel);return reinterpret_cast<LRESULT>(state.panel);
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:{RECT client{};GetClientRect(window,&client);Paint(reinterpret_cast<HDC>(wParam),client.right,client.bottom);return 0;}
    case WM_PAINT:{PAINTSTRUCT paint{};HDC target=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);HDC buffer=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);const auto old=SelectObject(buffer,bitmap);Paint(buffer,client.right,client.bottom);BitBlt(target,0,0,client.right,client.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);return 0;}
    case WM_CLOSE:if(state.stop)SetEvent(state.stop);if(state.worker.joinable())state.worker.join();DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
bool RenderPreview(HWND window,const wchar_t* path) {
    RECT rect{};GetClientRect(window,&rect);
    if(rect.right<=0 || rect.bottom<=0 || rect.right>4096 || rect.bottom>4096)return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC source=GetDC(window),memory=CreateCompatibleDC(source);void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(window,source);
    if(!bitmap || !memory){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);return false;}
    const auto old=SelectObject(memory,bitmap);SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_ERASEBKGND);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
        RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;if(width<=0 || height<=0)continue;
        HDC target=CreateCompatibleDC(memory);HBITMAP surface=CreateCompatibleBitmap(memory,width,height);
        if(!target || !surface){if(target)DeleteDC(target);if(surface)DeleteObject(surface);continue;}
        const auto previous=SelectObject(target,surface);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(target),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
        // Some Windows themes omit owner-drawn combo contents when the parent
        // is hidden. Invoke the same real item renderer used by the live control.
        if(child==state.pairs){DRAWITEMSTRUCT item{};item.CtlType=ODT_COMBOBOX;item.CtlID=PairBox;item.itemID=static_cast<UINT>(SendMessageW(child,CB_GETCURSEL,0,0));item.itemAction=ODA_DRAWENTIRE;item.itemState=ODS_DISABLED;item.hwndItem=child;item.hDC=target;item.rcItem={3,3,width-21,height-3};SendMessageW(window,WM_DRAWITEM,PairBox,reinterpret_cast<LPARAM>(&item));}
        BitBlt(memory,bounds.left,bounds.top,width,height,target,0,0,SRCCOPY);SelectObject(target,previous);DeleteObject(surface);DeleteDC(target);
    }
    GdiFlush();const DWORD bytes=static_cast<DWORD>(rect.right*rect.bottom*4);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+bytes;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);bool written=false;
    if(file!=INVALID_HANDLE_VALUE) {
        DWORD n=0;written=WriteFile(file,&header,sizeof(header),&n,nullptr)&&n==sizeof(header)&&WriteFile(file,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&n,nullptr)&&n==sizeof(BITMAPINFOHEADER)&&WriteFile(file,pixels,bytes,&n,nullptr)&&n==bytes;
        CloseHandle(file);if(!written)DeleteFileW(path);
    }
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);return written;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,wchar_t*,int show) {
    SetProcessDPIAware();INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    int argumentCount=0;wchar_t** arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);
    if(!arguments)return 1;
    state.preview=argumentCount==2 && wcscmp(arguments[1],L"--preview")==0;
    std::wstring imagePath;
    if(argumentCount==3 && wcscmp(arguments[1],L"--render-preview")==0){state.preview=true;imagePath=arguments[2];}
    const bool valid=argumentCount==1 || state.preview;LocalFree(arguments);
    if(!valid){MessageBoxW(nullptr,L"Supported option: --preview (offline synthetic UI only).",L"Titan Audio Link",MB_OK|MB_ICONERROR);return 2;}
    WNDCLASSW type{};type.lpfnWndProc=Window;type.hInstance=instance;type.lpszClassName=L"TitanAudioLinkStudio";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&type);
    HWND window=CreateWindowExW(0,type.lpszClassName,L"Titan Audio Link",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1020,810,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    RECT initial{};GetClientRect(window,&initial);Layout(initial.right,initial.bottom);
    if(!imagePath.empty()){const bool rendered=RenderPreview(window,imagePath.c_str());DestroyWindow(window);return rendered?0:1;}
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    return 0;
}
