// RHMongooseOS.h
//
// Routines for implementing RadioHead on Mongoose OS
// Contributed by Juha Yrjölä and used with permission

#ifndef RH_MONGOOSE_OS_h
#define RH_MONGOOSE_OS_h

#include <mgos.h>

typedef unsigned char byte;

#ifndef NULL
  #define NULL 0
#endif

class SerialSimulator
{
  public:
    #define DEC 10
    #define HEX 16
    #define OCT 8
    #define BIN 2

    // TODO: move these from being inlined
    static void begin(int baud);
    static size_t println(const char* s);
    static size_t print(const char* s);
    static size_t print(unsigned int n, int base = DEC);
    static size_t print(char ch);
    static size_t println(char ch);
    static size_t print(unsigned char ch, int base = DEC);
    static size_t println(unsigned char ch, int base = DEC);
};

extern SerialSimulator Serial;

long random(long min, long max);

#endif
