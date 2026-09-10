/**
 * DisplayConnect v2 — LOLIN32 Lite + ST7796S
 *
 * BLE UART (Nordic NUS) server receives JSON navigation updates from the
 * Android app and renders a vector map (route + position + maneuver overlay).
 *
 * IMPORTANT: Never call TFT / ArduinoJson from NimBLE callbacks — they run on
 * a small BLE task stack and will reboot the CYD. Callbacks only queue bytes /
 * flags; loop() does drawing and parsing.
 *
 * Libraries:
 *   - TFT_eSPI (Bodmer)
 *   - NimBLE-Arduino (h2zero) v2.x
 *   - ArduinoJson (Benoit Blanchon) v7+
 */

#include "config.h"

#include <NimBLEDevice.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <string.h>

#include "nav_types.h"
#include "map_renderer.h"
#include "nav_protocol.h"
#include "loading_screen.h"
#include "maps_theme.h"
#include "touch_cyd.h"
#include "utf8_text.h"
#include <ArduinoJson.h>

TFT_eSPI tft;
MapRenderer mapRenderer(tft);

static volatile bool clientConnected = false;
static volatile bool awaitingFirstNav = false;
static volatile bool uiShowLoading = false;
static volatile bool uiShowWaiting = false;
static volatile bool sendOkPending = false;

static uint32_t navUpdatesReceived = 0;
static uint32_t lastStatsMillis = 0;
static uint16_t updatesPerSecond = 0;

static NavState lastNav;
static bool hasNav = false;
static bool showingWaiting = true;
static bool touchWasDown = false;
static uint32_t lastThemeToggleMs = 0;

static const size_t LINE_BUF_SIZE = 12288;
static char lineBuf[LINE_BUF_SIZE];
static size_t lineLen = 0;
static bool discardLine = false;
static bool renderPending = false;

// Byte queue filled in BLE write callback, drained in loop()
static const size_t RX_QUEUE_SIZE = 24576;
static uint8_t rxQueue[RX_QUEUE_SIZE];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static bool rxOverflow = false;

static NimBLEServer* bleServer = nullptr;
static NimBLECharacteristic* txCharacteristic = nullptr;

bool initDisplay();
bool initBle();
void showStatusScreen(const char* title, const char* line2 = nullptr, const char* line3 = nullptr);
void showWaitingForAppScreen();
void processTextMessage(const char* payload, size_t length);
void drainRxQueue();
void appendRxByte(char c);
void updateStatsCounter();
void handleUiFlags();
void pollThemeSwitch();
void redrawAfterThemeChange();

static bool rxQueuePush(uint8_t b) {
  portENTER_CRITICAL(&rxMux);
  const size_t next = (rxHead + 1) % RX_QUEUE_SIZE;
  if (next == rxTail) {
    rxOverflow = true;
    portEXIT_CRITICAL(&rxMux);
    return false; // full
  }
  rxQueue[rxHead] = b;
  rxHead = next;
  portEXIT_CRITICAL(&rxMux);
  return true;
}

static bool rxQueuePop(uint8_t& b) {
  portENTER_CRITICAL(&rxMux);
  if (rxTail == rxHead) {
    portEXIT_CRITICAL(&rxMux);
    return false;
  }
  b = rxQueue[rxTail];
  rxTail = (rxTail + 1) % RX_QUEUE_SIZE;
  portEXIT_CRITICAL(&rxMux);
  return true;
}

static void resetRxQueue() {
  portENTER_CRITICAL(&rxMux);
  rxHead = rxTail = 0;
  rxOverflow = false;
  portEXIT_CRITICAL(&rxMux);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/) override {
    clientConnected = true;
    awaitingFirstNav = true;
    navUpdatesReceived = 0;
    updatesPerSecond = 0;
    lastStatsMillis = millis();
    resetRxQueue();
    hasNav = false;
    showingWaiting = false;
    uiShowLoading = true;
    sendOkPending = true;
    // Do NOT draw TFT or notify heavily here — defer to loop()
  }

  void onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/, int /*reason*/) override {
    clientConnected = false;
    awaitingFirstNav = false;
    navUpdatesReceived = 0;
    resetRxQueue();
    hasNav = false;
    uiShowWaiting = true;
    // Advertising restart deferred to loop() — safer than here
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /*connInfo*/) override {
    const NimBLEAttValue& value = characteristic->getValue();
    const size_t len = value.length();
    const uint8_t* data = value.data();
    for (size_t i = 0; i < len; i++) {
      if (!rxQueuePush(data[i])) {
        break; // drop overflow rather than crash
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== DisplayConnect LOLIN32 ST7796S v2 (BLE) ==="));

  mapsThemeInit();
  initDisplayLanguage();

  if (!initDisplay()) {
    Serial.println(F("Failed to initialize display."));
    while (true) { delay(1000); }
  }

  if (!touchInit()) {
    Serial.println(F("Touch disabled for this hardware profile"));
  }

  showStatusScreen("DisplayConnect", displayEnglish() ? "Starting BLE..." : "Iniciando Bluetooth...");

  if (!initBle()) {
    showStatusScreen(displayEnglish() ? "BLE Error" : "Erro no Bluetooth", displayEnglish() ? "Restart the board." : "Reinicie a placa.");
    while (true) { delay(1000); }
  }

  showWaitingForAppScreen();
  showingWaiting = true;
}

void loop() {
  handleUiFlags();
  drainRxQueue();
  if (renderPending && hasNav) {
    renderPending = false;
    const uint32_t started = millis();
    mapRenderer.draw(lastNav);
    Serial.printf("Map: %d street segments, %d route points, draw %lu ms\n",
                  lastNav.streetSegmentCount, lastNav.routeCount,
                  static_cast<unsigned long>(millis() - started));
  }
  pollThemeSwitch();

  if (clientConnected && awaitingFirstNav) {
    updateMapLoadingAnimation(tft, millis());
  }

  updateStatsCounter();
  delay(5);
}

void handleUiFlags() {
  if (uiShowLoading) {
    uiShowLoading = false;
    lineLen = 0;
    discardLine = false;
    renderPending = false;
    showingWaiting = false;
    showMapLoadingScreen(tft);
  }

  if (uiShowWaiting) {
    uiShowWaiting = false;
    lineLen = 0;
    discardLine = false;
    renderPending = false;
    showingWaiting = true;
    showWaitingForAppScreen();
    if (bleServer != nullptr) {
      bleServer->startAdvertising();
    }
  }

  if (sendOkPending && clientConnected && txCharacteristic != nullptr) {
    sendOkPending = false;
    txCharacteristic->setValue("OK\n");
    txCharacteristic->notify();
  }
}

void redrawAfterThemeChange() {
  if (hasNav && clientConnected) {
    mapRenderer.draw(lastNav);
    showingWaiting = false;
  } else if (clientConnected && awaitingFirstNav) {
    showMapLoadingScreen(tft);
    showingWaiting = false;
  } else {
    showWaitingForAppScreen();
    showingWaiting = true;
  }
}

void pollThemeSwitch() {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool down = touchRead(x, y);
  const uint32_t now = millis();

  if (down && !touchWasDown) {
    const bool hit = MapRenderer::themeSwitchHit(x, y);
    Serial.printf("tap %u,%u hit=%d\n", x, y, hit ? 1 : 0);
    if (hit && (now - lastThemeToggleMs) > 350) {
      const bool dark = mapsThemeToggle();
      lastThemeToggleMs = now;
      Serial.printf("theme -> %s\n", dark ? "dark" : "light");
      redrawAfterThemeChange();
    }
  }
  touchWasDown = down;
}

bool initDisplay() {
  tft.init();
  tft.setRotation(1);
  Serial.printf("TFT logical size: %dx%d\n", tft.width(), tft.height());
  if (tft.width() != SCR_W || tft.height() != SCR_H) return false;
  tft.fillScreen(mapsColLand());
  return true;
}

bool initBle() {
  // Keep NimBLE lean — CYD has limited RAM with TFT_eSPI
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  NimBLEDevice::setMTU(BLE_MTU);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  bleServer->advertiseOnDisconnect(false); // we restart advertising from loop()

  NimBLEService* service = bleServer->createService(NUS_SERVICE_UUID);
  NimBLECharacteristic* rx = service->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  rx->setCallbacks(new RxCallbacks());

  txCharacteristic = service->createCharacteristic(
      NUS_TX_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
  );

  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(NUS_SERVICE_UUID);
  advertising->start();

  Serial.printf("BLE advertising as \"%s\"\n", BLE_DEVICE_NAME);
  return true;
}

void showStatusScreen(const char* title, const char* line2, const char* line3) {
  tft.fillScreen(mapsColLand());
  if (!mapsThemeIsDark()) {
    tft.fillCircle(SCR_W - 30, 90, 36, mapsColPark());
  }
  tft.setTextDatum(MC_DATUM);

  tft.fillRect(0, 0, SCR_W, 36, mapsColCard());
  tft.fillRect(0, 36, SCR_W, 2, mapsColCardShadow());
  tft.setTextColor(mapsColText(), mapsColCard());
  tft.drawString(title, SCR_W / 2, 18, 2);

  tft.fillRoundRect(16, 100, SCR_W - 32, 80, 8, mapsColCard());
  tft.fillRect(16, 100, SCR_W - 32, 3, mapsColRoute());

  if (line2 != nullptr) {
    tft.setTextColor(mapsColAccent(), mapsColCard());
    tft.drawString(line2, SCR_W / 2, 128, 2);
  }
  if (line3 != nullptr) {
    tft.setTextColor(mapsColMuted(), mapsColCard());
    tft.drawString(line3, SCR_W / 2, 156, 2);
  }



  tft.setTextColor(mapsColMuted(), mapsColLand());
  tft.drawString(F("LOLIN32 Lite - ST7796S"), SCR_W / 2, SCR_H - 16, 1);
}

void showWaitingForAppScreen() {
  showStatusScreen(displayEnglish() ? "Ready to navigate" : "Pronto para navegar", "Bluetooth LE", BLE_DEVICE_NAME);
}

void appendRxByte(char c) {
  if (c == '\n' || c == '\r') {
    if (lineLen > 0 && !discardLine) {
      lineBuf[lineLen] = '\0';
      processTextMessage(lineBuf, lineLen);
      lineLen = 0;
    }
    lineLen = 0;
    discardLine = false;
    return;
  }
  if (discardLine) return;
  if (lineLen + 1 >= LINE_BUF_SIZE) {
    Serial.println(F("BLE line buffer overflow — reset"));
    lineLen = 0;
    discardLine = true;
    return;
  }
  lineBuf[lineLen++] = c;
}

void drainRxQueue() {
  portENTER_CRITICAL(&rxMux);
  const bool overflow = rxOverflow;
  if (overflow) {
    rxTail = rxHead;
    rxOverflow = false;
  }
  portEXIT_CRITICAL(&rxMux);
  if (overflow) {
    lineLen = 0;
    discardLine = true;
    Serial.println(F("BLE overflow: discarded incomplete frame"));
  }
  uint8_t b;
  // Process a bounded number per loop to keep UI responsive
  for (int i = 0; i < 4096; i++) {
    if (!rxQueuePop(b)) {
      break;
    }
    appendRxByte(static_cast<char>(b));
  }
}

static bool sameDrawing(const NavState& a, const NavState& b) {
  if (a.routeCount != b.routeCount || a.streetSegmentCount != b.streetSegmentCount ||
      a.hasUserPosition != b.hasUserPosition || a.userX != b.userX || a.userY != b.userY ||
      a.bearing != b.bearing || a.distanceM != b.distanceM || a.hasHtml != b.hasHtml ||
      a.remainingM != b.remainingM || a.remainingS != b.remainingS || a.offRoute != b.offRoute ||
      a.english != b.english || a.gpsWeak != b.gpsWeak ||
      strcmp(a.instruction, b.instruction) || strcmp(a.street, b.street) || strcmp(a.html, b.html)) {
    return false;
  }
  const size_t routeBytes = a.routeCount * sizeof(int16_t);
  const size_t streetBytes = a.streetSegmentCount * sizeof(int16_t);
  return memcmp(a.routeX, b.routeX, routeBytes) == 0 &&
         memcmp(a.routeY, b.routeY, routeBytes) == 0 &&
         memcmp(a.streetX0, b.streetX0, streetBytes) == 0 &&
         memcmp(a.streetY0, b.streetY0, streetBytes) == 0 &&
         memcmp(a.streetX1, b.streetX1, streetBytes) == 0 &&
         memcmp(a.streetY1, b.streetY1, streetBytes) == 0;
}

void processTextMessage(const char* payload, size_t length) {
  if (length < 150 && strstr(payload, "\"type\":\"config\"")) {
    JsonDocument config;
    if (!deserializeJson(config, payload, length)) {
      setDisplayLanguage(strcmp(config["lang"] | "pt-BR", "en") == 0);
      if (hasNav) { lastNav.english = displayEnglish(); renderPending = true; }
      else if (showingWaiting) showWaitingForAppScreen();
      else showMapLoadingScreen(tft);
    }
    return;
  }
  if (isLoadingJson(payload, length)) {
    awaitingFirstNav = true;
    hasNav = false;
    showingWaiting = false;
    showMapLoadingScreen(tft);
    return;
  }

  NavState state;
  if (!parseNavJson(payload, length, state)) {
    return;
  }

  const bool changed = !hasNav || !sameDrawing(state, lastNav);
  setDisplayLanguage(state.english);
  lastNav = state;
  hasNav = true;
  showingWaiting = false;
  renderPending = renderPending || changed;
  awaitingFirstNav = false;
  navUpdatesReceived++;
  updatesPerSecond++;
}

void updateStatsCounter() {
  const uint32_t now = millis();
  if (now - lastStatsMillis >= 1000) {
    if (clientConnected && navUpdatesReceived > 0) {
      Serial.printf("Nav updates/s: %u  Total: %lu\n",
                    updatesPerSecond,
                    static_cast<unsigned long>(navUpdatesReceived));
    }
    updatesPerSecond = 0;
    lastStatsMillis = now;
  }
}
