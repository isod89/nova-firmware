#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
uint32_t millis();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
inline void *ps_malloc(size_t n) { return malloc(n); }
inline long random(long n) { return n > 0 ? rand() % n : 0; }
using std::abs;
#include <cstdlib>

// Minimal Serial for the protocol test: RX fed by the harness, TX to stdout
#include <stdarg.h>
#include <deque>
struct SimSerial
{
  std::deque<char> rx;
  int available() { return (int)rx.size(); }
  int read() { if(rx.empty()) return -1; char c = rx.front(); rx.pop_front(); return (uint8_t)c; }
  void feed(const char *s) { while(*s) rx.push_back(*s++); }
  int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
  { va_list ap; va_start(ap, fmt); int n = vfprintf(stdout, fmt, ap); va_end(ap); return n; }
  void print(const char *s) { fputs(s, stdout); }
  void print(char c) { fputc(c, stdout); }
  void println(const char *s) { fputs(s, stdout); fputc(10, stdout); }
  void println() { fputc(10, stdout); }
  explicit operator bool() const { return true; }
};
extern SimSerial Serial;
