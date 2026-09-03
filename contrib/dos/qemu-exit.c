/*  Halt the QEMU guest through the isa-debug-exit device.  */
#include <pc.h>

int main(void) {
  outportb(0xf4, 0);
  return 0;
}
