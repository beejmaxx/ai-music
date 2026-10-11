// A sixteen-bar second theme for Side Street, using the established instruments.
#include "music/bass_voice.hpp"
#include "music/effects.hpp"
#include "music/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace {
constexpr double step = 60. / 116 / 4;
std::uint64_t at(double steps) { return std::llround(steps * step * music::sample_rate); }
struct Note { unsigned step, midi; double length, velocity; };
struct Event { std::uint64_t frame; unsigned kind, midi; double velocity; };
constexpr unsigned roots[] = {37,37,39,39,41,41,41,41};
const std::array<std::vector<Note>,8> phrase{{
  {{2,68,2.2,.76},{7,65,1.2,.66},{11,63,2.0,.70}},
  {{2,65,1.2,.67},{6,68,2.1,.76},{12,72,2.0,.80}},
  {{2,70,2.0,.80},{7,67,1.2,.68},{11,65,2.0,.72}},
  {{2,65,1.2,.65},{7,67,1.2,.71},{11,70,2.0,.78}},
  {{2,72,2.0,.83},{7,68,1.0,.74},{11,65,2.0,.78}},
  {{5,63,1.0,.64},{10,65,2.5,.74}},
  {{3,68,1.0,.72},{7,67,1.0,.64},{11,65,2.0,.78}},
  {{2,63,1.0,.62},{7,60,2.3,.64},{13,65,1.0,.66}},
}};
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: music-second-theme NEW_OUTPUT_PREFIX");
    const std::string prefix = argv[1];
    constexpr const char* suffixes[] = {".wav","-bass.wav","-hook.wav"};
    for (const auto suffix : suffixes)
      if (std::filesystem::exists(prefix+suffix)) throw std::runtime_error("Output already exists");
    music::BassVoice bass, hook;
    music::BassPatch bp;
    bp.sub=0; bp.cutoff=260; bp.filter_amount=2.7; bp.filter_decay_ms=190;
    bp.decay_ms=180; bp.sustain=.62; bp.release_ms=40; bp.glide_ms=25;
    bp.drive_db=4; bp.level=.65; bass.patch(bp);
    music::BassPatch hp;
    hp.oscillator=music::BassPatch::Oscillator::pulse; hp.pulse_width=.42; hp.sub=0;
    hp.cutoff=900; hp.resonance=.7; hp.filter_amount=2; hp.filter_decay_ms=260;
    hp.attack_ms=4; hp.decay_ms=650; hp.sustain=.18; hp.release_ms=180;
    hp.drive_db=1; hp.level=.43; hook.patch(hp);
    music::Effects echo; echo.tempo(116); echo.delay(.24f); echo.filter(6100);
    std::vector<Event> events;
    auto note=[&](unsigned bar, Note n, bool lead) {
      const auto onset=bar*16+n.step+(n.step%2 ? (lead?.07:.12) : 0.);
      events.push_back({at(onset),lead?4u:3u,n.midi,n.velocity});
      events.push_back({at(onset+n.length),lead?1u:0u,0,0});
    };
    for (unsigned bar=0;bar<16;++bar) {
      const unsigned local=bar%8, root=roots[local];
      const unsigned third=root+(local<4?4:3), seventh=root-(local<2?1:2);
      const unsigned next=roots[(local+1)%8];
      if (bar%2==0) {
        for (auto n:std::vector<Note>{{2,root,1.1,.87},{5,root,.65,.56},
            {7,root+7,.65,.70},{10,root,1.2,.83},{13,third,.6,.63},{15,seventh,.5,.57}})
          note(bar,n,false);
      } else {
        for (auto n:std::vector<Note>{{2,root,1.1,.86},{5,third,.6,.60},
            {7,root,.8,.72},{10,root+7,1.1,.79},{13,third,.6,.61},
            {15,bar==15?41u:next-1,.5,.56}}) {
          if (bar==15 && n.step>12) continue;
          note(bar,n,false);
        }
      }
      for (auto n:phrase[local]) {
        if (bar>=8) {
          if (bar%2 || n.step!=phrase[local].front().step) continue;
          n.length=5.5; n.velocity*=.80;
        }
        note(bar,n,true);
      }
      for (unsigned beat=0;beat<4;++beat) {
        if (bar==15 && beat==3) continue;
        events.push_back({at(bar*16+beat*4),2,0,0});
      }
    }
    std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b) {
      return a.frame==b.frame?a.kind<b.kind:a.frame<b.frame;
    });
    std::array<std::unique_ptr<music::Recorder>,3> files;
    for (unsigned i=0;i<files.size();++i) files[i]=std::make_unique<music::Recorder>(prefix+suffixes[i]);
    std::array<std::array<music::StereoFrame,music::block_size>,3> output{};
    const auto music_end=at(256), total=music_end+music::sample_rate;
    std::size_t next_event=0;
    double kick_age=10,peak=0;
    for (std::uint64_t frame=0;frame<total;) {
      const auto count=std::min<std::uint64_t>(music::block_size,total-frame);
      echo.begin_block();
      for (std::size_t i=0;i<count;++i,++frame) {
        while (next_event<events.size() && events[next_event].frame==frame) {
          const auto e=events[next_event++];
          switch(e.kind) {
            case 0:bass.note_off();break;
            case 1:hook.note_off();break;
            case 2:kick_age=0;break;
            case 3:bass.note_on(e.midi,e.velocity);break;
            case 4:hook.note_on(e.midi,e.velocity);break;
          }
        }
        const auto fade=std::min(1.,double(total-frame)/(.1*music::sample_rate));
        const float b=bass.render()*(1-.3*std::exp(-kick_age/.035))*1.3*fade;
        const float h=hook.render()*1.2;
        const auto wet=echo.process(h,h*.72f);
        const auto duck=float((1-.12*std::exp(-kick_age/.055))*fade);
        output[1][i]={b,b}; output[2][i]={wet.left*duck,wet.right*duck};
        output[0][i]={b+output[2][i].left,b+output[2][i].right};
        for (const auto& bus:output) {
          const auto sample=bus[i];
          if (!std::isfinite(sample.left) || !std::isfinite(sample.right) ||
              std::max(std::abs(sample.left),std::abs(sample.right))>=.98)
            throw std::runtime_error("Invalid section output");
        }
        peak=std::max({peak,double(std::abs(output[0][i].left)),double(std::abs(output[0][i].right))});
        kick_age+=1./music::sample_rate;
      }
      for (unsigned i=0;i<files.size();++i) files[i]->push(output[i].data(),count);
    }
    for (auto& file:files) {
      file->finish();
      if (file->failed() || file->dropped()) throw std::runtime_error("Section recording failed");
    }
    std::cout<<"Side Street second theme:16 bars Db/Eb/Fm, "<<total<<" frames; peak "<<20*std::log10(peak)<<" dBFS\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
