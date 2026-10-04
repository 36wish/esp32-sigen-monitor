#include "discovery.h"

#include <lwip/sockets.h>
#include <errno.h>
#include "dashboard.h"  // showStatus()
#include "display.h"    // pumpLvgl(), waitMs()
#include "modbus_tcp.h"
#include "sigenergy.h"
#include "storage.h"

// The cached address gets several patient attempts before we fall back to a scan.
static const uint32_t CACHE_CONNECT_MS = 2000;
static const uint32_t CACHE_READ_MS    = 2000;
static const uint32_t CACHE_RETRY_MS   = 1000;

// Subnet sweep: this many non-blocking port-502 connects in flight at once
// (lwIP has 16 sockets in total), all sharing one timeout. Hosts with the port
// open then get a Modbus test read.
static const int      SCAN_PARALLEL         = 12;
static const uint32_t SCAN_CONNECT_MS       = 400;
static const uint32_t SCAN_PROBE_CONNECT_MS = 1000;
static const uint32_t SCAN_PROBE_READ_MS    = 1500;

// Connects to `ip` and reads the live block from the plant's slave ID. Other
// Modbus devices can answer that too (and would then get the Charge and Free
// hour writes), so the reply also has to look like a Sigenergy plant: a rated
// power, and an SOC of at most 100%.
static bool probePlant(IPAddress ip, uint32_t connectMs, uint32_t readMs) {
  WiFiClient client;
  if (!client.connect(ip, MODBUS_PORT, connectMs)) return false;
  uint16_t regs[LIVE_BLOCK_COUNT];
  bool ok = modbusReadRegisters(client, PLANT_SLAVE_ID, FC_READ_HOLDING, LIVE_BLOCK_START, LIVE_BLOCK_COUNT,
                                regs, readMs) == MB_OK;
  client.stop();
  if (!ok) return false;
  uint32_t ratedW = toU32(&regs[MAX_ACTIVE_REG - LIVE_BLOCK_START]);
  uint16_t socRaw = regs[BATTERY_SOC_REG - LIVE_BLOCK_START];
  if (ratedW > 0 && socRaw <= 1000) return true;
  Serial.printf("%s answered, but not like a Sigenergy plant (rated power %lu W, SOC raw %u)\n",
                ip.toString().c_str(), (unsigned long)ratedW, (unsigned)socRaw);
  return false;
}

// Starts a non-blocking connect to the Modbus port on each host (at most
// SCAN_PARALLEL) at once and reports which accepted. Absent hosts and closed
// ports cost one shared timeout instead of one each, and LVGL keeps running
// while we wait.
static void findOpenPorts(const IPAddress *hosts, int count, bool *open, uint32_t timeoutMs) {
  int fds[SCAN_PARALLEL];
  int pending = 0;
  for (int i = 0; i < count; i++) {
    open[i] = false;
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (fds[i] < 0) continue;
    fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MODBUS_PORT);
    addr.sin_addr.s_addr = (uint32_t)hosts[i];
    if (lwip_connect(fds[i], (sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
      close(fds[i]);
      fds[i] = -1;
      continue;
    }
    pending++;
  }

  uint32_t start = millis();
  while (pending > 0 && millis() - start < timeoutMs) {
    fd_set wr;
    FD_ZERO(&wr);
    int maxFd = -1;
    for (int i = 0; i < count; i++) {
      if (fds[i] < 0) continue;
      FD_SET(fds[i], &wr);
      if (fds[i] > maxFd) maxFd = fds[i];
    }
    timeval tv = { 0, 20000 };
    int n = select(maxFd + 1, nullptr, &wr, nullptr, &tv);
    if (n < 0) break;
    for (int i = 0; n > 0 && i < count; i++) {
      if (fds[i] < 0 || !FD_ISSET(fds[i], &wr)) continue;
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
      open[i] = (err == 0);  // a refused connect also wakes select, with an error set
      close(fds[i]);
      fds[i] = -1;
      pending--;
    }
    pumpLvgl();
  }
  for (int i = 0; i < count; i++) {
    if (fds[i] >= 0) close(fds[i]);
  }
}

bool modbusPortOpen(IPAddress ip, uint32_t timeoutMs) {
  bool open = false;
  findOpenPorts(&ip, 1, &open, timeoutMs);
  return open;
}

bool findCachedPlant(IPAddress &ip) {
  String cached = prefs.getString(NVS_PLANT_IP, "");
  if (cached.length() == 0) return false;
  IPAddress candidate;
  if (!candidate.fromString(cached)) return false;
  for (int attempt = 1; attempt <= CACHE_ATTEMPTS; attempt++) {
    Serial.printf("Trying cached Sigenergy IP %s (attempt %d/%d)...\n", cached.c_str(), attempt, CACHE_ATTEMPTS);
    showStatus("Checking last known address\n" + cached + "  (try " + String(attempt) + "/" +
               String(CACHE_ATTEMPTS) + ")");
    if (probePlant(candidate, CACHE_CONNECT_MS, CACHE_READ_MS)) {
      ip = candidate;
      Serial.println("Cached IP confirmed.");
      return true;
    }
    if (attempt < CACHE_ATTEMPTS) waitMs(CACHE_RETRY_MS);
  }
  Serial.println("Cached IP did not respond, will rescan.");
  return false;
}

// Home/office LANs are almost always /24; if yours is larger, narrow it down
// manually via the cached-IP path.
bool scanForPlant(IPAddress &ip) {
  IPAddress local = WiFi.localIP();
  String subnet = local.toString().substring(0, local.toString().lastIndexOf('.') + 1);
  Serial.printf("Scanning %s0/24 for a Sigenergy plant (Modbus TCP, slave %u)...\n",
                subnet.c_str(), PLANT_SLAVE_ID);
  uint32_t scanStart = millis();
  int h = 1;
  while (h <= 254) {
    IPAddress batch[SCAN_PARALLEL];
    int n = 0;
    for (; h <= 254 && n < SCAN_PARALLEL; h++) {
      if (h != local[3]) batch[n++] = IPAddress(local[0], local[1], local[2], (uint8_t)h);
    }
    showStatus("Searching for Sigenergy plant...\n" + subnet + "0/24  (" + String(h - 1) + "/254)");

    bool open[SCAN_PARALLEL];
    findOpenPorts(batch, n, open, SCAN_CONNECT_MS);
    for (int i = 0; i < n; i++) {
      if (!open[i]) continue;
      Serial.printf("Port %u open on %s, checking Modbus...\n", MODBUS_PORT, batch[i].toString().c_str());
      if (probePlant(batch[i], SCAN_PROBE_CONNECT_MS, SCAN_PROBE_READ_MS)) {
        ip = batch[i];
        Serial.printf("Found Sigenergy plant at %s (%lu ms)\n", ip.toString().c_str(),
                      (unsigned long)(millis() - scanStart));
        prefs.putString(NVS_PLANT_IP, ip.toString());
        return true;
      }
    }
  }
  return false;
}
