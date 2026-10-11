// Low Glow: a keyboard-led A-minor groove, with short string replies.
#include "music/bass_voice.hpp"
#include "music/kick_voice.hpp"
#include "music/effects.hpp"
#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {
constexpr double bpm=122, step=60/bpm/4;
constexpr unsigned bars=56;
std::uint64_t at(double position) {return std::llround(position*step*music::sample_rate);}
struct Note {unsigned step,midi;double length,velocity;};
struct Event {std::uint64_t frame;unsigned kind,midi;double velocity;};
constexpr unsigned roots[]={33,33,38,38,41,41,40,40};
constexpr unsigned answers[][3]={{67,64,69},{69,66,64},{67,64,60},{68,65,64}};
}

int main(int argc,char** argv) {
  try {
    if(argc!=2) throw std::runtime_error("Usage: music-low-glow NEW_OUTPUT_PREFIX");
    const std::string prefix=argv[1];
    constexpr const char* suffixes[]={".wav","-bass.wav","-kick.wav","-percussion.wav","-strings.wav"};
    for(const auto suffix:suffixes)
      if(std::filesystem::exists(prefix+suffix)) throw std::runtime_error("Output already exists");
    if(std::filesystem::exists(prefix+"-sections.txt")) throw std::runtime_error("Section notes already exist");
    music::BassVoice bass,strings;
    music::BassPatch bp;
    bp.sub=0;bp.cutoff=245;bp.filter_amount=2.7;bp.filter_decay_ms=175;
    bp.decay_ms=170;bp.sustain=.58;bp.release_ms=34;bp.glide_ms=22;
    bp.drive_db=4;bp.level=.65;bass.patch(bp);
    music::BassPatch sp;
    sp.oscillator=music::BassPatch::Oscillator::pulse;sp.pulse_width=.42;sp.sub=0;
    sp.cutoff=850;sp.resonance=.7;sp.filter_amount=1.8;sp.filter_decay_ms=235;
    sp.attack_ms=5;sp.decay_ms=500;sp.sustain=.15;sp.release_ms=145;
    sp.drive_db=1;sp.level=.39;strings.patch(sp);
    music::KickVoice kick;music::KickPatch kp;
    kp.pitch=41.20344461;kp.decay_ms=195;kp.click=.18;kp.level=.69;kick.patch(kp);
    auto kit=music::make_synth_source();kit->tempo(float(bpm));
    for(const auto layer:{"kick","clap","hats","bass","lead","pad"})kit->mix(layer,0);
    music::Effects echo;echo.tempo(float(bpm));echo.delay(.17f);echo.filter(5700);
    std::vector<Event> events;
    auto note=[&](unsigned bar,Note n,bool lead,double level=1.) {
      const auto onset=bar*16+n.step+(n.step%2?(lead?.08:.15):0.);
      events.push_back({at(onset),lead?5u:4u,n.midi,n.velocity*level});
      events.push_back({at(onset+n.length),lead?1u:0u,0,0});
    };
    for(unsigned bar=0;bar<=bars;++bar) {
      events.push_back({at(bar*16),2,bar,0});
      if(bar==bars)continue;
      for(unsigned beat=0;beat<4;++beat) {
        if(bar>=24 && bar<28)continue;
        if((bar<4 || (bar>=28 && bar<32)) && beat%2)continue;
        if(((bar==23 || bar==31 || bar==47) && beat==3) || (bar==55 && beat!=0))continue;
        events.push_back({at(bar*16+beat*4),3,0,beat==0?.94:beat==2?.88:.81});
      }
      if(bar==55) {
        note(bar,{0,33,3.,.83},false);note(bar,{0,69,3.,.65},true);continue;
      }
      const auto local=bar%8,root=roots[local],third=root+(local<2?3:4),next=roots[(local+1)%8];
      if(bar<4 || (bar>=24 && bar<28)) {
        note(bar,{0,root,bar<4?1.8:5.,.76},false);
        if(bar<4)note(bar,{8,root,1.3,.67},false);
      } else {
        const auto pattern=bar%2==0
          ?std::vector<Note>{{0,root,1.25,.88},{3,root+7,.55,.59},{6,root,.9,.75},
                            {8,root,1.3,.82},{11,third,.7,.64},{14,root+7,.7,.68}}
          :std::vector<Note>{{0,root,1.2,.88},{4,third,.65,.61},{7,root+12,.55,.68},
                            {10,root+7,1.,.76},{13,root,.65,.69},{15,next-1,.45,.52}};
        for(auto n:pattern) {
          if(bar==31 && n.step>=13)continue;
          if(bar>=52 && n.step>10)continue;
          note(bar,n,false,bar>=28 && bar<32?.85:1.);
        }
      }
      if((bar>=12 && bar<20) || (bar>=32 && bar<48)) {
        if(bar%2) {
          for(unsigned n=0;n<3;++n) {
            auto pitch=answers[local/2][n];
            if(bar>=40 && n==2)pitch=local==1?72:local==3?71:local==5?69:68;
            note(bar,{n==0?10u:n==1?13u:15u,pitch,.65,n==2?.76:.66},true);
          }
        }
      } else if(bar>=28 && bar<32) {
        note(bar,{10,answers[local/2][2],2.2,.55},true);
      } else if(bar>=48 && bar<52 && bar%2) {
        note(bar,{10,answers[local/2][2],1.8,.56},true);
      }
    }
    std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b) {
      return a.frame==b.frame?a.kind<b.kind:a.frame<b.frame;
    });
    std::array<std::unique_ptr<music::Recorder>,5> files;
    for(unsigned i=0;i<files.size();++i)files[i]=std::make_unique<music::Recorder>(prefix+suffixes[i]);
    std::array<std::array<music::StereoFrame,music::block_size>,5> output{};
    std::array<float,music::block_size> kl{},kr{},unused_l{},unused_r{};
    const auto total=at(bars*16)+3*music::sample_rate,pause=at(32*16-2),return_at=at(32*16);
    std::size_t next_event=0;double kick_age=10,peak=0,kit_gain=0,target_kit=0;
    auto section=[&](unsigned bar) {
      const bool quiet=bar>=24 && bar<28;
      const bool tail=bar>=55;
      bp.cutoff=quiet?135:bar>=28 && bar<32?180+25*(bar-28):bar>=32 && bar<48?270:230;
      bass.patch(bp);
      sp.cutoff=bar>=40 && bar<48?1100:850;strings.patch(sp);
      kit->mix("hats",tail || quiet?0:bar<4?.10f:bar>=20 && bar<24?.16f:.22f);
      kit->mix("clap",tail || quiet || bar<4?0:bar>=28 && bar<32?.14f:.29f);
      kit->synth_control({music::Parameter::rhythm,bar==23 || bar==47?1.f:0.f});
      target_kit=bar<2 || bar>=bars?0:1;
    };
    for(std::uint64_t frame=0;frame<total;) {
      while(next_event<events.size() && events[next_event].frame==frame) {
        const auto e=events[next_event++];
        switch(e.kind) {
          case 0:bass.note_off();break;case 1:strings.note_off();break;
          case 2:section(e.midi);break;case 3:kick.trigger(e.velocity);kick_age=0;break;
          case 4:bass.note_on(e.midi,e.velocity);break;case 5:strings.note_on(e.midi,e.velocity);break;
        }
      }
      auto count=std::min<std::uint64_t>(music::block_size,total-frame);
      if(next_event<events.size())count=std::min(count,events[next_event].frame-frame);
      if(!count)throw std::runtime_error("Track clock did not advance");
      kit->read_buses(unused_l.data(),unused_r.data(),kl.data(),kr.data(),count);echo.begin_block();
      for(std::size_t i=0;i<count;++i,++frame) {
        kit_gain+=(target_kit-kit_gain)/960;
        double fade=std::min(1.,double(total-frame)/(.5*music::sample_rate));
        if(frame>=pause-576 && frame<pause)fade*=double(pause-frame)/576;
        else if(frame>=pause && frame<return_at)fade=0;
        else if(frame>=return_at && frame<return_at+96)fade*=double(frame-return_at)/96;
        const float b=bass.render()*(1-.3*std::exp(-kick_age/.035))*1.3*fade;
        const float k=kick.render()*1.3*fade,p=kl[i]*kit_gain*1.1*fade;
        const float h=strings.render()*1.2;const auto wet=echo.process(h,h*.72f);
        const auto duck=float((1-.12*std::exp(-kick_age/.055))*fade);
        output[1][i]={b,b};output[2][i]={k,k};output[3][i]={p,p};output[4][i]={wet.left*duck,wet.right*duck};
        output[0][i]={b+k+p+output[4][i].left,b+k+p+output[4][i].right};
        for(const auto& bus:output) {
          const auto s=bus[i];
          if(!std::isfinite(s.left) || !std::isfinite(s.right) || std::max(std::abs(s.left),std::abs(s.right))>=.98)
            throw std::runtime_error("Invalid or clipping output");
        }
        peak=std::max({peak,double(std::abs(output[0][i].left)),double(std::abs(output[0][i].right))});
        kick_age+=1./music::sample_rate;
      }
      for(unsigned i=0;i<files.size();++i)files[i]->push(output[i].data(),count);
    }
    for(auto& file:files) {file->finish();if(file->failed() || file->dropped())throw std::runtime_error("Recording failed");}
    std::ofstream notes(prefix+"-sections.txt");
    notes<<"Low Glow\n122 BPM / A minor / Am7-D9-Fmaj7-E7 / 56 bars\n";
    for(auto [bar,label]:std::vector<std::pair<unsigned,const char*>>{{0,"Opening"},{4,"Keyboard groove"},
        {12,"Strings answer"},{20,"Keys and bass"},{24,"Quiet middle"},{28,"Rebuild"},
        {32,"Full return"},{40,"Higher replies"},{48,"Outro"},{55,"Last A minor hit"}})
      notes<<bar*16*step<<"s  "<<label<<'\n';
    notes.close();if(!notes)throw std::runtime_error("Could not save cues");
    std::cout<<"Low Glow: "<<total<<" frames, "<<double(total)/music::sample_rate<<"s; peak "<<20*std::log10(peak)<<" dBFS\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
