#include "modbus_tcp.h"

#include "display.h"  // waitTick()

static const uint8_t FC_WRITE_SINGLE   = 0x06;
static const uint8_t FC_WRITE_MULTIPLE = 0x10;
static const uint8_t EXCEPTION_FLAG    = 0x80;  // set on the function code of a refusal

static const uint8_t REQUEST_LEN     = 12;  // MBAP header (7) + function code + address + quantity/value
static const uint8_t REPLY_HEADER    = 8;   // MBAP header (7) + function code
static const uint8_t MAX_EXTRA_BYTES = 5;   // FC 0x10's byte count + one U32
static const size_t  MAX_READ_BYTES  = 128; // supports up to 64 registers
static const uint8_t WRITE_REPLY_PDU = 5;   // function code + address + value/count
static const uint8_t EXCEPTION_PDU   = 2;   // function code + exception code

static uint16_t lastTxId = 0;  // each request takes the next one

static bool waitForBytes(WiFiClient &client, int n, uint32_t start, uint32_t timeoutMs) {
  while (client.connected() && client.available() < n && millis() - start < timeoutMs) waitTick();
  return client.available() >= n;
}

// Sends one request PDU (function code + 4 bytes: address and quantity/value,
// then any extra bytes, e.g. FC 0x10's byte count and data) and reads the
// response header up to and including the function code. `replyPdu` is the
// length of a normal reply's PDU, which the header's length has to match.
static ModbusResult modbusRequest(WiFiClient &client, uint8_t slaveId, uint8_t funcCode,
                                  uint16_t addr, uint16_t arg, uint16_t replyPdu,
                                  uint32_t start, uint32_t timeoutMs,
                                  const uint8_t *extra = nullptr, uint8_t extraLen = 0) {
  if (extraLen > MAX_EXTRA_BYTES) return MB_FAIL;
  uint16_t txId = ++lastTxId;
  uint8_t req[REQUEST_LEN + MAX_EXTRA_BYTES] = {
    (uint8_t)(txId >> 8), (uint8_t)(txId & 0xFF),
    0, 0,                              // protocol id
    0, (uint8_t)(6 + extraLen),        // length: unitId + PDU
    slaveId,
    funcCode,
    (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
    (uint8_t)(arg >> 8), (uint8_t)(arg & 0xFF),
  };
  if (extraLen) memcpy(req + REQUEST_LEN, extra, extraLen);
  client.write(req, REQUEST_LEN + extraLen);

  if (!waitForBytes(client, REPLY_HEADER, start, timeoutMs)) return MB_FAIL;
  uint8_t hdr[REPLY_HEADER];
  client.readBytes(hdr, REPLY_HEADER);
  // a stale reply, or one from a device that isn't the slave we asked, doesn't count
  if (hdr[0] != req[0] || hdr[1] != req[1]) return MB_FAIL;  // transaction ID
  if (hdr[2] != 0 || hdr[3] != 0) return MB_FAIL;            // protocol ID: Modbus
  if (hdr[6] != slaveId) return MB_FAIL;                     // unit ID
  uint16_t mbapLen = (hdr[4] << 8) | hdr[5];                  // unit ID + PDU

  uint8_t replyFunc = hdr[REPLY_HEADER - 1];
  if (replyFunc == (funcCode | EXCEPTION_FLAG)) {
    if (mbapLen != 1 + EXCEPTION_PDU) return MB_FAIL;
    if (waitForBytes(client, 1, start, timeoutMs)) client.read(); // consume exception code
    return MB_EXCEPTION;
  }
  return replyFunc == funcCode && mbapLen == 1 + replyPdu ? MB_OK : MB_FAIL;
}

ModbusResult modbusReadRegisters(WiFiClient &client, uint8_t slaveId, uint8_t funcCode,
                                 uint16_t startAddr, uint16_t qty, uint16_t *outRegs,
                                 uint32_t timeoutMs) {
  if (qty * 2 > MAX_READ_BYTES) return MB_FAIL;

  uint32_t start = millis();
  // reply PDU: function code + byte count + the registers
  ModbusResult r = modbusRequest(client, slaveId, funcCode, startAddr, qty, 2 + qty * 2, start, timeoutMs);
  if (r != MB_OK) return r;

  if (!waitForBytes(client, 1, start, timeoutMs)) return MB_FAIL;
  uint8_t byteCount = client.read();
  if (byteCount != qty * 2) return MB_FAIL;
  if (!waitForBytes(client, byteCount, start, timeoutMs)) return MB_FAIL;

  uint8_t data[MAX_READ_BYTES];
  client.readBytes(data, byteCount);
  for (uint16_t i = 0; i < qty; i++) {
    outRegs[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
  }
  return MB_OK;
}

// Reads the 4 bytes a write reply ends with (address + value for FC 0x06,
// address + register count for FC 0x10) and checks they match the request.
static ModbusResult readWriteEcho(WiFiClient &client, uint16_t addr, uint16_t arg,
                                  uint32_t start, uint32_t timeoutMs) {
  if (!waitForBytes(client, 4, start, timeoutMs)) return MB_FAIL;
  uint8_t echo[4];
  client.readBytes(echo, 4);
  bool match = echo[0] == (addr >> 8) && echo[1] == (addr & 0xFF) &&
               echo[2] == (arg >> 8) && echo[3] == (arg & 0xFF);
  return match ? MB_OK : MB_FAIL;
}

// The reply echoes address and value.
ModbusResult modbusWriteRegister(WiFiClient &client, uint8_t slaveId, uint16_t addr,
                                 uint16_t value, uint32_t timeoutMs) {
  uint32_t start = millis();
  ModbusResult r = modbusRequest(client, slaveId, FC_WRITE_SINGLE, addr, value, WRITE_REPLY_PDU, start,
                                 timeoutMs);
  if (r != MB_OK) return r;
  return readWriteEcho(client, addr, value, start, timeoutMs);
}

// The reply echoes address and register count.
ModbusResult modbusWriteU32(WiFiClient &client, uint8_t slaveId, uint16_t addr,
                            uint32_t value, uint32_t timeoutMs) {
  uint8_t data[5] = { 4,  // byte count
                      (uint8_t)(value >> 24), (uint8_t)(value >> 16),
                      (uint8_t)(value >> 8), (uint8_t)value };
  uint32_t start = millis();
  ModbusResult r = modbusRequest(client, slaveId, FC_WRITE_MULTIPLE, addr, 2, WRITE_REPLY_PDU, start,
                                 timeoutMs, data, sizeof(data));
  if (r != MB_OK) return r;
  return readWriteEcho(client, addr, 2, start, timeoutMs);
}
