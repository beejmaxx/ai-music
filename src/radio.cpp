#include "music/radio.hpp"
#include "music/commands.hpp"
#include <sstream>

namespace music {
Score RadioDirector::next() {
  struct Mood { const char* name; bool trance; float bpm, lead, pad, cutoff; };
  constexpr Mood journey[] = {
    {"Progressive opening", true, 132, .38f, .35f, 6200},
    {"Rolling trance", true, 134, .48f, .3f, 8000},
    {"Wide horizon", true, 136, .54f, .45f, 11000},
    {"Afterglow", true, 132, .34f, .5f, 5000},
    {"Deep house", false, 126, .27f, .38f, 3800},
    {"Night drive", false, 128, .35f, .32f, 6000},
    {"Lift", true, 130, .4f, .4f, 7500},
    {"Open sky", true, 134, .5f, .45f, 9500},
  };
  const auto& mood = journey[chapter_index_++ % std::size(journey)];
  chapter_ = mood.name;
  // Compose a four-step motif, answer it, then lift it an octave. Degrees follow
  // the live chord progression, so new phrases remain harmonically compatible.
  const int a = int(random_() % 3), b = int(random_() % 3);
  int motif[] = {a, 2, b + 3, 2, a + 3, b, 2, 3,
                 a, 2, b + 3, 4, a + 3, 5, b + 3, 2};
  std::ostringstream text;
  text << "quantize 8\nat 0 style " << (mood.trance ? "trance" : "house")
       << "\nat 0 drums on\nramp 0 8 tempo " << mood.bpm
       << "\nramp 0 2 mix kick .85\nramp 0 4 mix clap .3\nramp 0 4 mix hats .25"
       << "\nramp 0 4 mix bass .58\nramp 0 4 mix lead " << mood.lead
       << "\nramp 0 4 mix pad " << mood.pad
       << "\nramp 0 4 filter " << mood.cutoff << "\nramp 0 4 delay .28\nat 0 melody";
  for (unsigned i = 0; i < 16; ++i) {
    if (!mood.trance && i % 2) text << " -";
    else text << ' ' << motif[i];
  }
  text << "\nat 0 bassline";
  for (unsigned i = 0; i < 16; ++i)
    text << ' ' << (mood.trance ? i % 4 != 0 : i % 4 == 2 || (i % 4 == 3 && random_() % 2));
  // Eight-bar groove, four-bar breakdown, four-bar build, sixteen-bar release.
  text << "\nramp 8 1 mix kick 0\nramp 8 2 mix bass 0\nramp 8 2 mix clap 0"
       << "\nramp 8 2 mix hats .08\nramp 8 4 filter 900\nramp 8 2 mix pad .62"
       << "\nramp 8 4 delay .48\nramp 12 4 filter " << mood.cutoff * 1.25f
       << "\nramp 12 4 mix hats .38\nramp 12 4 mix lead " << mood.lead + .1f
       << "\nramp 12 3 mix clap .45\nat 15 mix clap 0\nat 15 mix hats 0"
       << "\nat 16 mix kick .9\nat 16 mix bass .65\nat 16 mix clap .34\nat 16 mix hats .3"
       << "\nat 16 delay .22\nramp 16 2 mix pad " << mood.pad
       << "\nramp 16 4 mix lead " << mood.lead << "\nramp 20 4 filter " << mood.cutoff
       << "\nat 24 melody";
  for (unsigned i = 0; i < 16; ++i) {
    if (!mood.trance && i % 2) text << " -";
    else text << ' ' << motif[(i + 4) % 16];
  }
  const auto commands = parse_commands(text.str());
  validate_controls(commands, false, true, true);
  return compile_score(commands);
}
}  // namespace music
