std::vector<uint8_t> Broker::CaptureUser(){
    namespace c=guardian::checkpoint;auto& s=*state_;
    state::Require(s.user32&&!s.gdi&&!s.finished&&!s.complete&&s.failure.empty(),"Only the live USER32 protocol supports this checkpoint");
    const auto admin=[&](uint32_t op,uint32_t offset=0){u::Packet p;p.boot=s.boot;p.session=s.session;p.command.op=static_cast<u::Op>(op);p.command.flags=offset;s.Transfer(p);state::Require(!p.reply.status,"USER32 checkpoint is not quiescent/supported");return p;};
    admin(c::Begin);c::User user{};auto* raw=reinterpret_cast<uint8_t*>(&user);
    for(uint32_t at=0;at<sizeof(user);at+=512){const auto p=admin(c::Read,at);std::memcpy(raw+at,p.reply.text,(std::min)(size_t(512),sizeof(user)-at));}
    state::Require(c::Valid(user)&&user.boot==s.boot&&user.session==s.session&&user.sequence==s.sequence,"USER32 snapshot identity rejected");
    state::Writer w;w.Pod(user);w.Pod(uint32_t(s.events.size()));for(const auto& e:s.events)w.Text(e);return std::move(w.data);
}
bool Broker::CloseCheckpoint(){auto& s=*state_;if(!s.user32||s.gdi)return false;u::Packet p;p.boot=s.boot;p.session=s.session;p.sequence=s.sequence+1;p.pid=p.tid=1;p.command.op=u::Op::Finish;p.command.flags=1;return Exchange(p)&&Completed();}
void Broker::RestoreUser(std::span<const uint8_t> bytes){
    namespace c=guardian::checkpoint;auto& s=*state_;state::Require(s.user32&&!s.gdi&&!s.sequence&&!s.attempted,"USER32 restore requires a fresh broker");
    state::Reader r(bytes);const auto user=r.Pod<c::User>();state::Require(c::Valid(user)&&user.boot==s.boot&&user.session==s.session,"USER32 restore identity rejected");
    const auto count=r.Pod<uint32_t>();state::Require(count==user.sequence&&count<=u::MaxCommands,"USER32 checkpoint history rejected");std::vector<std::string> events;for(uint32_t i=0;i<count;++i){auto e=r.Text();state::Require(e.size()<4096,"USER32 event history quota");events.push_back(std::move(e));}r.End();
    auto admin=[&](uint32_t op,uint32_t at=0){u::Packet p;p.boot=s.boot;p.session=s.session;p.command.op=static_cast<u::Op>(op);p.command.flags=at;if(op==c::Write)std::memcpy(p.command.text,reinterpret_cast<const uint8_t*>(&user)+at,(std::min)(size_t(512),sizeof(user)-at));s.Transfer(p);state::Require(!p.reply.status,"USER32 checkpoint restore rejected");};
    admin(c::Load);for(uint32_t at=0;at<sizeof(user);at+=512)admin(c::Write,at);admin(c::Commit);
    s.sequence=user.sequence;s.attempted=s.sequence!=0;s.events=std::move(events);s.last.restrictions=31;s.last.helperPid=s.pid;s.last.live[0]=user.windows;s.last.live[1]=0;s.last.live[2]=user.events;std::copy(std::begin(user.calls),std::end(user.calls),s.last.calls);
}
