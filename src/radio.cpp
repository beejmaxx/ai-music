#include "music/radio.hpp"
#include "music/commands.hpp"
#include <sstream>

namespace music {
void RadioDirector::program(const std::string& name) {
  if (name != "trance" && name != "french-house") throw std::runtime_error("Program must be trance or french-house");
  french_house_ = name == "french-house";
}

Score RadioDirector::next_house() {
  // Original funk/house materials: the bass and chord rhythm carry the hook.
  constexpr const char* titles[] = {"Midnight circuit", "Chrome steps", "Daybreak"};
  constexpr const char* harmony[] = {"0 0 3 3 6 6 2 2", "0 0 5 5 3 3 6 6", "5 5 6 6 0 0 2 2"};
  constexpr const char* bass[] = {
    "0 - 0 2 - 3 1 - 0 - 2 4 - 1 0 -",
    "0 - 0 2 - 1 3 - 0 - 2 - 4 1 0 2",
    "0 - - 0 - 2 1 - 3 - 0 4 - 1 0 -",
    "0 - 2 - 0 3 1 - 0 - 2 4 - 3 1 0",
  };
  constexpr const char* hooks[] = {
    "- - 2 ~ - - 3 - - - 2 ~ - 1 - -",
    "- - 3 - - 2 ~ - - - 4 ~ - 3 - -",
    "- - 2 - - - 3 ~ - - 4 - 3 ~ - -",
  };
  constexpr const char* chops[] = {
    "0 0 3 0 0 1 2 0 0 0 3 0 0 2 0 1",
    "0 0 3 0 0 0 2 0 0 1 3 0 0 2 0 0",
    "0 0 3 0 0 2 0 1 0 0 3 0 0 0 2 0",
  };
  const auto theme = chapter_index_++ % 3;
  chapter_ = titles[theme];
  std::ostringstream text;
  text << "quantize 4\nat 0 style house\nat 0 drums on\nat 0 voice pluck\nat 0 rhythm steady"
       << "\nat 0 chord-voice keys\nat 0 chord-bars 1\nat 0 harmony " << harmony[theme]
       << "\nat 0 chords " << chops[theme]
       << "\nramp 0 2 mix kick .86\nramp 0 2 mix clap .3\nramp 0 2 mix hats .19"
       << "\nramp 0 2 mix bass .68\nramp 0 2 mix pad .46\nramp 0 2 mix lead .2"
       << "\nramp 0 4 filter 5000\nramp 0 2 delay .14"
       << "\nramp 16 4 mix lead .3\nramp 16 4 mix hats .26\nramp 24 4 filter 6500"
       << "\nat 32 mix kick 0\nat 32 mix clap 0\nramp 32 2 mix bass .24\nramp 32 4 filter 1000"
       << "\nramp 32 4 delay .3\nramp 40 8 filter 8000\nramp 40 6 mix hats .3"
       << "\nramp 40 7 mix clap .35\nat 47 mix hats 0\nat 47 mix clap 0"
       << "\nat 48 mix kick .9\nat 48 mix bass .7\nat 48 mix clap .33\nat 48 mix hats .25"
       << "\nat 48 delay .13\nramp 52 4 mix lead .2\nramp 56 7 filter 5200";
  for (unsigned bar = 0; bar < 64; ++bar) {
    text << "\nat " << bar << " bassnotes " << bass[(theme + (bar % 4 == 3 ? 1 : 0) + bar / 16) % 4];
    if (bar % 4 == 0) text << "\nat " << bar << " melody " << hooks[(theme + (bar / 16) % 2) % 3];
    if (bar % 8 == 0) text << "\nat " << bar << " chords " << chops[(theme + bar / 16) % 3];
  }
  const auto commands = parse_commands(text.str());
  validate_controls(commands, false, true, true);
  return compile_score(commands);
}

Score RadioDirector::next() {
  if (french_house_) return next_house();
  // Four-bar themes: question, answer, lift, cadence. Develop recognizable ideas.
  constexpr int themes[4][4][16] = {
    {{3,-1,2,-1,3,4,5,-1,4,-1,3,-1,2,-1,1,2},
     {3,-1,2,3,5,-1,4,-1,3,-1,2,-1,0,-1,2,-1},
     {5,-1,4,-1,3,2,3,-1,4,-1,5,-1,6,-1,5,4},
     {3,-1,2,-1,1,-1,0,-1,2,-1,3,2,1,-1,0,-1}},
    {{0,-1,3,2,-1,3,0,-1,2,-1,3,5,-1,3,2,-1},
     {0,-1,3,2,-1,4,3,-1,2,-1,0,2,-1,3,2,-1},
     {3,-1,5,4,-1,5,3,-1,4,-1,5,6,-1,5,4,-1},
     {3,-1,2,0,-1,2,3,-1,2,-1,1,0,-1,-1,2,-1}},
    {{2,3,-1,2,4,-1,3,-1,2,3,-1,5,4,-1,3,-1},
     {2,3,-1,4,5,-1,4,-1,3,2,-1,0,2,-1,3,-1},
     {5,6,-1,5,4,-1,3,-1,4,5,-1,6,7,-1,6,-1},
     {5,4,-1,3,2,-1,0,-1,2,-1,3,-1,2,-1,0,-1}},
    {{3,-1,-1,2,-1,-1,4,-1,3,-1,-1,5,-1,-1,2,-1},
     {3,-1,-1,4,-1,-1,5,-1,4,-1,-1,3,-1,-1,0,-1},
     {5,-1,-1,4,-1,-1,3,-1,5,-1,-1,6,-1,-1,5,-1},
     {4,-1,-1,3,-1,-1,2,-1,1,-1,-1,0,-1,-1,2,-1}},
  };
  constexpr const char* names[] = {"Glass orbit", "Night current", "Open horizon", "Inner light"};
  constexpr const char* chords[] = {"0 5 2 6 0 3 5 4", "0 3 5 6 0 4 3 6", "0 2 5 6 3 5 0 4", "0 5 3 4 0 2 6 4"};
  constexpr const char* bass[] = {
    "- 0 0 0 - 0 1 0 - 0 0 2 - 0 1 0",
    "- - 0 0 - 1 0 - - - 0 2 - 0 1 -",
    "- 0 2 0 - 0 1 0 - 0 2 0 - 1 0 1",
    "- 0 - 0 - 0 1 - - 0 - 2 - 1 0 -",
  };
  constexpr unsigned order[] = {0, 1, 2, 0, 3, 1, 2, 3};
  const auto theme = order[chapter_index_++ % std::size(order)];
  chapter_ = names[theme];
  const auto voice = theme == 0 || theme == 3 ? "pluck" : "wide";
  const float lead = theme == 3 ? .4f : .48f;
  const float cutoff = theme == 1 ? 6000 : 8500;
  std::ostringstream text;
  text << "quantize 8\nat 0 style trance\nat 0 drums on\nat 0 chords sustain\nat 0 chord-voice pad\nat 0 chord-bars 4\nat 0 rhythm steady\nat 0 voice " << voice
       << "\nat 0 harmony " << chords[theme]
       << "\nramp 0 2 mix kick .85\nramp 0 4 mix clap .27\nramp 0 4 mix hats .18"
       << "\nramp 0 4 mix bass .58\nramp 0 4 mix lead " << lead
       << "\nramp 0 4 mix pad .34\nramp 0 4 filter " << cutoff << "\nramp 0 4 delay .28"
       << "\nat 16 rhythm drive\nramp 16 4 mix hats .28\nramp 16 4 mix lead " << lead + .06f
       << "\nramp 24 8 mix pad .5\nramp 28 4 mix lead .25"
       << "\nramp 32 1 mix kick 0\nramp 32 2 mix bass 0\nramp 32 2 mix clap 0"
       << "\nramp 32 2 mix hats 0\nat 32 voice soft\nramp 32 2 mix pad .65"
       << "\nramp 32 4 filter 2600\nramp 32 4 delay .48\nat 40 rhythm build\nat 40 voice " << voice
       << "\nramp 40 8 filter 11000\nramp 40 7 mix clap .44\nramp 40 7 mix hats .28"
       << "\nramp 40 8 mix lead .6\nat 47 mix clap 0\nat 47 mix hats 0"
       << "\nat 48 rhythm drive\nat 48 voice wide\nat 48 mix kick .9\nat 48 mix bass .65"
       << "\nat 48 mix clap .34\nat 48 mix hats .3\nat 48 delay .23\nramp 48 4 mix pad .34"
       << "\nramp 52 4 mix lead " << lead << "\nramp 56 8 filter " << cutoff;
  for (unsigned bar = 0; bar < 64; ++bar) {
    const unsigned phrase = bar % 4;
    const bool breakdown = bar >= 32 && bar < 40;
    const bool answer = bar >= 16 && bar < 32;
    text << "\nat " << bar << " melody";
    for (unsigned step = 0; step < 16; ++step) {
      auto note = themes[theme][answer ? (phrase + 2) % 4 : phrase][step];
      if (breakdown && step % 4) note = -1;
      if (bar >= 48 && note >= 0 && note <= 4 && step % 4 == 0) note += 3;
      if (note < 0) text << " -"; else text << ' ' << note;
    }
    if (bar % 8 == 0) text << "\nat " << bar << " bassnotes " << bass[(theme + bar / 16) % 4];
  }
  if (random_() % 2) text << "\nat 56 voice " << voice;
  const auto commands = parse_commands(text.str());
  validate_controls(commands, false, true, true);
  return compile_score(commands);
}
}  // namespace music
