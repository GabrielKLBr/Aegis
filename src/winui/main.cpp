#include <windows.h>
#include <shobjidl.h>
#include <dwmapi.h>
#undef GetCurrentTime
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <microsoft.ui.xaml.window.h>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <aegis/localization.hpp>
#include "Backend.h"

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
using json = nlohmann::json;

namespace {
std::atomic<unsigned long long> request_id{0};
// Windows std::system_error diagnostics may use the active ANSI code page.
// Never let an error-reporting path throw because those bytes are not UTF-8.
hstring diagnostic_text(std::string_view bytes) {
    if(bytes.empty())return {};
    if(bytes.size()>static_cast<size_t>(INT_MAX))return L"Diagnostic exceeds the display limit.";
    const int length=static_cast<int>(bytes.size());
    UINT code_page=CP_UTF8;
    DWORD flags=MB_ERR_INVALID_CHARS;
    int count=MultiByteToWideChar(code_page,flags,bytes.data(),length,nullptr,0);
    if(!count){code_page=CP_ACP;flags=0;count=MultiByteToWideChar(code_page,flags,bytes.data(),length,nullptr,0);}
    if(!count)return L"Diagnostic could not be decoded.";
    std::wstring wide(static_cast<size_t>(count),L'\0');
    if(!MultiByteToWideChar(code_page,flags,bytes.data(),length,wide.data(),count))return L"Diagnostic could not be decoded.";
    return hstring(wide);
}
int application_exit_code=0;
bool smoke_requested=false;
bool distribution_smoke=false;
std::filesystem::path diagnostics_path;
std::string smoke_locale,smoke_theme;int smoke_width=1200;
size_t in_flight=0;
void diagnostics(std::string const& message) {
    if(diagnostics_path.empty())return;
    try {std::ofstream stream(diagnostics_path,std::ios::app|std::ios::binary);stream<<message<<"\n";}catch(...){}
}

struct Shell : std::enable_shared_from_this<Shell> {
    Window window{nullptr};
    NavigationView navigation{nullptr};
    StackPanel content{nullptr};
    ScrollViewer scroller{nullptr};
    InfoBar info{nullptr};
    TextBlock connection{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
    aegis::Localization localization{"en-US"};
    std::shared_ptr<av::ui::Backend> backend=std::make_shared<av::ui::Backend>(smoke_requested?diagnostics_path.parent_path()/L"state":std::filesystem::path());
    TextBlock session_summary{nullptr};
    StackPanel session_results{nullptr};
    TextBox session_path{nullptr};
    bool session_polling=false;
    json settings = json::object();
    json last_data = json::object();
    std::string page = "dashboard";
    uint64_t generation = 0;
    bool closed = false;
    uint64_t event_cursor=0;bool initial_events=true,events_polling=false;
    bool rebuilding=false;
    std::map<std::string,std::function<void()>> commandActions;
    int scenario=0;
    bool polling = false;
    bool dialog_open = false;
    int ticks = 0;
    size_t smoke_step=0;
    TextBox scan_path{nullptr};
    TextBlock scan_summary{nullptr};
    StackPanel scan_results{nullptr};
    ProgressBar scan_progress{nullptr};
    Button cancel_scan{nullptr};
    TextBox quarantine_id{nullptr};
    TextBox restore_path{nullptr};
    ToggleSwitch quarantine_switch{nullptr}, realtime_switch{nullptr}, offline_switch{nullptr}, lookup_switch{nullptr};
    TextBox watch_paths{nullptr}, excluded_paths{nullptr};
    NumberBox workers{nullptr}, max_file{nullptr}, protection_files{nullptr},protection_seconds{nullptr},schedule_seconds{nullptr};
    ComboBox language{nullptr}, theme{nullptr};
    Grid caption{nullptr};
    TextBox hash_input{nullptr};
    ToggleSwitch hash_online{nullptr};
    StackPanel hash_results{nullptr};
    PasswordBox provider_key{nullptr};
    bool managing_services=false;
    TextBox update_source{nullptr},definition_source{nullptr};
    json deployment_status=json::object(),deployment_operation=json::object();
    int deployment_scenario=0;
    std::vector<std::string> language_ids;

    hstring tr(std::string const& key) const { return to_hstring(localization.text(key)); }
    std::string string(TextBox const& control) const { return to_string(control.Text()); }
    void accessible(DependencyObject const& control, hstring const& name) {
        Automation::AutomationProperties::SetName(control, name);
    }
    TextBlock text(hstring const& value, double size = 14) {
        TextBlock label;
        label.Text(value); label.FontSize(size); label.TextWrapping(TextWrapping::Wrap);
        return label;
    }
    TextBlock label(std::string const& key, double size = 14) { return text(tr(key), size); }
    Button button(std::string const& key, std::function<void()> action, bool accent = false) {
        Button value; value.Content(box_value(tr(key))); accessible(value, tr(key));
        if (accent) value.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        if(smoke_requested)commandActions[key]=action;
        value.Click([action = std::move(action)](auto const&, auto const&) { action(); });
        return value;
    }
    StackPanel row() {
        StackPanel value; value.Orientation(Orientation::Horizontal); value.Spacing(10);
        value.SizeChanged([](auto const& sender, auto const&) {
            auto panel=sender.as<StackPanel>();auto desired=panel.ActualWidth()<650?Orientation::Vertical:Orientation::Horizontal;
            if(panel.Orientation()!=desired)panel.Orientation(desired);
        });
        return value;
    }
    Border card(std::string const& title, hstring const& body) {
        Border value=Markup::XamlReader::Load(L"<Border xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" Background=\"{ThemeResource CardBackgroundFillColorDefaultBrush}\" BorderBrush=\"{ThemeResource CardStrokeColorDefaultBrush}\" />").as<Border>(); value.Padding(Thickness{20,20,20,20}); value.CornerRadius(CornerRadius{10,10,10,10});
        value.BorderThickness(Thickness{1,1,1,1});
        auto resources = Application::Current().Resources();

        StackPanel panel; panel.Spacing(8); panel.Children().Append(label(title, 18)); panel.Children().Append(text(body));
        value.Child(panel); return value;
    }
    TextBox input(std::string const& key, bool multiline = false) {
        TextBox value; value.Header(box_value(tr(key))); accessible(value, tr(key));
        value.PlaceholderText(tr(key)); value.AcceptsReturn(multiline);
        value.TextWrapping(multiline ? TextWrapping::Wrap : TextWrapping::NoWrap);
        value.MinWidth(180); if (multiline) value.MinHeight(90); return value;
    }
    void message(std::string const& title, hstring const& detail, InfoBarSeverity severity = InfoBarSeverity::Informational) {
        if (closed || !info) return;
        info.Title(tr(title)); info.Message(detail); info.Severity(severity); info.IsOpen(true);
    }
    void title(std::string const& key, std::string const& subtitle) {
        content.Children().Append(label(key, 30));
        auto sub = label(subtitle, 15); sub.Margin(Thickness{0,0,0,10}); content.Children().Append(sub);
    }
    HWND hwnd() { HWND handle{}; check_hresult(window.as<IWindowNative>()->get_WindowHandle(&handle)); return handle; }
    std::string choose_path(bool folder, bool save = false) {
        try {
            com_ptr<IFileDialog> dialog;
            if (save) {
                com_ptr<IFileSaveDialog> picker; check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(picker.put())));
                picker->SetDefaultExtension(L"json"); dialog = picker.as<IFileDialog>();
            } else {
                com_ptr<IFileOpenDialog> picker; check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(picker.put())));
                dialog = picker.as<IFileDialog>();
            }
            DWORD flags{}; check_hresult(dialog->GetOptions(&flags));
            flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
            if (folder) flags |= FOS_PICKFOLDERS;
            check_hresult(dialog->SetOptions(flags));
            HRESULT result = dialog->Show(hwnd());
            if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
            check_hresult(result);
            com_ptr<IShellItem> item; check_hresult(dialog->GetResult(item.put()));
            PWSTR path{}; check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
            auto selected = to_string(path); CoTaskMemFree(path); return selected;
        } catch (hresult_error const& e) { message("ui.error", e.message(), InfoBarSeverity::Error); return {}; }
    }
    fire_and_forget rpc(std::string method, json params, std::function<void(json const&)> callback = {}, uint64_t expected = 0, bool quiet = false) {
        if(in_flight>=8){if(!quiet)message("ui.busy",tr("ui.busy"),InfoBarSeverity::Warning);co_return;}++in_flight;
        auto self = shared_from_this();
        auto queue = dispatcher;
        json result; std::string failure;bool reached_server=false;
        co_await resume_background();
        try {
            result = self->backend->Request(method,params);
            reached_server=true;
        } catch (std::exception const& e) { failure = e.what(); }
        catch (...) { failure = "Unexpected IPC failure"; }
        reached_server=av::services::Query(av::services::Kind::Antivirus).Running();
        queue.TryEnqueue([self, result = std::move(result), failure = std::move(failure), callback = std::move(callback), expected, quiet, method,reached_server] {
            --in_flight;
            if (self->closed) return;
            if(method=="events")self->events_polling=false;
            if (method == "scan.status") self->polling = false;
            if (method == "session.status") self->session_polling=false;
            if (!failure.empty()) {
                self->connection.Text(self->tr(reached_server?"ui.connected":"ui.disconnected"));
                if (!quiet && (!expected || expected == self->generation)) self->message("ui.error", diagnostic_text(failure), InfoBarSeverity::Error);
                return;
            }
            self->connection.Text(self->tr(reached_server?"ui.connected":"ui.disconnected"));
            if(smoke_requested)diagnostics("IPC "+method+" connected");
            if (expected && expected != self->generation) return;
            if (callback) {
                try {callback(result);}
                catch(hresult_error const& e){self->message("ui.error",e.message(),InfoBarSeverity::Error);}
                catch(std::exception const& e){self->message("ui.error",diagnostic_text(e.what()),InfoBarSeverity::Error);}
            }
        });
    }
    void start_scan(std::string mode) {
        json params{{"mode",mode}};
        if (mode == "custom") {
            if (!scan_path || string(scan_path).empty()) { message("ui.path", tr("ui.path"), InfoBarSeverity::Warning); return; }
            params["path"] = string(scan_path);
        }
        rpc("scan.start", params, [self=shared_from_this()](auto const&) {
            self->message("scan.started", L"", InfoBarSeverity::Success);
            self->show_page("scan"); self->poll_scan();
        });
    }
    void render_json(StackPanel const& target, json const& data, size_t limit = 100) {
        target.Children().Clear();
        auto list = data;
        if (data.is_object()) {
            if (data.contains("items")) list = data["items"];
            else if (data.contains("events")) list = data["events"];
            else if (data.contains("results")) list = data["results"];
        }
        if ((list.is_array() || list.is_object()) && list.empty()) { target.Children().Append(label("ui.none")); return; }
        if (list.is_array()) {
            size_t count = 0;
            for (auto const& item : list) {
                if (++count > limit) break;
                StackPanel evidence;evidence.Spacing(8);
                if(item.is_object()) {
                    auto path=item.value("path",item.value("original_path",item.value("source",std::string{})));
                    if(item.contains("kind")){auto title=item.value("kind",0)==1?"services.av.title":"services.sandbox.title";evidence.Children().Append(text(tr(title)+L": "+tr(item.value("running",false)?"services.running":"services.stopped")));}
                    if(item.contains("complete")&&!item.value("complete",false))evidence.Children().Append(label("coverage.incomplete"));
                    if(item.contains("filesScanned"))evidence.Children().Append(text(to_hstring(localization.plural("scan.files",item.value("filesScanned",0LL)))));
                    if(item.contains("threatsFound"))evidence.Children().Append(text(to_hstring(localization.plural("scan.threats",item.value("threatsFound",0LL)))));
                    if(item.contains("state")&&item["state"].is_string())evidence.Children().Append(text(tr("session.state."+item["state"].get<std::string>())));
                    if(!path.empty()){auto name=text(to_hstring(path),15);name.IsTextSelectionEnabled(true);evidence.Children().Append(name);}
                    if(item.contains("created")&&item["created"].is_number_integer())evidence.Children().Append(text(to_hstring(localization.timestamp(item["created"].get<long long>()))));
                    if(item.contains("status"))evidence.Children().Append(text(to_hstring(item["status"].is_string()?item["status"].get<std::string>():item["status"].dump()),14));
                    if(item.contains("id")){auto id=text(to_hstring(item["id"].dump()),13);id.IsTextSelectionEnabled(true);evidence.Children().Append(id);}
                }
                auto detail = text(to_hstring(item.dump(2).substr(0,16384)), 12);detail.IsTextSelectionEnabled(true);
                Expander disclosure;disclosure.Header(box_value(tr("ui.details")));disclosure.Content(detail);disclosure.HorizontalContentAlignment(HorizontalAlignment::Stretch);evidence.Children().Append(disclosure);
                Border wrapper; wrapper.Padding(Thickness{14,14,14,14}); wrapper.CornerRadius(CornerRadius{8,8,8,8}); wrapper.BorderThickness(Thickness{1,1,1,1});
                wrapper.BorderBrush(Application::Current().Resources().Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
                wrapper.Child(evidence); target.Children().Append(wrapper);
            }
        } else {
            auto detail = text(to_hstring(list.dump(2).substr(0,32768)),13); detail.IsTextSelectionEnabled(true); target.Children().Append(detail);
        }
    }
    void poll_scan(bool quiet=true) {
        if (closed || page != "scan" || polling) return;
        polling = true;
        rpc("scan.status", json::object(), [self=shared_from_this()](json const& result) {
            self->last_data = result;
            bool running = result.value("running", false);
            if (result.contains("state")) running = result.value("state", std::string{}) == "running";
            auto completed=result.value("completed",result.value("scanned",uint64_t(0)));
            auto summary=to_hstring(self->localization.plural("scan.files",static_cast<long long>(completed)));
            if(running)summary=summary+L"\n"+self->tr("scan.progress");else if(!result.value("complete",false))summary=summary+L"\n"+self->tr("coverage.incomplete");
            auto current=result.value("current_path",std::string{});if(!current.empty())summary=summary+L"\n"+to_hstring(current);
            auto error=result.value("last_error",std::string{});if(!error.empty())summary=summary+L"\n"+to_hstring(error);
            self->scan_summary.Text(summary);
            self->scan_progress.IsIndeterminate(running); self->scan_progress.Visibility(running ? Visibility::Visible : Visibility::Collapsed);
            self->cancel_scan.IsEnabled(running);
            if (result.contains("results")) self->render_json(self->scan_results, result["results"]);
            else if (result.contains("detections")) self->render_json(self->scan_results, result["detections"]);
            else if(result.contains("last_result")&&!result["last_result"].is_null()&&!result["last_result"].empty())self->render_json(self->scan_results,json::array({result["last_result"]}));
        }, generation, quiet);
    }
    fire_and_forget capture_smoke_image(std::string selected) {
        auto self=shared_from_this();
        try {
            Media::Imaging::RenderTargetBitmap bitmap;
            co_await bitmap.RenderAsync(window.Content().as<FrameworkElement>());
            auto pixels=co_await bitmap.GetPixelsAsync();
            std::vector<uint8_t> bytes(pixels.Length());
            Windows::Storage::Streams::DataReader::FromBuffer(pixels).ReadBytes(bytes);
            auto folder=co_await Windows::Storage::StorageFolder::GetFolderFromPathAsync(diagnostics_path.parent_path().wstring());
            auto file=co_await folder.CreateFileAsync(to_hstring("Aegis-UI-"+selected+".png"),Windows::Storage::CreationCollisionOption::ReplaceExisting);
            auto stream=co_await file.OpenAsync(Windows::Storage::FileAccessMode::ReadWrite);
            auto encoder=co_await Windows::Graphics::Imaging::BitmapEncoder::CreateAsync(Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId(),stream);
            encoder.SetPixelData(Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied,bitmap.PixelWidth(),bitmap.PixelHeight(),96,96,bytes);
            co_await encoder.FlushAsync();
            diagnostics("screenshot "+to_string(file.Path()));
        }catch(hresult_error const& e){diagnostics("Screenshot unavailable: "+to_string(e.message()));}
    }
    void build_dashboard() {
        title("nav.dashboard", "dashboard.subtitle");
        content.Children().Append(card("dashboard.protection", tr("ui.loading")));
        auto service_card = content.Children().GetAt(content.Children().Size()-1).as<Border>();
        content.Children().Append(card("dashboard.privacy", tr("dashboard.privacyDetail")));
        auto actions = row(); actions.Children().Append(button("scan.quick", [self=shared_from_this()]{self->start_scan("quick");}, true));
        actions.Children().Append(button("scan.full", [self=shared_from_this()]{self->start_scan("full");})); content.Children().Append(actions);
        content.Children().Append(card("dashboard.latest", tr("ui.notScanned")));
        auto latest = content.Children().GetAt(content.Children().Size()-1).as<Border>();
        content.Children().Append(label("dashboard.notFullAV")); content.Children().Append(label("dashboard.noGuarantee"));
        rpc("status", json::object(), [self=shared_from_this(),service_card](json const& result) {
            self->last_data = result;
            if(smoke_requested)diagnostics("service="+result.value("service",std::string("unknown")));
            auto panel = service_card.Child().as<StackPanel>();
            panel.Children().GetAt(1).as<TextBlock>().Text(self->tr(result.value("monitoringActive",false)?"dashboard.ready":"dashboard.disabled")+L"\n"+self->tr("dashboard.connectedDetail"));
            auto realtime=result;
            auto state=realtime.value("monitoringActive",false)?self->tr("dashboard.enabled"):self->tr("dashboard.disabled");
            panel.Children().Append(self->text(self->tr("dashboard.realtime")+L": "+state));
        }, generation);
        rpc("scan.status", json::object(), [self=shared_from_this(),latest](json const& result) {
            auto summary=self->localization.plural("scan.files",static_cast<long long>(result.value("completed",uint64_t(0))));
            if(!result.value("complete",false))summary+="\n"+to_string(self->tr("coverage.incomplete"));latest.Child().as<StackPanel>().Children().GetAt(1).as<TextBlock>().Text(to_hstring(summary));
        }, generation, true);
    }
    void build_scan() {
        title("nav.scan", "scan.subtitle");
        content.Children().Append(label("scan.warning"));
        auto actions = row(); actions.Children().Append(button("scan.quick", [self=shared_from_this()]{self->start_scan("quick");},true));
        actions.Children().Append(button("scan.full", [self=shared_from_this()]{self->start_scan("full");}));
        cancel_scan = button("scan.cancel", [self=shared_from_this()]{self->rpc("scan.cancel", json::object(), [self](auto const&){self->message("scan.cancelled",L"");});});
        cancel_scan.IsEnabled(false); actions.Children().Append(cancel_scan); content.Children().Append(actions);
        content.Children().Append(label("scan.fullNote")); scan_path = input("ui.path"); content.Children().Append(scan_path);
        auto custom = row(); custom.Children().Append(button("ui.browseFile", [self=shared_from_this()]{ auto path = self->choose_path(false); if(!path.empty()) self->scan_path.Text(to_hstring(path)); }));
        custom.Children().Append(button("ui.browseFolder", [self=shared_from_this()]{ auto path = self->choose_path(true); if(!path.empty()) self->scan_path.Text(to_hstring(path)); }));
        custom.Children().Append(button("scan.custom", [self=shared_from_this()]{self->start_scan("custom");})); content.Children().Append(custom);
        content.Children().Append(label("scan.progress", 20));
        scan_progress = ProgressBar(); scan_progress.Visibility(Visibility::Collapsed); accessible(scan_progress,tr("scan.progress")); content.Children().Append(scan_progress);
        scan_summary = label("ui.loading"); scan_summary.IsTextSelectionEnabled(true); content.Children().Append(scan_summary);
        content.Children().Append(label("scan.results",20)); scan_results = StackPanel(); scan_results.Spacing(8); content.Children().Append(scan_results);
        content.Children().Append(label("hash.title",20));content.Children().Append(label("hash.privacy"));
        hash_input=input("hash.input");content.Children().Append(hash_input);
        auto hashActions=row();hashActions.Children().Append(button("hash.file",[self=shared_from_this()]{const auto path=self->choose_path(false);if(!path.empty())self->hash_input.Text(to_hstring(path));}));
        hash_online=ToggleSwitch();hash_online.Header(box_value(tr("hash.online")));accessible(hash_online,tr("hash.online"));hash_online.IsOn(false);content.Children().Append(hash_online);
        hashActions.Children().Append(button("hash.query",[self=shared_from_this()]{
            const auto value=self->string(self->hash_input);json query{{"schema",1},{"online",self->hash_online.IsOn()}};
            const bool isHash=value.size()==64&&value.find_first_not_of("0123456789abcdefABCDEF")==value.npos;query[isHash?"sha256":"source"]=value;
            self->hash_results.Children().Clear();self->hash_results.Children().Append(self->label("ui.loading"));
            self->rpc("reputation.lookup",query,[self](const json& result){self->hash_results.Children().Clear();auto digest=self->text(to_hstring(result.at("sha256").get<std::string>()));digest.IsTextSelectionEnabled(true);self->hash_results.Children().Append(digest);
                for(const auto& item:result.at("items"))self->hash_results.Children().Append(self->text(to_hstring(item.at("provider").get<std::string>())+L": "+self->tr("hash.status."+item.at("status").get<std::string>())));
                self->hash_results.Children().Append(self->label("hash.evidence"));if(smoke_requested){self->last_hash=result;diagnostics("PASS: UI hash query command");}
            },self->generation);
        }));content.Children().Append(hashActions);hash_results=StackPanel();hash_results.Spacing(8);content.Children().Append(hash_results);
        poll_scan(false);
    }
    static json lines(std::string const& value) {
        json result = json::array(); std::istringstream stream(value); std::string line;
        while (std::getline(stream,line)) { if(!line.empty() && line.back()=='\r') line.pop_back(); if(!line.empty()) result.push_back(line); }
        return result;
    }
    static hstring join_lines(json const& value) {
        std::string result; if(value.is_array()) for(auto const& line:value) if(line.is_string()) result += line.get<std::string>() + "\r\n";
        return to_hstring(result);
    }
    void poll_events(){
        if(events_polling||closed)return;events_polling=true;
        rpc("events",{{"after",event_cursor}},[self=shared_from_this()](const json& page){
            if(self->initial_events){self->initial_events=false;self->event_cursor=page.value("lastSequence",uint64_t(0));return;}
            self->event_cursor=page.value("next",self->event_cursor);
            if(page.value("cursorGap",false))self->message("ui.notification",self->tr("events.gap"),InfoBarSeverity::Warning);
            for(const auto& event:page.at("events")){
                const auto action=event.value("action",std::string());if(action=="threat-detected"||action=="coverage-gap"||action=="protection-error")self->message("ui.notification",diagnostic_text(action+": "+event.value("path",event.value("error",std::string()))),InfoBarSeverity::Warning);
            }
        },0,true);
    }
    void build_realtime() {
        title("nav.realtime","realtime.subtitle");
        content.Children().Append(card("ui.details",tr("realtime.notice")));
        realtime_switch=ToggleSwitch();realtime_switch.Header(box_value(tr("realtime.enable")));accessible(realtime_switch,tr("realtime.enable"));content.Children().Append(realtime_switch);
        watch_paths=input("realtime.watch",true);excluded_paths=input("realtime.exclusions",true);content.Children().Append(watch_paths);content.Children().Append(excluded_paths);
        quarantine_switch=ToggleSwitch();quarantine_switch.Header(box_value(tr("protection.quarantine")));accessible(quarantine_switch,tr("protection.quarantine"));content.Children().Append(quarantine_switch);
        auto number=[&](const char* key,double min,double max){NumberBox n;n.Header(box_value(tr(key)));accessible(n,tr(key));n.Minimum(min);n.Maximum(max);n.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);content.Children().Append(n);return n;};
        protection_files=number("protection.maxFiles",1,100000);protection_seconds=number("protection.maxSeconds",1,86400);schedule_seconds=number("protection.schedule",0,604800);
        content.Children().Append(label("protection.admin"));
        auto save=button("ui.save",[self=shared_from_this()]{
            auto f=self->protection_files.Value(),t=self->protection_seconds.Value(),interval=self->schedule_seconds.Value();
            if(!std::isfinite(f)||!std::isfinite(t)||!std::isfinite(interval)||f<1||f>100000||t<1||t>86400||(interval!=0&&interval<60)||interval>604800||f!=std::floor(f)||t!=std::floor(t)||interval!=std::floor(interval)){self->message("ui.error",self->tr("protection.schedule"),InfoBarSeverity::Warning);return;}
            self->rpc("protection.set",{{"enabled",self->realtime_switch.IsOn()},{"roots",lines(self->string(self->watch_paths))},{"exclusions",lines(self->string(self->excluded_paths))},{"autoQuarantine",self->quarantine_switch.IsOn()},{"maxFiles",static_cast<int>(f)},{"maxSeconds",static_cast<int>(t)},{"uiScheduleSeconds",static_cast<int>(interval)}},[self](auto const&){self->show_page("realtime");self->message("ui.saved",L"",InfoBarSeverity::Success);},self->generation);
        },true);save.IsEnabled(false);content.Children().Append(save);
        auto status=label("ui.loading");content.Children().Append(status);
        rpc("protection.get",json::object(),[self=shared_from_this(),status,save](json const& data){
            const auto policy=data.at("policy");self->realtime_switch.IsOn(policy.value("enabled",false));self->watch_paths.Text(join_lines(policy.at("roots")));self->excluded_paths.Text(join_lines(policy.at("exclusions")));self->quarantine_switch.IsOn(policy.value("autoQuarantine",false));self->protection_files.Value(policy.value("maxFiles",10000));self->protection_seconds.Value(policy.value("maxSeconds",1800));self->schedule_seconds.Value(0);
            for(const auto& schedule:policy.at("schedules"))if(schedule.value("id",std::string())=="winui17-quick")self->schedule_seconds.Value(schedule.value("intervalSeconds",0));
            status.Text(self->tr(data.value("monitoringActive",false)?"dashboard.enabled":"dashboard.disabled")+L"\n"+to_hstring(data.value("lastError",std::string())));save.IsEnabled(true);
        },generation);
    }

    void poll_session(){
        if(session_polling||closed||!session_summary)return;session_polling=true;
        rpc("session.status",json::object(),[self=shared_from_this()](json const& data){self->last_data=data;self->session_summary.Text(self->tr("session.state."+data.value("state",std::string("idle"))));self->render_json(self->session_results,data.contains("report")?data.at("report"):data);},generation,true);
    }
    void build_session(std::string const& kind){
        title("nav."+kind,kind=="vm"?"vm.subtitle":kind=="behavior"?"behavior.subtitle":"sandbox.subtitle");
        content.Children().Append(label("session.notice"));
        if(kind!="behavior"){
            session_path=input("session.path");content.Children().Append(session_path);
            auto actions=row();actions.Children().Append(button("ui.browseFile",[self=shared_from_this()]{const auto path=self->choose_path(false);if(!path.empty())self->session_path.Text(to_hstring(path));}));
            for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"session.run","session.start"},{"session.save","session.checkpoint"},{"session.resume","session.resume"},{"session.bundle","session.bundle"}}){
                actions.Children().Append(button(pair.first,[self=shared_from_this(),method=pair.second]{self->rpc(method,{{"path",self->string(self->session_path)}},[self](auto const&){self->poll_session();});}));
            }
            actions.Children().Append(button("ui.cancel",[self=shared_from_this()]{self->rpc("session.cancel",json::object());}));content.Children().Append(actions);
        }
        session_summary=label("ui.loading");content.Children().Append(session_summary);session_results=StackPanel();session_results.Spacing(8);content.Children().Append(session_results);
        content.Children().Append(button("ui.export",[self=shared_from_this()]{self->export_json();}));poll_session();
        if(kind=="vm")content.Children().Append(label("vm.limits"));
        if(kind=="behavior")content.Children().Append(label("behavior.limits"));
    }
    fire_and_forget confirm_quarantine(bool remove) {
        auto self=shared_from_this();
        if(dialog_open)co_return;
        auto id=string(quarantine_id); auto destination=string(restore_path);
        if(id.empty() || (!remove && destination.empty())) {message("ui.error",tr(remove?"quarantine.id":"quarantine.destination"),InfoBarSeverity::Warning);co_return;}
        dialog_open=true;
        try {
            ContentDialog dialog; dialog.XamlRoot(content.XamlRoot()); dialog.Title(box_value(tr("ui.confirm")));
            dialog.Content(box_value(tr(remove?"quarantine.deleteWarning":"quarantine.restoreWarning")));
            dialog.PrimaryButtonText(tr(remove?"ui.delete":"ui.restore")); dialog.CloseButtonText(tr("ui.cancel"));
            dialog.DefaultButton(ContentDialogButton::Close);
            auto choice=co_await dialog.ShowAsync();
            if(choice==ContentDialogResult::Primary&&!closed) {
                json params{{"id",id}}; if(!remove)params["destination"]=destination;
                rpc(remove?"quarantine.delete":"quarantine.restore",params,[self](auto const&){self->show_page("quarantine");self->message("quarantine.changed",L"",InfoBarSeverity::Success);});
            }
        } catch(hresult_error const& e){message("ui.error",e.message(),InfoBarSeverity::Error);}
        dialog_open=false;
    }
    fire_and_forget isolate_selected(){
        auto self=shared_from_this();if(dialog_open)co_return;const auto path=choose_path(false);if(path.empty())co_return;dialog_open=true;
        try{ContentDialog dialog;dialog.XamlRoot(content.XamlRoot());dialog.Title(box_value(tr("quarantine.add")));dialog.Content(box_value(tr("quarantine.isolateWarning")));dialog.PrimaryButtonText(tr("quarantine.add"));dialog.CloseButtonText(tr("ui.cancel"));dialog.DefaultButton(ContentDialogButton::Close);
            if(co_await dialog.ShowAsync()==ContentDialogResult::Primary&&!closed)rpc("quarantine.add",{{"path",path}},[self](auto const&){self->show_page("quarantine");self->message("quarantine.changed",L"",InfoBarSeverity::Success);});
        }catch(const hresult_error& e){message("ui.error",e.message(),InfoBarSeverity::Error);}dialog_open=false;
    }
    void build_quarantine() {
        title("nav.quarantine","quarantine.subtitle");
        auto list=StackPanel(); list.Spacing(8); content.Children().Append(list);
        rpc("quarantine.list",json::object(),[self=shared_from_this(),list](json const& data){self->last_data=data;self->render_json(list,data.contains("entries")?data.at("entries"):data);},generation);
        auto add=button("quarantine.add",[self=shared_from_this()]{self->isolate_selected();});content.Children().Append(add);
        quarantine_id=input("quarantine.id"); restore_path=input("quarantine.destination");
        content.Children().Append(quarantine_id); content.Children().Append(restore_path);
        auto actions=row(); actions.Children().Append(button("ui.restore",[self=shared_from_this()]{self->confirm_quarantine(false);}));
        actions.Children().Append(button("ui.delete",[self=shared_from_this()]{self->confirm_quarantine(true);}));content.Children().Append(actions);
    }
    fire_and_forget export_json() {
        const auto selected=choose_path(false,true);if(selected.empty())co_return;
        auto self=shared_from_this();const auto report=last_data;auto queue=dispatcher;std::string error;
        co_await resume_background();
        try{av::ui::Backend::Export(std::filesystem::path(av::services::Wide(selected)),report);}catch(const std::exception& e){error=e.what();}
        queue.TryEnqueue([self,selected,error]{if(self->closed)return;if(error.empty())self->message("ui.exported",to_hstring(selected),InfoBarSeverity::Success);else self->message("ui.error",diagnostic_text(error),InfoBarSeverity::Error);});
    }
    void build_history() {
        title("nav.history","history.subtitle");
        auto export_button=button("ui.export",[self=shared_from_this()]{self->export_json();});export_button.IsEnabled(false);content.Children().Append(export_button);
        auto events=StackPanel();events.Spacing(8);content.Children().Append(events);
        auto filters=row();for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"history.scans","history"},{"history.protection","events"},{"history.vault","quarantine.audit"}})filters.Children().Append(button(item.first,[self=shared_from_this(),events,method=item.second,export_button]{self->rpc(method,json::object(),[self,events,export_button](const json& data){self->last_data=data;self->render_json(events,data);export_button.IsEnabled(true);},self->generation);}));content.Children().Append(filters);
        rpc("history",json{{"limit",10}},[self=shared_from_this(),events,export_button](json const& result){self->last_data=result;self->render_json(events,result);export_button.IsEnabled(true);},generation);
    }
    void apply_theme(std::string const& selected) {
        window.Content().as<FrameworkElement>().RequestedTheme(selected=="light"?ElementTheme::Light:selected=="dark"?ElementTheme::Dark:ElementTheme::Default);
    }
    void build_settings() {
        title("nav.settings","settings.subtitle");
        language=ComboBox();language.Header(box_value(tr("settings.language")));accessible(language,tr("settings.language"));language.HorizontalAlignment(HorizontalAlignment::Stretch);
        language_ids.clear(); language.Items().Append(box_value(tr("settings.system")));language_ids.push_back("system");
        int selected=0;
        auto languages=localization.languages();
        for(auto const& item:languages) {
            if(!item.is_object())continue; auto locale=item.value("locale",std::string{}); if(locale.empty())continue;
            language.Items().Append(box_value(to_hstring(item.value("name",locale))));language_ids.push_back(locale);
            if(locale==settings.value("locale",std::string("system"))) selected=static_cast<int>(language_ids.size()-1);
        }
        language.SelectedIndex(selected);content.Children().Append(language);
        language.SelectionChanged([self=shared_from_this()](auto const&,auto const&){
            int index=self->language.SelectedIndex();if(index<0||size_t(index)>=self->language_ids.size())return;
            auto id=self->language_ids[size_t(index)];
            self->settings["locale"]=id;
            if(self->offline_switch)self->settings["offline"]=self->offline_switch.IsOn();
            if(self->lookup_switch)self->settings["hash_lookup_enabled"]=self->lookup_switch.IsOn();
            if(self->workers&&std::isfinite(self->workers.Value()))self->settings["scan_workers"]=static_cast<int>(self->workers.Value());
            if(self->max_file&&std::isfinite(self->max_file.Value()))self->settings["max_file_bytes"]=static_cast<uint64_t>(self->max_file.Value()*1048576);
            self->localization.set_locale(id=="system"?"":id);self->rebuild_navigation();self->show_page("settings",false);
        });
        theme=ComboBox();theme.Header(box_value(tr("settings.theme")));accessible(theme,tr("settings.theme"));theme.HorizontalAlignment(HorizontalAlignment::Stretch);
        for(auto const* key:{"settings.system","settings.light","settings.dark"})theme.Items().Append(box_value(tr(key)));
        auto current=settings.value("theme",std::string("system"));theme.SelectedIndex(current=="light"?1:current=="dark"?2:0);content.Children().Append(theme);
        theme.SelectionChanged([self=shared_from_this()](auto const&,auto const&){auto selected=self->theme.SelectedIndex();self->settings["theme"]=selected==1?"light":selected==2?"dark":"system";self->apply_theme(self->settings["theme"].get<std::string>());});
        content.Children().Append(label("settings.restart"));
        content.Children().Append(button("ui.save",[self=shared_from_this()]{self->rpc("settings.set",{{"locale",self->settings.value("locale",std::string("system"))},{"theme",self->settings.value("theme",std::string("system"))}},[self](json const& result){self->settings=result;self->message("ui.saved",L"",InfoBarSeverity::Success);},self->generation);},true));
        content.Children().Append(label("services.notice"));
        auto states=StackPanel();content.Children().Append(states);
        rpc("services.get",json::object(),[self=shared_from_this(),states](json const& data){self->render_json(states,data);},generation);
        auto controls=row();
        for(int kind:{1,2})for(bool running:{true,false})controls.Children().Append(button(std::string(kind==1?"services.av.":"services.sandbox.")+(running?"start":"stop"),[self=shared_from_this(),kind,running]{self->rpc("services.set",{{"kind",kind},{"running",running}},[self](auto const&){self->show_page("settings");});}));
        content.Children().Append(controls);
        content.Children().Append(label("services.installNotice"));auto registration=StackPanel();registration.Spacing(8);content.Children().Append(registration);
        rpc("services.get",json::object(),[self=shared_from_this(),registration](const json& data){
            bool installed=false,all=true;for(const auto& s:data.at("items")){installed|=s.value("installed",false);all&=s.value("installed",false);}
            if(!all)registration.Children().Append(self->button("services.install",[self]{self->manage_services(false);}));
            if(installed){registration.Children().Append(self->button("services.update",[self]{self->manage_services(false);}));registration.Children().Append(self->button("services.uninstall",[self]{self->confirm_remove_services();}));}
        },generation);
        content.Children().Append(label("hash.keyTitle",20));content.Children().Append(label("hash.keyNotice"));
        HyperlinkButton authentication;authentication.Content(box_value(L"MalwareBazaar / Auth-Key"));authentication.NavigateUri(Uri(L"https://auth.abuse.ch/"));content.Children().Append(authentication);
        provider_key=PasswordBox();provider_key.Header(box_value(tr("hash.key")));accessible(provider_key,tr("hash.key"));provider_key.MaxLength(512);content.Children().Append(provider_key);
        auto keyState=label("ui.loading");content.Children().Append(keyState);
        rpc("reputation.key.status",json::object(),[self=shared_from_this(),keyState](const json& data){keyState.Text(self->tr(data.value("configured",false)?"hash.keyPresent":"hash.keyAbsent"));},generation);
        auto keys=row();keys.Children().Append(button("hash.keySave",[self=shared_from_this()]{auto key=to_string(self->provider_key.Password());self->provider_key.Password(L"");self->rpc("reputation.key.save",{{"key",key}},[self](const json&){self->show_page("settings");});if(!key.empty())SecureZeroMemory(key.data(),key.size());}));
        keys.Children().Append(button("hash.keyRemove",[self=shared_from_this()]{self->rpc("reputation.key.remove",json::object(),[self](const json&){self->show_page("settings");});}));content.Children().Append(keys);
        content.Children().Append(label("settings.limits"));content.Children().Append(label("settings.about",12));
        content.Children().Append(label("deployment.title",20));content.Children().Append(label("deployment.notice"));
        auto prerequisites=label("ui.loading");content.Children().Append(prerequisites);
        rpc("deployment.prerequisites",json::object(),[self=shared_from_this(),prerequisites](const json& data){prerequisites.Text(self->tr(data.value("antivirusReady",false)?"deployment.avReady":"deployment.avUnavailable")+L"\n"+self->tr(data.value("sandboxReady",false)?"deployment.sandboxReady":"deployment.sandboxUnavailable"));},generation);
        auto versionState=label("ui.loading");content.Children().Append(versionState);
        rpc("deployment.status",json::object(),[self=shared_from_this(),versionState](const json& data){self->deployment_status=data;versionState.Text(data.value("available",false)?self->tr("deployment.verified")+L" "+to_hstring(data.value("version",std::string())):self->tr("deployment.unavailable"));},generation);
        update_source=input("deployment.source");content.Children().Append(update_source);auto updateActions=row();
        updateActions.Children().Append(button("deployment.choose",[self=shared_from_this()]{const auto path=self->choose_path(true);if(!path.empty())self->update_source.Text(to_hstring(path));}));
        updateActions.Children().Append(button("deployment.apply",[self=shared_from_this()]{self->deployment_operation=json::object();self->rpc("deployment.update",{{"source",self->string(self->update_source)}},[self](const json& result){self->deployment_operation=result;self->deployment_operation["action"]="update";self->message("ui.saved",self->tr("deployment.restart"),InfoBarSeverity::Success);},self->generation);}));
        updateActions.Children().Append(button("deployment.rollback",[self=shared_from_this()]{self->deployment_operation=json::object();self->rpc("deployment.rollback",json::object(),[self](const json& result){self->deployment_operation=result;self->deployment_operation["action"]="rollback";self->message("ui.saved",self->tr("deployment.restart"),InfoBarSeverity::Success);},self->generation);}));content.Children().Append(updateActions);
        content.Children().Append(label("deployment.definitionsNotice"));definition_source=input("deployment.definitionsSource");content.Children().Append(definition_source);auto definitionActions=row();
        definitionActions.Children().Append(button("deployment.chooseDefinitions",[self=shared_from_this()]{const auto path=self->choose_path(true);if(!path.empty())self->definition_source.Text(to_hstring(path));}));
        definitionActions.Children().Append(button("deployment.definitionsApply",[self=shared_from_this()]{self->maintain("definitions",self->string(self->definition_source));}));
        definitionActions.Children().Append(button("deployment.definitionsRollback",[self=shared_from_this()]{self->maintain("definitions.rollback",{});}));definitionActions.Children().Append(button("deployment.servicesRollback",[self=shared_from_this()]{self->maintain("rollback",{});}));content.Children().Append(definitionActions);
    }
    fire_and_forget maintain(std::string action,std::string source){auto self=shared_from_this();if(managing_services)co_return;managing_services=true;const auto owner=hwnd();const auto queue=dispatcher;std::string failure;co_await resume_background();try{self->backend->Maintain(owner,action,std::filesystem::path(av::services::Wide(source)));}catch(const std::exception& e){failure=e.what();}queue.TryEnqueue([self,failure]{self->managing_services=false;if(self->closed)return;if(failure.empty())self->message("ui.saved",self->tr("services.changed"),InfoBarSeverity::Success);else self->message("ui.error",diagnostic_text(failure),InfoBarSeverity::Error);});}
    json last_hash=json::object();
    fire_and_forget confirm_remove_services(){auto self=shared_from_this();if(dialog_open)co_return;dialog_open=true;try{ContentDialog dialog;dialog.XamlRoot(content.XamlRoot());dialog.Title(box_value(tr("services.uninstall")));dialog.Content(box_value(tr("services.removeNotice")));dialog.PrimaryButtonText(tr("services.uninstall"));dialog.CloseButtonText(tr("ui.cancel"));auto result=co_await dialog.ShowAsync();dialog_open=false;if(result==ContentDialogResult::Primary)manage_services(true);}catch(const hresult_error& e){dialog_open=false;message("ui.error",e.message(),InfoBarSeverity::Error);}}
    fire_and_forget manage_services(bool remove){
        auto self=shared_from_this();if(managing_services)co_return;managing_services=true;const auto owner=hwnd();const auto queue=dispatcher;
        message("ui.loading",tr("services.installNotice"));std::string failure;
        co_await resume_background();try{self->backend->ManageServices(owner,remove);}catch(const av::services::WindowsError& e){failure=e.code==ERROR_CANCELLED?"cancelled":e.what();}catch(const std::exception& e){failure=e.what();}
        queue.TryEnqueue([self,failure]{self->managing_services=false;if(self->closed)return;self->show_page("settings");if(!failure.empty())self->message("ui.error",failure=="cancelled"?self->tr("services.cancelled"):diagnostic_text(failure),InfoBarSeverity::Warning);else self->message("ui.saved",self->tr("services.changed"),InfoBarSeverity::Success);});
    }
    void update_caption(){
        if(!caption)return;const auto bar=window.AppWindow().TitleBar();const auto scale=double(GetDpiForWindow(hwnd()))/96.;
        caption.Padding(Thickness{16,0,std::max(140.,bar.RightInset()/scale),0});
        if(Windows::UI::ViewManagement::AccessibilitySettings().HighContrast()){bar.ButtonForegroundColor(nullptr);bar.ButtonBackgroundColor(nullptr);bar.ButtonInactiveBackgroundColor(nullptr);return;}
        const auto transparent=Windows::UI::Color{0,0,0,0};bar.ButtonBackgroundColor(transparent);bar.ButtonInactiveBackgroundColor(transparent);
        const bool dark=window.Content().as<FrameworkElement>().ActualTheme()==ElementTheme::Dark;
        bar.ButtonForegroundColor(dark?Windows::UI::Color{255,255,255,255}:Windows::UI::Color{255,0,0,0});
    }
    void show_page(std::string selected, bool reload_settings=true) {
        if(closed)return;page=std::move(selected);++generation;polling=false;session_polling=false;session_summary=nullptr;session_results=nullptr;
        commandActions.clear();content.Children().Clear();info.IsOpen(false); scroller.ChangeView(nullptr,box_value(0.0).as<IReference<double>>(),nullptr);
        try {
            if(page=="dashboard")build_dashboard();else if(page=="scan")build_scan();else if(page=="realtime")build_realtime();
            else if(page=="sandbox"||page=="vm"||page=="behavior")build_session(page);
            else if(page=="quarantine")build_quarantine();else if(page=="history")build_history();else if(page=="settings") {
                if(!reload_settings)build_settings();
                else {content.Children().Append(label("ui.loading"));rpc("settings.get",json::object(),[self=shared_from_this()](json const& data){self->settings=data;self->content.Children().Clear();self->build_settings();if(smoke_requested)diagnostics("settings loaded locale="+self->localization.locale()+" controls="+std::to_string(self->content.Children().Size()));},generation);}
            }
        } catch(hresult_error const& e){message("ui.error",e.message(),InfoBarSeverity::Error);}catch(std::exception const& e){message("ui.error",diagnostic_text(e.what()),InfoBarSeverity::Error);}
    }
    void rebuild_navigation() {
        rebuilding=true;navigation.MenuItems().Clear();
        const std::pair<const char*,const wchar_t*> items[]={{"dashboard",L"\uE80F"},{"scan",L"\uE721"},{"realtime",L"\uE72E"},{"sandbox",L"\uE7F4"},{"vm",L"\uE7F8"},{"behavior",L"\uE9D9"},{"quarantine",L"\uE74C"},{"history",L"\uE81C"},{"settings",L"\uE713"}};
        for(auto const& [id,glyph]:items) {
            NavigationViewItem item;item.Content(box_value(tr(std::string("nav.")+id)));item.Tag(box_value(to_hstring(id)));
            Microsoft::UI::Xaml::Input::KeyboardAccelerator shortcut;shortcut.Key(static_cast<Windows::System::VirtualKey>(int(Windows::System::VirtualKey::Number1)+int(navigation.MenuItems().Size())));shortcut.Modifiers(Windows::System::VirtualKeyModifiers::Menu);shortcut.Invoked([self=shared_from_this(),id](auto const&,auto const& e){for(auto value:self->navigation.MenuItems()){auto item=value.as<NavigationViewItem>();if(to_string(unbox_value<hstring>(item.Tag()))==id){self->navigation.SelectedItem(item);break;}}e.Handled(true);});item.KeyboardAccelerators().Append(shortcut);
            FontIcon icon;icon.Glyph(glyph);item.Icon(icon);accessible(item,tr(std::string("nav.")+id));navigation.MenuItems().Append(item);
            if(page==id)navigation.SelectedItem(item);
        }
        rebuilding=false;
    }
    void initialize() {
        if(smoke_requested){json p=json::object();if(!smoke_locale.empty())p["locale"]=smoke_locale;if(!smoke_theme.empty())p["theme"]=smoke_theme;if(!p.empty())backend->Request("settings.set",p);}
        window=Window();window.Title(L"Aegis");
        try {window.SystemBackdrop(MicaBackdrop());}catch(...){}
        dispatcher=window.DispatcherQueue();
        Grid outer;auto captionRow=RowDefinition();captionRow.Height(GridLength{40,GridUnitType::Pixel});outer.RowDefinitions().Append(captionRow);outer.RowDefinitions().Append(RowDefinition());auto status_row=RowDefinition();status_row.Height(GridLength{0,GridUnitType::Auto});outer.RowDefinitions().Append(status_row);
        caption=Grid();caption.Background(SolidColorBrush(Windows::UI::Color{0,0,0,0}));auto brand=text(L"Aegis",14);brand.VerticalAlignment(VerticalAlignment::Center);caption.Children().Append(brand);outer.Children().Append(caption);
        navigation=NavigationView();navigation.IsSettingsVisible(false);navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);navigation.PaneTitle(L"Aegis");navigation.OpenPaneLength(252);navigation.CompactModeThresholdWidth(600);navigation.ExpandedModeThresholdWidth(1050);
        Grid body;body.Padding(Thickness{24,24,24,16});auto info_row=RowDefinition();info_row.Height(GridLength{0,GridUnitType::Auto});body.RowDefinitions().Append(info_row);body.RowDefinitions().Append(RowDefinition());
        info=InfoBar();info.Margin(Thickness{0,0,0,16});info.IsClosable(true);body.Children().Append(info);
        scroller=ScrollViewer();scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);Grid::SetRow(scroller,1);
        content=StackPanel();content.Spacing(16);content.MaxWidth(1100);content.HorizontalAlignment(HorizontalAlignment::Stretch);content.Padding(Thickness{0,0,8,24});
        Media::Animation::TransitionCollection transitions;transitions.Append(Media::Animation::EntranceThemeTransition());content.ChildrenTransitions(transitions);
        scroller.Content(content);body.Children().Append(scroller);navigation.Content(body);Grid::SetRow(navigation,1);outer.Children().Append(navigation);
        connection=label("ui.loading",12);connection.Margin(Thickness{18,6,18,10});Grid::SetRow(connection,2);outer.Children().Append(connection);window.Content(outer);
        window.ExtendsContentIntoTitleBar(true);window.SetTitleBar(caption);update_caption();
        caption.SizeChanged([self=shared_from_this()](auto const&,auto const&){self->update_caption();});outer.ActualThemeChanged([self=shared_from_this()](auto const&,auto const&){self->update_caption();});
        const DWORD border=DWMWA_COLOR_NONE;DwmSetWindowAttribute(hwnd(),DWMWA_BORDER_COLOR,&border,sizeof(border));
        if(smoke_requested)diagnostics(window.ExtendsContentIntoTitleBar()?"PASS: integrated native title bar":"FAIL: integrated native title bar");
        rebuild_navigation();
        navigation.SelectionChanged([self=shared_from_this()](auto const&,NavigationViewSelectionChangedEventArgs const& args){if(self->rebuilding)return;auto item=args.SelectedItem().try_as<NavigationViewItem>();if(item)self->show_page(to_string(unbox_value<hstring>(item.Tag())));});
        window.Closed([self=shared_from_this()](auto const&,auto const&){self->closed=true;self->backend->Close();if(self->timer)self->timer.Stop();});
        auto refresh=button("ui.refresh",[self=shared_from_this()]{self->show_page(self->page);});navigation.PaneFooter(refresh);
        window.AppWindow().Resize(Windows::Graphics::SizeInt32{smoke_requested?smoke_width:1200,850});
        timer=dispatcher.CreateTimer();timer.Interval(std::chrono::seconds(1));timer.Tick([weak=weak_from_this()](auto const&,auto const&){if(auto self=weak.lock()){
            ++self->ticks;if(self->ticks%5==0)self->poll_events();if(self->page=="scan")self->poll_scan();if(self->page=="sandbox"||self->page=="vm"||self->page=="behavior")self->poll_session();
            if(distribution_smoke){
                if(self->ticks>60){diagnostics("FAIL: distribution UI timeout");application_exit_code=1;self->window.Close();return;}
                if(self->deployment_status.empty())return;
                if(self->deployment_scenario==0){
                    if(!self->deployment_status.value("available",false)||!self->commandActions.contains("deployment.apply")||!self->commandActions.contains("deployment.servicesRollback")||!self->commandActions.contains("deployment.definitionsApply")){diagnostics("FAIL: distribution status or controls");application_exit_code=1;self->window.Close();return;}
                    diagnostics("PASS: verified package, prerequisites and maintenance controls");wchar_t source[32768]{};if(GetEnvironmentVariableW(L"AEGIS_UI_TEST_UPDATE",source,32768)){self->update_source.Text(source);self->commandActions.at("deployment.apply")();self->deployment_scenario=1;return;}
                    diagnostics("PASS: phase 18 UI");self->window.Close();return;
                }
                if(self->deployment_scenario==1&&self->deployment_operation.value("action",std::string())=="update"){const auto state=self->backend->Request("deployment.status",json::object());if(state.value("active",std::string())=="base"){diagnostics("FAIL: UI update did not activate");application_exit_code=1;self->window.Close();return;}diagnostics("PASS: UI signed update command");self->commandActions.at("deployment.rollback")();self->deployment_scenario=2;return;}
                if(self->deployment_scenario==2&&self->deployment_operation.value("action",std::string())=="rollback"){const auto state=self->backend->Request("deployment.status",json::object());if(state.value("active",std::string())!="base"){diagnostics("FAIL: UI rollback selection");application_exit_code=1;}else diagnostics("PASS: UI rollback command");diagnostics("PASS: phase 18 UI");self->window.Close();}return;
            }
            if(smoke_requested&&self->ticks==2){const auto style=GetWindowLongPtrW(self->hwnd(),GWL_STYLE);if(!self->window.ExtendsContentIntoTitleBar()||self->caption.ActualHeight()<32||(style&WS_THICKFRAME)==0||(style&WS_MAXIMIZEBOX)==0){diagnostics("FAIL: native resizing or integrated caption layout");application_exit_code=1;}else diagnostics("PASS: integrated caption layout and native resize/maximize styles");}
            if(smoke_requested&&self->ticks%2==1&&(self->page=="dashboard"||self->page=="settings"))self->capture_smoke_image(self->page);
            if(smoke_requested&&self->ticks%2==0){
                if(self->page=="scan"&&self->scenario==0){
                    const auto fixture=diagnostics_path.parent_path()/L"ui-fixture.txt";std::ofstream(fixture)<<"Inert UI flow fixture.";self->scan_path.Text(fixture.wstring());self->scenario=1;self->commandActions.at("scan.custom")();return;
                }
                if(self->page=="scan"&&self->scenario==1){
                    if(self->last_data.value("running",true))return;
                    if(self->last_data.value("completed",0)!=1||!self->last_data.value("complete",false)){diagnostics("FAIL: UI scan result");application_exit_code=1;}else diagnostics("PASS: UI scan command and progress");
                    self->hash_input.Text(L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");self->hash_online.IsOn(false);self->commandActions.at("hash.query")();self->scenario=5;return;
                }
                if(self->page=="scan"&&self->scenario==5){if(self->last_hash.empty())return;if(self->last_hash.value("online",true)||self->last_hash.at("items").size()!=2){diagnostics("FAIL: offline UI hash query");application_exit_code=1;}self->scenario=2;}
                if(self->page=="settings"&&self->scenario==2){
                    if(!self->language||!self->theme||!self->commandActions.contains("ui.save"))return;
                    self->language.SelectedIndex(1);self->theme.SelectedIndex(2);self->commandActions.at("ui.save")();self->scenario=3;return;
                }
                if(self->page=="settings"&&self->scenario==3){
                    if(!self->commandActions.contains("hash.keySave")||!self->provider_key||!self->commandActions.contains("services.uninstall")||!self->commandActions.contains("services.update")){diagnostics("FAIL: key or service controls");application_exit_code=1;}else diagnostics("PASS: protected key and service management controls present; no elevation invoked");
                    const auto saved=self->backend->Request("settings.get",json::object());
                    if(saved.value("locale",std::string())!="en-US"||saved.value("theme",std::string())!="dark"||self->localization.locale()!="en-US"){diagnostics("FAIL: language/theme save");application_exit_code=1;}else diagnostics("PASS: language/theme preview and save");self->scenario=4;
                }
                diagnostics("rendered "+self->localization.locale()+" "+self->page+" children="+std::to_string(self->content.Children().Size())+" width="+std::to_string(self->content.ActualWidth()));
                static const char* pages[]={"scan","realtime","sandbox","vm","behavior","quarantine","history","settings"};
                if(self->content.Children().Size()==0||self->content.ActualWidth()<=0)application_exit_code=1;
                auto first=Microsoft::UI::Xaml::Input::FocusManager::FindFirstFocusableElement(self->scroller).try_as<Control>();
                if(!first||!first.Focus(FocusState::Keyboard)){diagnostics("FAIL: keyboard focus "+self->page);application_exit_code=1;}else diagnostics("focus "+self->page);
                diagnostics("dpi="+std::to_string(GetDpiForWindow(self->hwnd())));
                if(self->smoke_step<std::size(pages))self->show_page(pages[self->smoke_step++]);
                else {diagnostics(application_exit_code==0?"PASS: all nine WinUI pages rendered":"FAIL: page rendering");self->timer.Stop();self->window.Close();}
            }
        }});timer.Start();
        show_page(distribution_smoke?"settings":"dashboard");window.Activate();
        rpc("settings.get",json::object(),[self=shared_from_this()](json const& data){
            self->settings=data;if(smoke_requested){if(!smoke_locale.empty())self->settings["locale"]=smoke_locale;if(!smoke_theme.empty())self->settings["theme"]=smoke_theme;}auto locale=self->settings.value("locale",std::string("system"));self->localization.set_locale(locale=="system"?"":locale);self->apply_theme(self->settings.value("theme",std::string("system")));self->rebuild_navigation();self->show_page(self->page);
        });
    }
};

struct App : ApplicationT<App, Markup::IXamlMetadataProvider> {
    std::shared_ptr<Shell> shell;
    XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
    App(){
        UnhandledException([](auto const&,UnhandledExceptionEventArgs const& error){diagnostics("Unhandled WinUI exception: "+to_string(error.Message()));application_exit_code=1;error.Handled(true);Application::Current().Exit();});
    }
    Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type){return provider.GetXamlType(type);}
    Markup::IXamlType GetXamlType(hstring const& name){return provider.GetXamlType(name);}
    com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions(){return provider.GetXmlnsDefinitions();}
    void OnLaunched(LaunchActivatedEventArgs const&) {
        try {Resources().MergedDictionaries().Append(XamlControlsResources());shell=std::make_shared<Shell>();shell->initialize();diagnostics("Window activated");}
        catch(hresult_error const& e){diagnostics("Startup failure: "+to_string(e.message()));application_exit_code=1;Application::Current().Exit();}
        catch(std::exception const& e){diagnostics(std::string("Startup failure: ")+e.what());application_exit_code=1;Application::Current().Exit();}
    }
};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    try {
        wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);diagnostics_path=std::filesystem::path(temporary)/L"Aegis-UI.log";
        smoke_requested=command_line&&std::wstring_view(command_line).find(L"--smoke-test")!=std::wstring_view::npos;
        distribution_smoke=smoke_requested&&std::wstring_view(command_line).find(L"--phase18-test")!=std::wstring_view::npos;
        if(smoke_requested){
            wchar_t setting[32768]{};
            if(GetEnvironmentVariableW(L"AEGIS_UI_TEST_OUTPUT",setting,32768)){std::filesystem::create_directories(setting);diagnostics_path=std::filesystem::path(setting)/L"ui.log";}
            if(GetEnvironmentVariableW(L"AEGIS_UI_TEST_LOCALE",setting,32768))smoke_locale=to_string(setting);
            if(GetEnvironmentVariableW(L"AEGIS_UI_TEST_THEME",setting,32768))smoke_theme=to_string(setting);
            if(GetEnvironmentVariableW(L"AEGIS_UI_TEST_WIDTH",setting,32768))smoke_width=std::clamp(_wtoi(setting),600,1600);
        }
        diagnostics(smoke_requested?"Starting UI smoke test":"Starting Aegis");
        init_apartment(apartment_type::single_threaded);
        Application::Start([](auto&&){make<App>();});
        return application_exit_code;
    } catch(hresult_error const& e){if(smoke_requested){diagnostics(to_string(e.message()));return 1;}MessageBoxW(nullptr,e.message().c_str(),L"Aegis — startup error",MB_OK|MB_ICONERROR);return static_cast<int>(e.code());}
    catch(std::exception const& e){if(smoke_requested){diagnostics(e.what());return 1;}MessageBoxW(nullptr,diagnostic_text(e.what()).c_str(),L"Aegis — startup error",MB_OK|MB_ICONERROR);return 1;}
}
