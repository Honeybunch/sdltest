#include <stdint.h>

#include <switch.h>
#include <unistd.h>

extern int32_t SDL_main(int32_t argc, char *argv[]);

int32_t main(int32_t argc, char *argv[]) {
  int32_t nxlink_socket = -1;

  romfsInit();
  // TODO: Only if a debug build...
  socketInitializeDefault();
  nxlink_socket = nxlinkStdio();

  int32_t ret = SDL_main(argc, argv);

  if (nxlink_socket != -1) {
    close(nxlink_socket);
  }
  socketExit();
  romfsExit();

  return ret;
}