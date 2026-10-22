#include "../src/microvm/gui/Backend.h"
#include "../src/microvm/state/Files.h"
#include <iostream>
using namespace av::microvm;
using namespace guardian::graphics;
int main(int argc,char** argv) {
    try {
        state::Require(argc==2,"Output directory required");
        const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
        uint32_t checks=0;
        const auto check=[&](bool ok){++checks;state::Require(ok,"Session core check failed");};
        const auto rejects=[&](auto operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected);};
        check(guardian::session::ValidDuration(100,100));check(guardian::session::ValidDuration(604800000,30000));
        check(!guardian::session::ValidDuration(604800001,100));check(!guardian::session::ValidDuration(99,100));
        for(const auto& text:{"-1 key 65", "1 key 999", "1 click 320 0 1", "1 click 0 0 2", "1 key 65 host", "2 key 65\n1 close", "1 hostwindow", "604800001 close", "1 close host"}) {
            state::AtomicText(root/"rejected.txt",text);rejects([&]{gui::ReadScript(root/"rejected.txt");});
        }
        state::AtomicText(root/"valid.txt","# guest milliseconds\n100 key 13\n200 click 40 65 1\n200 blur\n300 close\n");
        gui::Options o;o.continuous=true;o.script=gui::ReadScript(root/"valid.txt");check(o.script.size()==4);
        auto mixed=o;mixed.input.push_back({Input::Close});rejects([&]{gui::Backend invalid(11,mixed);});
        gui::Backend a(11,o);Packet p;p.boot=11;p.op=Op::Poll;p.sequence=1;
        check(a.Exchange(p,nullptr,99999999) && p.status==Status::Empty);
        p=Packet{};p.boot=11;p.op=Op::Poll;p.sequence=2;
        check(a.Exchange(p,nullptr,100000000) && p.event.type==Input::Key && p.event.key==13);
        state::Writer saved;a.Save(saved);state::Reader reader(saved.data);gui::Backend restored(11,o);restored.Load(reader);reader.End();
        p=Packet{};p.boot=11;p.op=Op::Poll;p.sequence=3;
        check(restored.Exchange(p,nullptr,199999999) && p.status==Status::Empty);
        p=Packet{};p.boot=11;p.op=Op::Poll;p.sequence=4;
        check(restored.Exchange(p,nullptr,200000000) && p.event.type==Input::Button);
        p=Packet{};p.boot=11;p.op=Op::Poll;p.sequence=5;
        check(restored.Exchange(p,nullptr,200000000) && p.event.type==Input::Blur);
        p=Packet{};p.boot=11;p.op=Op::Poll;p.sequence=6;
        rejects([&]{restored.Exchange(p,nullptr,199999999);});
        auto different=o;different.script[0].event.key=27;gui::Backend changed(11,different);state::Reader mismatch(saved.data);rejects([&]{changed.Load(mismatch);});
        // Manual snapshots redraw only this viewer and discard host focus on attach.
        gui::Options manual;manual.continuous=true;manual.interactive=true;
        uint32_t displays=0;manual.display=[&](const FrameInfo&,const uint32_t*){++displays;};manual.poll=[] {return Event{Input::Key,0,0,0,65};};
        gui::Backend first(12,manual);state::Writer snapshot;first.Save(snapshot);
        gui::Backend second(12,manual);state::Reader source(snapshot.data);second.Load(source);source.End();check(displays==1);
        p=Packet{};p.boot=12;p.op=Op::Poll;p.sequence=1;check(second.Exchange(p,nullptr,100) && p.event.type==Input::Blur);
        p=Packet{};p.boot=12;p.op=Op::Poll;p.sequence=2;check(second.Exchange(p,nullptr,101) && p.event.type==Input::Key);
        state::AtomicText(root/"core.json","{\"status\":\"PASS\",\"checks\":"+std::to_string(checks)+"}\n");
        std::cout<<checks<<" session duration, timing, script isolation, rollback, snapshot and manual reattach checks PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
