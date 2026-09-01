// ============================================================================
//  Smart ePod - wireless layer
//  See wireless.h for the contract and the reasoning behind the split.
// ============================================================================
#include "wireless.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------------------------------------------------------- config ---
#define AP_SSID      "ePod-Music"
#define AP_PASSWORD  "epodmusicpass"
#define AP_ADDRESS   "192.168.4.1"

// UUIDs from MOBILE_APP_INTEGRATION.md. They must match the app exactly.
#define SVC_UUID   "00004550-0000-1000-8000-00805F9B34FB"
#define EP01_UUID  "00004501-0000-1000-8000-00805F9B34FB"   // control   (write)
#define EP02_UUID  "00004502-0000-1000-8000-00805F9B34FB"   // telemetry (notify)
#define EP03_UUID  "00004503-0000-1000-8000-00805F9B34FB"   // file      (write)

#define CMD_QUEUE_SLOTS 6
#define CMD_MAX_LEN     72

// ----------------------------------------------------------------- state ---
static WebServer httpServer(80);
static bool wifiUp = false;

static BLEServer* bleServer = NULL;
static BLECharacteristic* chTelemetry = NULL;
static bool bleConnected = false;
static bool bleReady = false;
static bool bleActive = false;
static uint32_t uploadTotalSize = 0;
static char devName[32] = "ePod";
static unsigned long lastProgressReport = 0;
static uint32_t lastReportedBytes = 0;

// Command ring. Written from radio callbacks, drained by the main loop, so a
// single producer index and a single consumer index need no lock.
static char cmdQueue[CMD_QUEUE_SLOTS][CMD_MAX_LEN];
static volatile uint8_t cmdHead = 0;
static volatile uint8_t cmdTail = 0;

// Upload state, shared by both transports - only one can be active at a time.
static File uploadFile;
static char uploadName[64];
static volatile bool uploadBusy = false;
static volatile bool uploadDone = false;
static char uploadDoneName[48];
static uint32_t uploadBytes = 0;

// ============================================================================
//  Helpers
// ============================================================================
static void queueCommand(const char* line) {
  uint8_t next = (cmdHead + 1) % CMD_QUEUE_SLOTS;
  if (next == cmdTail) return;                 // full: drop rather than block
  strncpy(cmdQueue[cmdHead], line, CMD_MAX_LEN - 1);
  cmdQueue[cmdHead][CMD_MAX_LEN - 1] = '\0';
  cmdHead = next;
}

bool wirelessNextCommand(char* out, size_t outSize) {
  if (cmdTail == cmdHead) return false;
  strncpy(out, cmdQueue[cmdTail], outSize - 1);
  out[outSize - 1] = '\0';
  cmdTail = (cmdTail + 1) % CMD_QUEUE_SLOTS;
  return true;
}

// Splits an incoming blob into newline-terminated commands. The app sends
// "PLAY\n"; BLE may deliver several at once or one split across writes.
static void ingestText(const uint8_t* data, size_t len) {
  static char partial[CMD_MAX_LEN];
  static uint8_t partialLen = 0;

  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(data[i]);
    if (c == '\n' || c == '\r') {
      if (partialLen > 0) {
        partial[partialLen] = '\0';
        queueCommand(partial);
        partialLen = 0;
      }
    } else if (c >= 32 && c <= 126 && partialLen < CMD_MAX_LEN - 1) {
      partial[partialLen++] = c;
    }
  }
}

// Only the lines the app's parser understands are worth the airtime.
static bool isAppTelemetry(const char* line) {
  return strncmp(line, "INFO ", 5) == 0 || strncmp(line, "TRACK ", 6) == 0 ||
         strncmp(line, "STATE ", 6) == 0 || strncmp(line, "VOL ", 4) == 0 ||
         strncmp(line, "MSG ", 4) == 0 || strcmp(line, "RESCAN") == 0;
}

void wirelessNotify(const char* line) {
  if (!bleReady || !bleConnected || chTelemetry == NULL) return;
  if (!isAppTelemetry(line)) return;
  chTelemetry->setValue(String(line));
  chTelemetry->notify();
}

bool wirelessBleConnected() { return bleConnected; }
bool wirelessUploadBusy() { return uploadBusy; }
const char* wirelessApAddress() { return AP_ADDRESS; }

// ============================================================================
//  Upload, shared by BLE and HTTP
// ============================================================================
static void sanitiseName(const char* in, char* out, size_t outSize) {
  size_t n = 0;
  for (const char* p = in; *p && n < outSize - 5; p++) {
    char c = *p;
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' ||
              c == '.';
    if (ok) out[n++] = c;
  }
  out[n] = '\0';
  if (n == 0) strncpy(out, "track.raw", outSize - 1);
  // The player only indexes .raw and .wav; anything else would upload fine and
  // then be invisible, which looks like a failure.
  size_t len = strlen(out);
  if (len < 4 || strcasecmp(out + len - 4, ".raw") != 0) {
    if (len > outSize - 5) len = outSize - 5;
    strcpy(out + len, ".raw");
  }
}

static bool uploadBegin(const char* rawName) {
  if (uploadBusy) return false;
  if (!epodCanAcceptUpload()) return false;

  // Claimed before the first SD call, not after. A BLE transfer runs on the
  // radio task, and this flag is what stops the main loop touching the card at
  // the same time.
  uploadBusy = true;

  char clean[48];
  sanitiseName(rawName, clean, sizeof(clean));
  snprintf(uploadName, sizeof(uploadName), "%s/%s", epodMusicDir(), clean);

  if (!SD.exists(epodMusicDir())) SD.mkdir(epodMusicDir());
  uploadFile = SD.open(uploadName, FILE_WRITE);
  if (!uploadFile) {
    Serial.print("Upload: cannot create ");
    Serial.println(uploadName);
    uploadBusy = false;
    return false;
  }
  uploadBytes = 0;
  uploadTotalSize = 0;
  Serial.print("Upload started: ");
  Serial.println(uploadName);
  epodUploadStarted(clean);
  return true;
}

static void uploadWrite(const uint8_t* data, size_t len) {
  if (!uploadBusy || !uploadFile || len == 0) return;
  if (uploadFile.write(data, len) != len) {
    Serial.println("Upload: write failed, card full?");
    uploadFile.close();
    SD.remove(uploadName);
    uploadBusy = false;
    return;
  }
  uploadBytes += len;
}

static void uploadEnd(bool keep) {
  if (!uploadBusy) return;
  if (uploadFile) uploadFile.close();

  const char* shortName = strrchr(uploadName, '/');
  shortName = shortName ? shortName + 1 : uploadName;

  if (!keep || uploadBytes == 0) {
    SD.remove(uploadName);
    Serial.println("Upload discarded (empty).");
    uploadBusy = false;                        // released last, after the SD work
    epodUploadFailed();
    return;
  }

  Serial.print("Upload complete: ");
  Serial.print(shortName);
  Serial.print("  ");
  Serial.print(uploadBytes);
  Serial.println(" bytes");

  // Re-reading the card is more SD work, and over BLE this function is running
  // on the radio task. Hand it to the main loop instead - uploadBusy stays set
  // until that has finished, so nothing else touches the card meanwhile.
  strncpy(uploadDoneName, shortName, sizeof(uploadDoneName) - 1);
  uploadDoneName[sizeof(uploadDoneName) - 1] = '\0';
  uploadDone = true;
}

// ============================================================================
//  BLE
// ============================================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    bleConnected = true;
    Serial.println("BLE: app connected.");
  }
  void onDisconnect(BLEServer* server) override {
    bleConnected = false;
    Serial.println("BLE: app disconnected.");
    // A half-finished transfer must not leave a stub file on the card.
    if (uploadBusy) uploadEnd(false);
    if (bleActive) {
      server->startAdvertising();               // stay findable
    }
  }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    ingestText(ch->getData(), ch->getLength());
  }
};

// EP03 packet: [seqHi][seqLo][lenHi][lenLo][payload...]
// A zero-length payload marks end of file.
class FileCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    const uint8_t* d = ch->getData();
    size_t n = ch->getLength();
    if (n < 4) return;

    uint16_t seq = (static_cast<uint16_t>(d[0]) << 8) | d[1];
    uint16_t payLen = (static_cast<uint16_t>(d[2]) << 8) | d[3];
    if (payLen > n - 4) payLen = n - 4;        // never trust a length field

    if (seq == 0 && !uploadBusy) {
      // The app does not name the file over BLE, so it gets a generic one and
      // can rename it later; the important thing is that it lands in /music.
      uploadBegin("ble_upload.raw");
    }
    if (payLen > 0) {
      uploadWrite(d + 4, payLen);
    } else {
      uploadEnd(true);
    }
  }
};

static void bleBegin(const char* deviceName) {
  BLEDevice::init(deviceName);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  BLEService* svc = bleServer->createService(SVC_UUID);

  BLECharacteristic* control = svc->createCharacteristic(
      EP01_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  control->setCallbacks(new ControlCallbacks());

  chTelemetry = svc->createCharacteristic(EP02_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  chTelemetry->addDescriptor(new BLE2902());

  BLECharacteristic* file = svc->createCharacteristic(
      EP03_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  file->setCallbacks(new FileCallbacks());

  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  bleReady = true;
  bleActive = true;
  Serial.print("BLE advertising as ");
  Serial.println(deviceName);
}

// ============================================================================
//  Wi-Fi + HTTP
// ============================================================================
static void handleUpload() {
  HTTPUpload& up = httpServer.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadBegin(up.filename.c_str());
      uploadTotalSize = up.totalSize;
      break;
    case UPLOAD_FILE_WRITE:
      uploadWrite(up.buf, up.currentSize);
      uploadTotalSize = up.totalSize;
      break;
    case UPLOAD_FILE_END:
      uploadEnd(true);
      break;
    default:
      if (uploadBusy) uploadEnd(false);        // aborted
      break;
  }
}

bool wirelessWifiStart() {
  if (wifiUp) return true;
  if (!epodCanAcceptUpload()) {
    Serial.println("Wi-Fi refused: only available while stopped.");
    return false;
  }

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Soft-AP failed to start.");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // The upload handler is the second callback: the first runs only once the
  // whole body has arrived, which is far too late to be writing the file.
  httpServer.on("/upload", HTTP_POST,
                []() { httpServer.send(200, "text/plain", "OK"); },
                handleUpload);

  httpServer.on("/status", HTTP_GET, []() {
    char line[80];
    epodStatusLine(line, sizeof(line));
    httpServer.send(200, "text/plain", line);
  });

  // --- signage API ---------------------------------------------------------
  // CORS is sent even though serve.py proxies these: it lets the page be opened
  // straight from a browser pointed at the device, which is the fallback if the
  // laptop has no Python on the day.
  httpServer.on("/api/state", HTTP_GET, []() {
    char json[200];
    epodApiState(json, sizeof(json));
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(200, "application/json", json);
  });

  httpServer.on("/api/command", HTTP_POST, []() {
    char json[200];
    epodApiCommand(httpServer.arg("plain").c_str(), json, sizeof(json));
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", json);
  });

  httpServer.on("/api/command", HTTP_OPTIONS, []() {
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    httpServer.send(204);
  });

  httpServer.onNotFound([]() { httpServer.send(404, "text/plain", "ePod"); });
  httpServer.begin();

  wifiUp = true;
  Serial.print("Wi-Fi AP up: ");
  Serial.print(AP_SSID);
  Serial.print(" / ");
  Serial.println(AP_ADDRESS);
  return true;
}

void wirelessWifiStop() {
  if (!wifiUp) return;
  if (uploadBusy) uploadEnd(false);
  httpServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiUp = false;
  Serial.println("Wi-Fi off.");
}

bool wirelessWifiActive() { return wifiUp; }

// ============================================================================
//  Lifecycle
// ============================================================================
void wirelessBegin(const char* deviceName) {
  strncpy(devName, deviceName, sizeof(devName) - 1);
  devName[sizeof(devName) - 1] = 0;
  // BLE is off by default.
}

bool wirelessBleStart() {
  if (bleActive) return true;
  if (!bleReady) {
    bleBegin(devName);
  } else {
    bleActive = true;
    BLEDevice::startAdvertising();
  }
  Serial.println("BLE started.");
  return true;
}

void wirelessBleStop() {
  if (!bleActive) return;
  bleActive = false;
  if (bleReady) {
    BLEDevice::getAdvertising()->stop();
    if (bleConnected && bleServer) {
      // Standard BLE Server disconnect by querying map
      std::map<uint16_t, conn_status_t> peers = bleServer->getPeerDevices(true);
      for (auto const& pair : peers) {
        bleServer->disconnect(pair.first);
      }
    }
  }
  bleConnected = false;
  Serial.println("BLE stopped.");
}

bool wirelessBleActive() {
  return bleActive;
}

void wirelessService() {
  if (wifiUp) httpServer.handleClient();

  if (uploadBusy && (millis() - lastProgressReport >= 250 || uploadBytes != lastReportedBytes)) {
    lastProgressReport = millis();
    lastReportedBytes = uploadBytes;
    epodUploadProgress(uploadBytes, uploadTotalSize);
  }

  // The one place a finished upload is acted on, and it is the main loop.
  if (uploadDone) {
    uploadDone = false;
    epodUploadFinished(uploadDoneName);
    uploadBusy = false;
  }
}
