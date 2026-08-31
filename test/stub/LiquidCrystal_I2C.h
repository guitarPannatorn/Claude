#pragma once
#include "Arduino.h"
struct LiquidCrystal_I2C {
  LiquidCrystal_I2C(int,int,int) {}
  void init() {} void backlight() {} void clear() {}
  void setCursor(int,int) {}
  void print(const char*) {}
};
