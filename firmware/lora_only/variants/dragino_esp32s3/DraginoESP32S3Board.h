#pragma once

#include <helpers/ESP32Board.h>

class DraginoESP32S3Board : public ESP32Board {
public:
  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0;
  }
};
