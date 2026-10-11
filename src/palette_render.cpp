// Render an original composition from a small text score using the saved palette.
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
#include <sstream>
#include <vector>

namespace {
struct Event {
  double position=0; unsigned kind=0, note=0, ticket=0;
  double value=0, second=0, third=0, fourth=0; unsigned rhythm=0;
  std::uint64_t frame=0;
};
struct Pause {double start,length;};
}

int main(int argc,char** argv) {
  try {
    if(argc!=3)throw std::runtime_error("Usage: music-palette-render SCORE.txt NEW_OUTPUT_PREFIX");
    std::ifstream score(argv[1]);if(!score)throw std::runtime_error("Cannot open score");
    double bpm=120,bars=64,tail=3,kick_tune=43.6535,delay=.20;
    std::vector<Event> events;std::vector<Pause> pauses;
    std::string line;unsigned line_number=0,ticket=0;
    while(std::getline(score,line)) {
      ++line_number;
      std::istringstream in(line);std::string name;
      if(!(in>>name) || name[0]=='#')continue;
      if(name=="tempo")in>>bpm;
      else if(name=="bars")in>>bars;
      else if(name=="tail")in>>tail;
      else if(name=="kick_tune")in>>kick_tune;
      else if(name=="echo")in>>delay;
      else if(name=="bass" || name=="strings") {
        double position,length,velocity;unsigned midi;
        in>>position>>midi>>length>>velocity;
        if(!in || !std::isfinite(position+length+velocity) || position<0 || length<=0 || midi>127 || velocity<=0 || velocity>1)
          throw std::runtime_error("Invalid note on score line "+std::to_string(line_number));
        const bool lead=name=="strings";
        const unsigned id=++ticket;
        events.push_back({position,lead?5u:4u,midi,id,velocity});
        events.push_back({position+length,lead?1u:0u,0,id});
      } else if(name=="kick") {
        double position,velocity;in>>position>>velocity;
        if(!in || !std::isfinite(position+velocity) || position<0 || velocity<=0 || velocity>1)
          throw std::runtime_error("Invalid kick");
        events.push_back({position,3,0,0,velocity});
      } else if(name=="color") {
        Event event;event.kind=2;
        in>>event.position>>event.value>>event.second>>event.third>>event.fourth>>event.rhythm;
        if(!in || !std::isfinite(event.position+event.value+event.second+event.third+event.fourth) || event.position<0 ||
           event.value<30 || event.second<30 || event.third<0 || event.third>1 || event.fourth<0 || event.fourth>1 || event.rhythm>3)
          throw std::runtime_error("Invalid palette color");
        events.push_back(event);
      } else if(name=="pause") {
        Pause p;in>>p.start>>p.length;
        if(!in || !std::isfinite(p.start+p.length) || p.start<0 || p.length<=0)throw std::runtime_error("Invalid pause");
        pauses.push_back(p);
      } else throw std::runtime_error("Unknown score command: "+name);
      if(!in)throw std::runtime_error("Incomplete score line "+std::to_string(line_number));
      std::string extra;if(in>>extra && extra[0]!='#')throw std::runtime_error("Extra score field on line "+std::to_string(line_number));
    }
    if(!std::isfinite(bpm+bars+tail+kick_tune+delay) || bpm<40 || bpm>220 || bars<1 || bars>256 ||
       tail<0 || tail>10 || kick_tune<20 || kick_tune>100 || delay<0 || delay>1)
      throw std::runtime_error("Invalid score header");
    const double step=60/bpm/4;
    const auto at=[&](double n){return std::uint64_t(std::llround(n*step*music::sample_rate));};
    const auto total=at(bars*16)+std::uint64_t(std::llround(tail*music::sample_rate));
    for(auto& event:events) {
      event.frame=at(event.position);
      if(event.frame>=total)throw std::runtime_error("Score event extends beyond recording");
    }
    for(const auto& p:pauses)if(at(p.start+p.length)>total)throw std::runtime_error("Pause beyond recording");
    std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.frame==b.frame?a.kind<b.kind:a.frame<b.frame;});
    const std::string prefix=argv[2];
    constexpr const char* suffixes[]={".wav","-bass.wav","-kick.wav","-percussion.wav","-strings.wav"};
    for(auto suffix:suffixes)if(std::filesystem::exists(prefix+suffix))throw std::runtime_error("Output already exists");
    music::BassVoice bass,strings;music::BassPatch bp,sp;
    bp.sub=0;bp.cutoff=240;bp.filter_amount=2.7;bp.filter_decay_ms=190;bp.decay_ms=180;
    bp.sustain=.62;bp.release_ms=40;bp.glide_ms=25;bp.drive_db=4;bp.level=.65;bass.patch(bp);
    sp.oscillator=music::BassPatch::Oscillator::pulse;sp.pulse_width=.42;sp.sub=0;sp.cutoff=900;
    sp.resonance=.7;sp.filter_amount=2;sp.filter_decay_ms=260;sp.attack_ms=4;sp.decay_ms=650;
    sp.sustain=.18;sp.release_ms=180;sp.drive_db=1;sp.level=.41;strings.patch(sp);
    music::KickVoice kick;music::KickPatch kp;kp.pitch=kick_tune;kp.decay_ms=225;kp.click=.20;kp.level=.7;kick.patch(kp);
    auto kit=music::make_synth_source();kit->tempo(float(bpm));
    for(const auto name:{"kick","clap","hats","bass","lead","pad"})kit->mix(name,0);
    std::array<float,music::block_size> kl{},kr{},unused_l{},unused_r{};
    // Settle the mix controls over a complete eight-bar pattern before writing
    // sample zero, so a score can start with drums without the initial kick leak.
    for(std::uint64_t n=0;n<at(128);) {
      const auto count=std::min<std::uint64_t>(music::block_size,at(128)-n);
      kit->read_buses(unused_l.data(),unused_r.data(),kl.data(),kr.data(),count);n+=count;
    }
    music::Effects echo;echo.tempo(float(bpm));echo.delay(float(delay));echo.filter(6000);
    std::array<std::unique_ptr<music::Recorder>,5> files;
    for(unsigned i=0;i<files.size();++i)files[i]=std::make_unique<music::Recorder>(prefix+suffixes[i]);
    std::array<std::array<music::StereoFrame,music::block_size>,5> output{};
    std::size_t next=0;unsigned bass_ticket=0,string_ticket=0;double kick_age=10,peak=0;
    for(std::uint64_t frame=0;frame<total;) {
      while(next<events.size() && events[next].frame==frame) {
        const auto& e=events[next++];
        switch(e.kind) {
          case 0:if(e.ticket==bass_ticket)bass.note_off();break;
          case 1:if(e.ticket==string_ticket)strings.note_off();break;
          case 2:bp.cutoff=e.value;sp.cutoff=e.second;bass.patch(bp);strings.patch(sp);
            kit->mix("hats",float(e.third));kit->mix("clap",float(e.fourth));
            kit->synth_control({music::Parameter::rhythm,float(e.rhythm)});break;
          case 3:kick.trigger(e.value);kick_age=0;break;
          case 4:bass.note_on(e.note,e.value);bass_ticket=e.ticket;break;
          case 5:strings.note_on(e.note,e.value);string_ticket=e.ticket;break;
        }
      }
      auto count=std::min<std::uint64_t>(music::block_size,total-frame);
      if(next<events.size())count=std::min(count,events[next].frame-frame);
      if(!count)throw std::runtime_error("Score clock stopped");
      kit->read_buses(unused_l.data(),unused_r.data(),kl.data(),kr.data(),count);echo.begin_block();
      for(std::size_t i=0;i<count;++i,++frame) {
        double fade=std::min(1.,double(total-frame)/(.5*music::sample_rate));
        for(const auto& p:pauses) {
          const auto a=at(p.start),b=at(p.start+p.length);
          if(frame<a && a-frame<576)fade*=double(a-frame)/576;
          else if(frame>=a && frame<b)fade=0;
          else if(frame>=b && frame-b<96)fade*=double(frame-b)/96;
        }
        const float b=bass.render()*(1-.3*std::exp(-kick_age/.035))*1.3*fade;
        const float k=kick.render()*1.3*fade,p=kl[i]*1.1*fade;
        const float h=strings.render()*1.2;const auto wet=echo.process(h,h*.72f);
        const auto duck=float((1-.12*std::exp(-kick_age/.055))*fade);
        output[1][i]={b,b};output[2][i]={k,k};output[3][i]={p,p};output[4][i]={wet.left*duck,wet.right*duck};
        output[0][i]={b+k+p+output[4][i].left,b+k+p+output[4][i].right};
        for(const auto& bus:output) {
          const auto s=bus[i];
          if(!std::isfinite(s.left) || !std::isfinite(s.right) || std::max(std::abs(s.left),std::abs(s.right))>=.98)
            throw std::runtime_error("Invalid or clipping palette output");
        }
        peak=std::max({peak,double(std::abs(output[0][i].left)),double(std::abs(output[0][i].right))});
        kick_age+=1./music::sample_rate;
      }
      for(unsigned i=0;i<files.size();++i)files[i]->push(output[i].data(),count);
    }
    for(auto& file:files){file->finish();if(file->failed() || file->dropped())throw std::runtime_error("Recording failed");}
    std::cout<<prefix<<": "<<double(total)/music::sample_rate<<"s, "<<bpm<<" BPM, peak "<<20*std::log10(peak)<<" dBFS\n";
  } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
