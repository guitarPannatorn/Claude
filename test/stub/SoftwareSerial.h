#pragma once
#include "Arduino.h"
struct SoftwareSerial {
  SoftwareSerial(int,int) {}
  void begin(long) {}
  bool listen() { return true; }
  int available() { return 0; }
  int read() { return -1; }
  void print(const char*) {}
  void print(int) {}
  void print(unsigned long) {}
  void print(float,int) {}
  void print(const String&) {}
  void println(const char*) {}
};
