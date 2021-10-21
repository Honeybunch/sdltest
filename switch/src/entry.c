#include <stdint.h>

extern int32_t SDL_main(int32_t argc, char *argv[]);

#include "twili.h"

int32_t main(int32_t argc, char *argv[]) {
  // TODO: Only if a debug build...
  twiliInitialize();
  int32_t ret = SDL_main(argc, argv);
  twiliExit();
  return ret;
}