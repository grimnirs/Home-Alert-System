#include <stdio.h>
#include "pico/stdlib.h"

int main()
{
  stdio_init_all();

  while (1)
  {
    printf("Sensor node running...\n");
    sleep_ms(1000);
  }
}