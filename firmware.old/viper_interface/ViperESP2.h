#ifndef ViperESP2_h
#define ViperESP2_h
#include "Arduino.h"

class ViperESP2 {
  public:
    ViperESP2(HardwareSerial& serial);
    void begin();
    void update(); // Processes the listen/callback logic
    void sniff();  // Raw hex dumper for reverse engineering
    
    void lock();
    void unlock();
    void remoteStart();
    
    void onMessage(void (*function)(uint8_t*, int));

  private:
    HardwareSerial& _vSerial;
    void (*_callback)(uint8_t*, int);
    uint8_t _buffer[5];
    int _pos = 0;
};
#endif
