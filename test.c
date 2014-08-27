#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include "ftfp.h"

void p(fixed f) {
  char buf[40];

  fix_print(buf, f);
  printf("n: %s\n", buf);
}

int main(int argc, char** argv) {

  fixed f = FIXINT(15);
  fixed g = FIXINT(1);
  fixed h;

  p(f);
  p(g);

  f = FIXINT(1 << 13);
  g = FIXINT(1 << 13);
  p(f);

  fix_add(f,g, &h);

  p(h);

  f = FIXINT( (1 << 14) -1 );
  p(f);
}
