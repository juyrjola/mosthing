// RHMongooseOS.cpp

// Contributed by Juha Yrjölä and used with permission

#include <RadioHead.h>
#include <Print.h>
#include "RHMongooseOS.h"


void SerialSimulator::begin(int baud)
{
  (void) baud;
}

size_t SerialSimulator::println(const char* s)
{
  size_t ret;

  ret = print(s);
  print("\n");
  return ret + 1;
}

size_t SerialSimulator::print(const char* s)
{
  return printf("%s", s);
}

size_t SerialSimulator::print(unsigned int n, int base)
{
  if (base == DEC)
    return printf("%d", n);
  else if (base == HEX)
    return printf("%02x", n);
  else if (base == OCT)
    return printf("%o", n);
  // TODO: BIN
  return 0;
}

size_t SerialSimulator::print(char ch)
{
  printf("%c", ch);
  return 1;
}

size_t SerialSimulator::println(char ch)
{
  printf("%c\n", ch);
  return 2;
}

size_t SerialSimulator::print(unsigned char ch, int base)
{
  return print((unsigned int)ch, base);
}

size_t SerialSimulator::println(unsigned char ch, int base)
{
  size_t ret;

  ret = print((unsigned int)ch, base);
  printf("\n");
  return ret + 1;
}

long random(long min, long max)
{
  return min + rand() % (max - min);
}
