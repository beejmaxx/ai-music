extern "C" int ai_music_test_cpu_import(const char* directory);
int main(int argc, char** argv) {
  return argc == 2 ? ai_music_test_cpu_import(argv[1]) : 1;
}
