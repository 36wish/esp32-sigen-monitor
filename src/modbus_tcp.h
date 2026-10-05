#pragma once

#include <WiFi.h>

// Minimal Modbus TCP client: MBAP header + read/write registers. While waiting
// for a reply it keeps LVGL running, so touch and redraws stay live.

// MB_EXCEPTION means the device answered but refused the request; MB_FAIL means
// no usable answer (timeout, dropped link, garbled reply, or a reply whose
// transaction ID, unit ID, protocol ID or length doesn't match the request).
enum ModbusResult { MB_OK, MB_EXCEPTION, MB_FAIL };

static const uint8_t  FC_READ_HOLDING   = 0x03;
static const uint8_t  FC_READ_INPUT     = 0x04;
static const uint32_t MODBUS_TIMEOUT_MS = 2000;

// FC_READ_HOLDING or FC_READ_INPUT; up to 64 registers.
ModbusResult modbusReadRegisters(WiFiClient &client, uint8_t slaveId, uint8_t funcCode,
                                 uint16_t startAddr, uint16_t qty, uint16_t *outRegs,
                                 uint32_t timeoutMs = MODBUS_TIMEOUT_MS);

// FC 0x06 write single register.
ModbusResult modbusWriteRegister(WiFiClient &client, uint8_t slaveId, uint16_t addr,
                                 uint16_t value, uint32_t timeoutMs = MODBUS_TIMEOUT_MS);

// FC 0x10 write a U32 (two registers, high word first).
ModbusResult modbusWriteU32(WiFiClient &client, uint8_t slaveId, uint16_t addr,
                            uint32_t value, uint32_t timeoutMs = MODBUS_TIMEOUT_MS);

// A 32-bit value held in two registers, high word first.
inline uint32_t toU32(const uint16_t *regs) {
  return ((uint32_t)regs[0] << 16) | regs[1];
}

inline int32_t toS32(const uint16_t *regs) {
  return (int32_t)toU32(regs);
}

// A 64-bit value held in four registers, high word first.
inline uint64_t toU64(const uint16_t *regs) {
  return ((uint64_t)toU32(regs) << 32) | toU32(regs + 2);
}
