#pragma once
#include "Arduino.h"
#include "SoftwareSerial.h"
struct HUSKYLENSResult { int ID; int xCenter; int yCenter; };
struct HUSKYLENS {
  bool begin(SoftwareSerial&) { return true; }
  bool request() { return true; }
  int  available() { return 0; }
  HUSKYLENSResult read() { return HUSKYLENSResult(); }
};
