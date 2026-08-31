#pragma once
#include <string>
#include <cstdio>
#include <cstdint>
#include <cstring>

typedef unsigned char byte;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A6 20
#define A7 21
#define F(x) (x)
#define HEX 16

void pinMode(int, int);
void digitalWrite(int, int);
int  digitalRead(int);
int  analogRead(int);
void delay(unsigned long);
unsigned long millis();

struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c) {}
  String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); s = b; }
  String(unsigned char v, int) { char b[24]; snprintf(b, sizeof(b), "%x", v); s = b; }
  void trim() {}
  size_t length() const { return s.size(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  String substring(int i) const { return String(s.substr(i).c_str()); }
  String substring(int from, int to) const {
    if (from < 0 || (size_t)from > s.size() || to < from) return String("");
    return String(s.substr(from, to - from).c_str());
  }
  int indexOf(const char* needle) const {
    size_t k = s.find(needle);
    return k == std::string::npos ? -1 : (int)k;
  }
  int indexOf(char c, int from) const {
    size_t k = s.find(c, from);
    return k == std::string::npos ? -1 : (int)k;
  }
  int toInt() const { return atoi(s.c_str()); }
  const char* c_str() const { return s.c_str(); }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(const char* c) { s += c; return *this; }
  String& operator=(const char* c) { s = c; return *this; }
  bool operator==(const char* c) const { return s == c; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator!=(const char* c) const { return !(s == c); }
  bool operator!=(const String& o) const { return s != o.s; }
};
inline String operator+(const String& a, const String& b) { return String((a.s + b.s).c_str()); }

struct SerialClass {
  void begin(long) {}
  void print(const char*) {}
  void print(int) {}
  void print(unsigned long) {}
  void print(float, int) {}
  void print(const String&) {}
  void print(unsigned char, int) {}
  void println() {}
  void println(const char*) {}
  void println(int) {}
  void println(unsigned long) {}
  void println(bool) {}
  void println(const String&) {}
};
extern SerialClass Serial;
