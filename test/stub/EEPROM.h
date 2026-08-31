#pragma once
#include "Arduino.h"
struct EEPROMClass {
  unsigned char read(int) { return 0; }
  void update(int, unsigned char) {}
  template<typename T> void put(int, const T&) {}
  template<typename T> void get(int, T&) {}
};
extern EEPROMClass EEPROM;
