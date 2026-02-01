/*
 * ███████╗██╗ ██████╗ ███╗   ██╗ █████╗ ██╗         ██████╗ ███████╗
 * ██╔════╝██║██╔════╝ ████╗  ██║██╔══██╗██║         ██╔══██╗██╔════╝
 * ███████╗██║██║  ███╗██╔██╗ ██║███████║██║         ██████╔╝█████╗
 * ╚════██║██║██║   ██║██║╚██╗██║██╔══██║██║         ██╔══██╗██╔══╝
 * ███████║██║╚██████╔╝██║ ╚████║██║  ██║███████╗    ██║  ██║██║
 * ╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝    ╚═╝  ╚═╝╚═╝
 *
 * Signal RF - Multi-Tool RF Analysis Firmware
 * For M5Stack Cardputer
 *
 * -Made by Byte
 *
 * Version: 2.1.1
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <arduinoFFT.h>
#include <SD.h>
#include <deque>

// ============== THEME SELECTION ==============
// Uncomment ONE theme:
//#define THEME_DEFAULT
#define THEME_SASQUATCH

// ============== CONFIGURATION ==============
#define APP_VERSION "2.1.1"
#define SD_CS_PIN 12
#define SAMPLE_RATE 44100
#define SAMPLES 512
#define MAX_DEVICES 50
#define RSSI_HISTORY_SIZE 60
#define AUTO_SCAN_INTERVAL 5000
#define OUI_FILE "/oui.txt"

#ifndef TFT_ORANGE
#define TFT_ORANGE 0xFD20
#endif

#define FFT_BANDS 10
#define ULTRASONIC_THRESHOLD 18000
#define RSSI_STRONG_THRESHOLD -50
#define RSSI_MEDIUM_THRESHOLD -70

// ============== THEME DEFINITIONS ==============
#ifdef THEME_SASQUATCH
  // Sasquatch Edition - The Talking Sasquatch
  #define APP_NAME "Signal RF"
  #define THEME_NAME "SASQUATCH EDITION"
  #define THEME_AUTHOR "The Talking Sasquatch"
  #define COLOR_STATUSBAR 0x2104    // Dark forest green
  #define COLOR_BACKGROUND TFT_BLACK
  #define COLOR_TITLE 0x07FF        // Cyan
  #define COLOR_ACCENT 0xFBE0       // Gold/Orange
  #define COLOR_HIGHLIGHT 0x07E0    // Green
  #define COLOR_TEXT TFT_WHITE
  #define COLOR_SUBTLE 0x8410       // Gray
  #define MSG_NO_TRACKERS "Stay hidden."
  #define MSG_TRACKERS_FOUND "!! WARNING !!"
  #define MSG_SCANNING "Searching..."
  #define MSG_MENU_TITLE "SASQUATCH TOOLKIT"
  #define SHOW_FOOTER_PATTERN true
  #define STATUSBAR_ICON "\xf0\x9f\x8c\xb2"  // Tree emoji (if supported) or use "^"
#else
  // Default Theme
  #define APP_NAME "Signal RF"
  #define THEME_NAME ""
  #define THEME_AUTHOR "Byte"
  #define COLOR_STATUSBAR TFT_NAVY
  #define COLOR_BACKGROUND TFT_BLACK
  #define COLOR_TITLE TFT_CYAN
  #define COLOR_ACCENT TFT_YELLOW
  #define COLOR_HIGHLIGHT TFT_GREEN
  #define COLOR_TEXT TFT_WHITE
  #define COLOR_SUBTLE TFT_DARKGREY
  #define MSG_NO_TRACKERS "No trackers detected"
  #define MSG_TRACKERS_FOUND "TRACKERS FOUND!"
  #define MSG_SCANNING "Scanning..."
  #define MSG_MENU_TITLE "MAIN MENU"
  #define SHOW_FOOTER_PATTERN false
  #define STATUSBAR_ICON ""
#endif

// ============== DATA STRUCTURES ==============
struct DeviceInfo {
  String mac;
  String name;
  String vendor;
  int rssi;
  int channel;
  bool isNew;
  unsigned long firstSeen;
  unsigned long lastSeen;
  std::deque<int> rssiHistory;
};

// ============== GLOBAL VARIABLES ==============
int batteryPercent = 100;
bool sdAvailable = false;

enum ToolState {
  BOOT_SCREEN,
  MAIN_MENU,
  WIFI_SNIFFER,      // Menu 1
  BLE_SNIFFER,       // Menu 2
  ULTRASONIC,        // Menu 3
  MAPPER,            // Menu 4
  TRACKER_DETECTOR,  // Menu 5
  DEVICE_DETAIL,     // Sub-screen (not in menu)
  HELP_SCREEN        // Sub-screen (not in menu)
};
ToolState currentTool = BOOT_SCREEN;
ToolState previousTool = MAIN_MENU;

bool needsRefresh = true;
int scrollOffset = 0;
int selectedIndex = 0;
int graphScrollOffset = 0;
int maxVisibleItems = 5;

bool autoScanEnabled = true;
bool scanPaused = false;
unsigned long lastAutoScan = 0;

std::vector<DeviceInfo> wifiDevices;
std::vector<DeviceInfo> bleDevices;
std::map<String, unsigned long> knownDevices;

DeviceInfo* selectedDevice = nullptr;
bool viewingWifi = true;

BLEScan* pBLEScan = nullptr;

double vReal[SAMPLES];
double vImag[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLE_RATE);

#define MAPPER_GRID_SIZE 5
int mapperGrid[MAPPER_GRID_SIZE][MAPPER_GRID_SIZE];
std::vector<String> mapperDevices[MAPPER_GRID_SIZE][MAPPER_GRID_SIZE];
int mapperCursorX = 0;
int mapperCursorY = 0;
bool mapperUseWifi = true;
bool mapperShowDevices = false;

bool micInitialized = false;

// ============== OUI DATABASE ==============
String lookupVendor(String mac) {
  if (!sdAvailable) return "";

  mac.toUpperCase();
  String prefix = "";
  for (int i = 0; i < mac.length() && prefix.length() < 6; i++) {
    if (mac[i] != ':' && mac[i] != '-') {
      prefix += mac[i];
    }
  }

  File ouiFile = SD.open(OUI_FILE, FILE_READ);
  if (!ouiFile) return "";

  String result = "";
  while (ouiFile.available()) {
    String line = ouiFile.readStringUntil('\n');
    line.trim();
    int pipeIdx = line.indexOf('|');
    if (pipeIdx > 0) {
      String filePrefix = line.substring(0, pipeIdx);
      filePrefix.toUpperCase();
      if (filePrefix == prefix) {
        result = line.substring(pipeIdx + 1);
        break;
      }
    }
  }
  ouiFile.close();
  return result;
}

// ============== AUDIO FEEDBACK ==============
void playClickTone() {
  M5Cardputer.Speaker.tone(1000, 30);
}

void playAlertTone(int type) {
  switch(type) {
    case 1:
      M5Cardputer.Speaker.tone(1500, 100);
      break;
    case 2:
      for (int i = 0; i < 2; i++) {
        M5Cardputer.Speaker.tone(2000, 80);
        delay(100);
      }
      break;
    case 3:
      for (int i = 0; i < 3; i++) {
        M5Cardputer.Speaker.tone(2500, 100);
        delay(80);
        M5Cardputer.Speaker.tone(1800, 100);
        delay(80);
      }
      break;
  }
}

// ============== SD CARD ==============
void initSD() {
  sdAvailable = SD.begin(SD_CS_PIN);
}

// ============== THEME HELPERS ==============
void drawFooter() {
  #if SHOW_FOOTER_PATTERN
    M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
    M5Cardputer.Display.setCursor(0, 127);
    M5Cardputer.Display.print("/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\");
  #endif
}

void drawSectionTitle(const char* title, int y) {
  M5Cardputer.Display.setTextColor(COLOR_TITLE);
  int len = strlen(title);
  int x = (240 - (len * 6)) / 2;  // Center the title
  M5Cardputer.Display.setCursor(x - 18, y);
  M5Cardputer.Display.print("=== ");
  M5Cardputer.Display.print(title);
  M5Cardputer.Display.print(" ===");
}

// ============== STATUS BAR ==============
void drawStatusBar() {
  M5Cardputer.Display.fillRect(0, 0, 240, 14, COLOR_STATUSBAR);
  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setCursor(2, 3);
  #if SHOW_FOOTER_PATTERN
    M5Cardputer.Display.print("^ ");
  #endif
  M5Cardputer.Display.print(APP_NAME);

  if (sdAvailable) {
    M5Cardputer.Display.setTextColor(COLOR_HIGHLIGHT);
    M5Cardputer.Display.setCursor(110, 3);
    M5Cardputer.Display.print("SD");
  }

  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setCursor(200, 3);
  M5Cardputer.Display.printf("%d%%", batteryPercent);
}

// ============== BOOT SCREEN ==============
void drawBootScreen() {
  M5Cardputer.Display.fillScreen(COLOR_BACKGROUND);

  for (int i = 0; i < 3; i++) {
    M5Cardputer.Display.drawRect(i*2, i*2, 240 - i*4, 135 - i*4, COLOR_TITLE);
  }

  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(COLOR_TITLE);
  M5Cardputer.Display.setCursor(40, 15);
  M5Cardputer.Display.print("SIGNAL");
  M5Cardputer.Display.setTextColor(TFT_MAGENTA);
  M5Cardputer.Display.print(" RF");

  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setCursor(45, 38);
  M5Cardputer.Display.print("RF Analysis Multi-Tool");

  M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
  M5Cardputer.Display.setCursor(90, 50);
  M5Cardputer.Display.printf("v%s", APP_VERSION);

  #ifdef THEME_SASQUATCH
    M5Cardputer.Display.setTextColor(COLOR_ACCENT);
    M5Cardputer.Display.setCursor(45, 62);
    M5Cardputer.Display.print("--- SASQUATCH EDITION ---");
  #endif

  M5Cardputer.Display.drawRect(40, 78, 160, 10, COLOR_TITLE);
  for (int i = 0; i <= 156; i += 4) {
    M5Cardputer.Display.fillRect(42, 80, i, 6, COLOR_TITLE);
    delay(8);
  }

  M5Cardputer.Display.setTextColor(COLOR_ACCENT);
  M5Cardputer.Display.setCursor(60, 95);
  M5Cardputer.Display.print(THEME_AUTHOR);

  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setCursor(50, 115);
  M5Cardputer.Display.print("Press any key to continue");

  #if SHOW_FOOTER_PATTERN
    drawFooter();
  #endif

  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isPressed()) {
      playClickTone();
      delay(100);
      break;
    }
    delay(50);
  }
}

// ============== DEVICE DETAIL VIEW ==============
void drawDeviceDetail() {
  if (!selectedDevice) {
    currentTool = previousTool;
    needsRefresh = true;
    return;
  }

  // Lazy vendor lookup - only when viewing details
  if (selectedDevice->vendor.length() == 0 && sdAvailable) {
    selectedDevice->vendor = lookupVendor(selectedDevice->mac);
  }

  M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);

  M5Cardputer.Display.setTextColor(TFT_CYAN);
  M5Cardputer.Display.setCursor(5, 16);

  String displayName = selectedDevice->name.length() > 0 ? selectedDevice->name : selectedDevice->mac;
  if (displayName.length() > 25) displayName = displayName.substring(0, 22) + "...";
  M5Cardputer.Display.print(displayName);

  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(5, 28);
  M5Cardputer.Display.print("MAC: ");
  M5Cardputer.Display.print(selectedDevice->mac);

  if (selectedDevice->vendor.length() > 0) {
    M5Cardputer.Display.setCursor(5, 40);
    M5Cardputer.Display.setTextColor(TFT_MAGENTA);
    M5Cardputer.Display.print("Vendor: ");
    String vendorDisplay = selectedDevice->vendor;
    if (vendorDisplay.length() > 20) vendorDisplay = vendorDisplay.substring(0, 18) + "..";
    M5Cardputer.Display.print(vendorDisplay);
  }

  M5Cardputer.Display.setCursor(160, 28);
  M5Cardputer.Display.setTextColor(TFT_GREEN);
  M5Cardputer.Display.printf("RSSI: %d", selectedDevice->rssi);

  int graphX = 10, graphY = 55, graphW = 220, graphH = 50;
  M5Cardputer.Display.drawRect(graphX, graphY, graphW, graphH, TFT_WHITE);

  // Draw RSSI scale on left
  M5Cardputer.Display.setTextColor(TFT_DARKGREY);
  M5Cardputer.Display.setCursor(graphX + 2, graphY + 2);
  M5Cardputer.Display.print("-30");
  M5Cardputer.Display.setCursor(graphX + 2, graphY + graphH - 10);
  M5Cardputer.Display.print("-100");

  int historySize = selectedDevice->rssiHistory.size();
  if (historySize > 0) {
    int maxPoints = graphW - 30;  // Leave room for scale
    int startIdx = 0;
    if (historySize > maxPoints) {
      startIdx = historySize - maxPoints;
    }

    // Draw dots instead of lines
    for (int i = 0; i < min(historySize - startIdx, maxPoints); i++) {
      int idx = startIdx + i;
      if (idx >= historySize) break;

      int rssi = selectedDevice->rssiHistory[idx];
      int x = graphX + 28 + i;
      int y = map(constrain(rssi, -100, -30), -100, -30, graphY + graphH - 4, graphY + 4);

      uint16_t color = TFT_RED;
      if (rssi > -50) color = TFT_GREEN;
      else if (rssi > -70) color = TFT_YELLOW;

      // Draw a dot (small filled circle)
      M5Cardputer.Display.fillCircle(x, y, 2, color);
    }

    // Show point count
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    M5Cardputer.Display.setCursor(graphX + graphW - 30, graphY + 2);
    M5Cardputer.Display.printf("%d pts", historySize);
  } else {
    M5Cardputer.Display.setTextColor(TFT_DARKGREY);
    M5Cardputer.Display.setCursor(graphX + 60, graphY + 20);
    M5Cardputer.Display.print("Press R to scan");
  }

  M5Cardputer.Display.setTextColor(TFT_DARKGREY);
  M5Cardputer.Display.setCursor(20, 120);
  M5Cardputer.Display.print("R:Scan+Plot  Bksp:Back");
}

// ============== WIFI SNIFFER ==============
void scanWifi() {
  int n = WiFi.scanNetworks();

  for (int i = 0; i < n && wifiDevices.size() < MAX_DEVICES; i++) {
    String mac = WiFi.BSSIDstr(i);
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    int channel = WiFi.channel(i);

    bool found = false;
    for (auto& dev : wifiDevices) {
      if (dev.mac == mac) {
        dev.rssi = rssi;
        dev.lastSeen = millis();
        dev.isNew = false;
        dev.rssiHistory.push_back(rssi);
        if (dev.rssiHistory.size() > RSSI_HISTORY_SIZE) dev.rssiHistory.pop_front();
        found = true;
        break;
      }
    }

    if (!found) {
      DeviceInfo newDev;
      newDev.mac = mac;
      newDev.name = ssid.length() > 0 ? ssid : "[Hidden]";
      newDev.vendor = "";  // Lookup lazily in detail view
      newDev.rssi = rssi;
      newDev.channel = channel;
      newDev.isNew = (knownDevices.count(mac) == 0);
      newDev.firstSeen = millis();
      newDev.lastSeen = millis();
      newDev.rssiHistory.push_back(rssi);

      if (newDev.isNew) {
        knownDevices[mac] = millis();
        playAlertTone(1);
      }
      wifiDevices.push_back(newDev);
    }
  }
  WiFi.scanDelete();
}

void drawWifiList() {
  M5Cardputer.Display.fillRect(0, 14, 240, 121, COLOR_BACKGROUND);
  drawSectionTitle("WiFi Scanner", 16);

  if (wifiDevices.empty()) {
    M5Cardputer.Display.setTextColor(COLOR_ACCENT);
    M5Cardputer.Display.setCursor(70, 60);
    M5Cardputer.Display.print(MSG_SCANNING);
    drawFooter();
    return;
  }

  int displayCount = min((int)wifiDevices.size() - scrollOffset, maxVisibleItems);

  for (int i = 0; i < displayCount; i++) {
    int idx = i + scrollOffset;
    if (idx >= (int)wifiDevices.size()) break;

    DeviceInfo& dev = wifiDevices[idx];
    int y = 30 + (i * 15);

    if (idx == selectedIndex) {
      M5Cardputer.Display.fillRect(0, y - 1, 240, 15, COLOR_SUBTLE);
      M5Cardputer.Display.setTextColor(COLOR_TEXT);
      M5Cardputer.Display.setCursor(2, y);
      M5Cardputer.Display.print(">");
    }

    uint16_t color = TFT_RED;
    if (dev.rssi > RSSI_STRONG_THRESHOLD) color = COLOR_HIGHLIGHT;
    else if (dev.rssi > RSSI_MEDIUM_THRESHOLD) color = COLOR_ACCENT;

    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.setCursor(10, y);

    String displayName = dev.name;
    if (displayName.length() > 12) displayName = displayName.substring(0, 10) + "..";
    M5Cardputer.Display.print(displayName);

    if (dev.isNew) {
      M5Cardputer.Display.setTextColor(COLOR_TITLE);
      M5Cardputer.Display.print("(N)");
    }

    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.setCursor(115, y);
    M5Cardputer.Display.print(dev.mac.substring(9));

    M5Cardputer.Display.setCursor(175, y);
    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.printf("%d", dev.rssi);
  }

  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setCursor(5, 108);
  M5Cardputer.Display.printf("%d/%d", selectedIndex + 1, (int)wifiDevices.size());

  M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
  M5Cardputer.Display.setCursor(30, 108);
  M5Cardputer.Display.print("W/S:Nav Enter:Details R:Rescan");

  drawFooter();
}

void runWifiSniffer() {
  // No auto-scan - manual only with 'R' key
  // Only redraw when needed
  if (needsRefresh) {
    needsRefresh = false;
    drawStatusBar();
    drawWifiList();
  }
}

// ============== BLE SNIFFER ==============
void scanBle() {
  if (!pBLEScan) return;

  BLEScanResults* results = pBLEScan->start(2, false);
  int n = results->getCount();

  for (int i = 0; i < n && bleDevices.size() < MAX_DEVICES; i++) {
    BLEAdvertisedDevice device = results->getDevice(i);
    String mac = device.getAddress().toString().c_str();
    mac.toUpperCase();
    String name = device.haveName() ? device.getName().c_str() : "";
    int rssi = device.getRSSI();

    bool found = false;
    for (auto& dev : bleDevices) {
      if (dev.mac == mac) {
        dev.rssi = rssi;
        dev.lastSeen = millis();
        dev.isNew = false;
        if (name.length() > 0 && dev.name.length() == 0) dev.name = name;
        dev.rssiHistory.push_back(rssi);
        if (dev.rssiHistory.size() > RSSI_HISTORY_SIZE) dev.rssiHistory.pop_front();
        found = true;
        break;
      }
    }

    if (!found) {
      DeviceInfo newDev;
      newDev.mac = mac;
      newDev.name = name;
      newDev.vendor = "";  // Lookup lazily in detail view
      newDev.rssi = rssi;
      newDev.channel = 0;
      newDev.isNew = (knownDevices.count(mac) == 0);
      newDev.firstSeen = millis();
      newDev.lastSeen = millis();
      newDev.rssiHistory.push_back(rssi);

      if (newDev.isNew) {
        knownDevices[mac] = millis();
        playAlertTone(1);
      }
      bleDevices.push_back(newDev);
    }
  }
  pBLEScan->clearResults();
}

void drawBleList() {
  M5Cardputer.Display.fillRect(0, 14, 240, 121, COLOR_BACKGROUND);
  drawSectionTitle("BLE Scanner", 16);

  if (bleDevices.empty()) {
    M5Cardputer.Display.setTextColor(COLOR_ACCENT);
    M5Cardputer.Display.setCursor(70, 60);
    M5Cardputer.Display.print(MSG_SCANNING);
    drawFooter();
    return;
  }

  int displayCount = min((int)bleDevices.size() - scrollOffset, maxVisibleItems);

  for (int i = 0; i < displayCount; i++) {
    int idx = i + scrollOffset;
    if (idx >= (int)bleDevices.size()) break;

    DeviceInfo& dev = bleDevices[idx];
    int y = 30 + (i * 15);

    if (idx == selectedIndex) {
      M5Cardputer.Display.fillRect(0, y - 1, 240, 15, COLOR_SUBTLE);
      M5Cardputer.Display.setTextColor(COLOR_TEXT);
      M5Cardputer.Display.setCursor(2, y);
      M5Cardputer.Display.print(">");
    }

    uint16_t color = TFT_RED;
    if (dev.rssi > RSSI_STRONG_THRESHOLD) color = COLOR_HIGHLIGHT;
    else if (dev.rssi > RSSI_MEDIUM_THRESHOLD) color = COLOR_ACCENT;

    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.setCursor(10, y);

    String displayName;
    if (dev.name.length() > 0) displayName = dev.name;
    else if (dev.vendor.length() > 0) displayName = dev.vendor;
    else displayName = dev.mac.substring(0, 8);
    if (displayName.length() > 12) displayName = displayName.substring(0, 10) + "..";
    M5Cardputer.Display.print(displayName);

    if (dev.isNew) {
      M5Cardputer.Display.setTextColor(COLOR_TITLE);
      M5Cardputer.Display.print("(N)");
    }

    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.setCursor(115, y);
    M5Cardputer.Display.print(dev.mac.substring(9));

    M5Cardputer.Display.setCursor(175, y);
    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.printf("%d", dev.rssi);
  }

  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setCursor(5, 108);
  M5Cardputer.Display.printf("%d/%d", selectedIndex + 1, (int)bleDevices.size());

  M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
  M5Cardputer.Display.setCursor(30, 108);
  M5Cardputer.Display.print("W/S:Nav Enter:Details R:Rescan");

  drawFooter();
}

void runBleSniffer() {
  // No auto-scan - manual only with 'R' key
  // Only redraw when needed
  if (needsRefresh) {
    needsRefresh = false;
    drawStatusBar();
    drawBleList();
  }
}

// ============== ULTRASONIC DETECTOR ==============
void initMic() {
  if (micInitialized) return;
  auto micCfg = M5Cardputer.Mic.config();
  micCfg.sample_rate = SAMPLE_RATE;
  micCfg.magnification = 16;
  M5Cardputer.Mic.config(micCfg);
  M5Cardputer.Mic.begin();
  micInitialized = true;
}

void runUltrasonic() {
  // Runs continuously - no needsRefresh check

  if (!M5Cardputer.Mic.isEnabled()) {
    if (needsRefresh) {
      needsRefresh = false;
      M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
      drawStatusBar();
      M5Cardputer.Display.setTextColor(TFT_RED);
      M5Cardputer.Display.setCursor(50, 60);
      M5Cardputer.Display.print("Mic not available!");
    }
    return;
  }

  // Record audio
  int16_t samples[SAMPLES];
  M5Cardputer.Mic.record(samples, SAMPLES);
  delay(20);

  // Prepare FFT
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (double)samples[i];
    vImag[i] = 0.0;
  }

  FFT.windowing(FFTWindow::Hamming, FFT_FORWARD);
  FFT.compute(FFT_FORWARD);
  FFT.complexToMagnitude();

  // Find peak frequency
  double maxMag = 0;
  int peakBin = 0;
  for (int i = 2; i < SAMPLES / 2; i++) {
    if (vReal[i] > maxMag) {
      maxMag = vReal[i];
      peakBin = i;
    }
  }
  float peakFreq = (float)peakBin * SAMPLE_RATE / SAMPLES;

  // Clear and draw
  M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
  drawStatusBar();

  M5Cardputer.Display.setTextColor(TFT_CYAN);
  M5Cardputer.Display.setCursor(35, 16);
  M5Cardputer.Display.print("=== Audio Frequency ===");

  // Show peak frequency
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(10, 30);
  M5Cardputer.Display.print("Peak: ");

  if (maxMag > 100) {
    M5Cardputer.Display.setTextColor(TFT_GREEN);
    M5Cardputer.Display.printf("%.0f Hz", peakFreq);
  } else {
    M5Cardputer.Display.setTextColor(TFT_DARKGREY);
    M5Cardputer.Display.print("---");
  }

  // Check if peak frequency is ultrasonic (before drawing bars)
  bool ultrasonicDetected = false;
  if (peakFreq > ULTRASONIC_THRESHOLD && maxMag > 500) {
    ultrasonicDetected = true;
  }

  // Draw spectrum bars
  int barWidth = 20, barSpacing = 3, startX = 15, maxHeight = 45, baseY = 100;

  for (int i = 0; i < FFT_BANDS; i++) {
    int startBin = (i * SAMPLES / 2) / FFT_BANDS;
    int endBin = ((i + 1) * SAMPLES / 2) / FFT_BANDS;

    double bandMag = 0;
    for (int j = startBin; j < endBin; j++) bandMag += vReal[j];
    bandMag /= (endBin - startBin);

    int barHeight = map(constrain((int)bandMag, 0, 3000), 0, 3000, 0, maxHeight);
    int x = startX + i * (barWidth + barSpacing);
    int y = baseY - barHeight;

    uint16_t barColor = TFT_GREEN;
    float freq = (float)(startBin + endBin) / 2.0 * SAMPLE_RATE / SAMPLES;

    if (freq > ULTRASONIC_THRESHOLD) {
      barColor = TFT_RED;
    } else if (freq > 10000) {
      barColor = TFT_YELLOW;
    }

    M5Cardputer.Display.fillRect(x, y, barWidth, barHeight, barColor);
    M5Cardputer.Display.drawRect(x, baseY - maxHeight, barWidth, maxHeight, TFT_DARKGREY);
  }

  // Frequency labels
  M5Cardputer.Display.setTextColor(TFT_DARKGREY);
  M5Cardputer.Display.setCursor(15, 103);
  M5Cardputer.Display.print("0");
  M5Cardputer.Display.setCursor(100, 103);
  M5Cardputer.Display.print("11k");
  M5Cardputer.Display.setCursor(195, 103);
  M5Cardputer.Display.print("22k");

  // Only show ULTRASONIC! when peak frequency is above threshold with strong signal
  if (ultrasonicDetected) {
    M5Cardputer.Display.setTextColor(TFT_RED);
    M5Cardputer.Display.setCursor(140, 30);
    M5Cardputer.Display.print("ULTRASONIC!");
    // Play alert tone
    M5Cardputer.Speaker.tone(2000, 50);
  }

  M5Cardputer.Display.setTextColor(TFT_DARKGREY);
  M5Cardputer.Display.setCursor(30, 118);
  M5Cardputer.Display.print("T:Test1kHz Bksp:Back");
}

// ============== SIGNAL MAPPER ==============
void initMapper() {
  for (int y = 0; y < MAPPER_GRID_SIZE; y++) {
    for (int x = 0; x < MAPPER_GRID_SIZE; x++) {
      mapperGrid[y][x] = -100;
      mapperDevices[y][x].clear();
    }
  }
  mapperCursorX = 0;
  mapperCursorY = 0;
  mapperShowDevices = false;
}

void runMapper() {
  if (!needsRefresh) return;
  needsRefresh = false;

  M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
  drawStatusBar();

  M5Cardputer.Display.setTextColor(TFT_CYAN);
  M5Cardputer.Display.setCursor(10, 16);
  M5Cardputer.Display.printf("=== Mapper [%s] ===", mapperUseWifi ? "WiFi" : "BLE");

  if (mapperShowDevices) {
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    M5Cardputer.Display.setCursor(5, 30);
    M5Cardputer.Display.printf("Devices at %d,%d:", mapperCursorX, mapperCursorY);

    std::vector<String>& devices = mapperDevices[mapperCursorY][mapperCursorX];
    int y = 44;
    for (int i = 0; i < min((int)devices.size(), 6); i++) {
      M5Cardputer.Display.setCursor(10, y);
      M5Cardputer.Display.print(devices[i].substring(0, 30));
      y += 12;
    }

    if (devices.empty()) {
      M5Cardputer.Display.setCursor(10, 50);
      M5Cardputer.Display.setTextColor(TFT_DARKGREY);
      M5Cardputer.Display.print("No devices sampled");
    }

    M5Cardputer.Display.setTextColor(TFT_DARKGREY);
    M5Cardputer.Display.setCursor(10, 120);
    M5Cardputer.Display.print("L:Back to grid");
    return;
  }

  int cellSize = 18, gridStartX = 10, gridStartY = 32;

  for (int y = 0; y < MAPPER_GRID_SIZE; y++) {
    for (int x = 0; x < MAPPER_GRID_SIZE; x++) {
      int rssi = mapperGrid[y][x];
      uint16_t cellColor = TFT_DARKGREY;
      if (rssi > -100) {
        if (rssi > -50) cellColor = TFT_GREEN;
        else if (rssi > -70) cellColor = TFT_YELLOW;
        else if (rssi > -85) cellColor = TFT_ORANGE;
        else cellColor = TFT_RED;
      }

      int px = gridStartX + x * cellSize;
      int py = gridStartY + y * cellSize;

      M5Cardputer.Display.fillRect(px, py, cellSize - 2, cellSize - 2, cellColor);

      if (!mapperDevices[y][x].empty()) {
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        M5Cardputer.Display.setCursor(px + 4, py + 4);
        M5Cardputer.Display.printf("%d", (int)mapperDevices[y][x].size());
      }

      if (x == mapperCursorX && y == mapperCursorY) {
        M5Cardputer.Display.drawRect(px - 1, py - 1, cellSize, cellSize, TFT_WHITE);
      }
    }
  }

  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(110, 34);
  M5Cardputer.Display.printf("Pos: %d,%d", mapperCursorX, mapperCursorY);
  M5Cardputer.Display.setCursor(110, 46);
  M5Cardputer.Display.printf("RSSI: %d", mapperGrid[mapperCursorY][mapperCursorX]);

  M5Cardputer.Display.setTextColor(TFT_DARKGREY);
  M5Cardputer.Display.setCursor(5, 120);
  M5Cardputer.Display.print("WASD:Move Spc:Sample L:List Q:Toggle");
}

void sampleMapperPoint() {
  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.setCursor(110, 100);
  M5Cardputer.Display.print("Sampling...");

  mapperDevices[mapperCursorY][mapperCursorX].clear();
  int rssi = -100;

  if (mapperUseWifi) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < min(n, 10); i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) ssid = "[Hidden]";
      String info = ssid + " " + String(WiFi.RSSI(i)) + "dBm";
      mapperDevices[mapperCursorY][mapperCursorX].push_back(info);
      if (i == 0) rssi = WiFi.RSSI(i);
    }
    WiFi.scanDelete();
  } else if (pBLEScan) {
    BLEScanResults* results = pBLEScan->start(2, false);
    int n = results->getCount();
    for (int i = 0; i < min(n, 10); i++) {
      BLEAdvertisedDevice dev = results->getDevice(i);
      String name = dev.haveName() ? dev.getName().c_str() : dev.getAddress().toString().c_str();
      String info = name + " " + String(dev.getRSSI()) + "dBm";
      mapperDevices[mapperCursorY][mapperCursorX].push_back(info);
      if (i == 0) rssi = dev.getRSSI();
    }
    pBLEScan->clearResults();
  }

  mapperGrid[mapperCursorY][mapperCursorX] = rssi;
  playClickTone();
  needsRefresh = true;
}

// ============== TRACKER DETECTOR ==============
unsigned long lastTrackerScan = 0;
#define TRACKER_SCAN_INTERVAL 8000
bool trackerShowAllDevices = false;
std::vector<DeviceInfo> trackerBleDevices;

void drawTrackerResults() {
  M5Cardputer.Display.fillRect(0, 14, 240, 121, COLOR_BACKGROUND);
  drawStatusBar();

  int trackerCount = 0;
  for (auto& dev : trackerBleDevices) {
    if (dev.isNew) trackerCount++;
  }

  if (trackerShowAllDevices) {
    // Show all BLE devices
    drawSectionTitle("All BLE Devices", 16);

    int y = 30;
    for (int i = scrollOffset; i < min((int)trackerBleDevices.size(), scrollOffset + 5); i++) {
      DeviceInfo& dev = trackerBleDevices[i];

      // Lazy vendor lookup for visible devices
      if (dev.vendor.length() == 0 && sdAvailable) {
        dev.vendor = lookupVendor(dev.mac);
      }

      uint16_t color = dev.isNew ? TFT_RED : COLOR_TEXT;
      M5Cardputer.Display.setTextColor(color);
      M5Cardputer.Display.setCursor(5, y);

      String displayName = dev.name.length() > 0 ? dev.name : dev.mac.substring(0, 11);
      if (displayName.length() > 15) displayName = displayName.substring(0, 13) + "..";
      M5Cardputer.Display.print(displayName);

      M5Cardputer.Display.setCursor(115, y);
      M5Cardputer.Display.printf("%d", dev.rssi);

      if (dev.vendor.length() > 0) {
        M5Cardputer.Display.setTextColor(TFT_MAGENTA);
        M5Cardputer.Display.setCursor(145, y);
        String vendorShort = dev.vendor.substring(0, 10);
        M5Cardputer.Display.print(vendorShort);
      }

      y += 14;
    }

    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.setCursor(5, 108);
    M5Cardputer.Display.printf("%d devices", (int)trackerBleDevices.size());

    M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
    M5Cardputer.Display.setCursor(80, 108);
    M5Cardputer.Display.print("W/S:Scroll L:Trackers");
  } else {
    // Show tracker detection mode
    drawSectionTitle("Tracker Detector", 16);

    if (trackerCount == 0) {
      M5Cardputer.Display.setTextColor(COLOR_HIGHLIGHT);
      M5Cardputer.Display.setCursor(70, 50);
      M5Cardputer.Display.print(MSG_NO_TRACKERS);
    } else {
      M5Cardputer.Display.setTextColor(TFT_RED);
      M5Cardputer.Display.setCursor(60, 32);
      M5Cardputer.Display.print(MSG_TRACKERS_FOUND);

      int y = 48;
      for (auto& dev : trackerBleDevices) {
        if (dev.isNew && y < 85) {
          M5Cardputer.Display.setTextColor(TFT_RED);
          M5Cardputer.Display.setCursor(10, y);
          String label = dev.vendor.length() > 0 ? dev.vendor : dev.name;
          M5Cardputer.Display.printf("! %s %ddBm", label.c_str(), dev.rssi);
          y += 14;
        }
      }
    }

    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.setCursor(10, 92);
    M5Cardputer.Display.printf("BLE nearby: %d  Trackers: %d", (int)trackerBleDevices.size(), trackerCount);

    M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
    M5Cardputer.Display.setCursor(10, 108);
    M5Cardputer.Display.print("R:Rescan L:All devices H:Help");
  }

  drawFooter();
}

void scanTrackerDevices() {
  if (!pBLEScan) return;

  // Show scanning message
  M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
  drawStatusBar();
  M5Cardputer.Display.setTextColor(TFT_CYAN);
  M5Cardputer.Display.setCursor(40, 16);
  M5Cardputer.Display.print("=== Tracker Detector ===");
  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.setCursor(70, 60);
  M5Cardputer.Display.print("Scanning...");

  BLEScanResults* results = pBLEScan->start(3, false);
  int n = results->getCount();

  trackerBleDevices.clear();

  for (int i = 0; i < n; i++) {
    BLEAdvertisedDevice device = results->getDevice(i);
    String mac = device.getAddress().toString().c_str();
    mac.toUpperCase();
    int rssi = device.getRSSI();
    String name = device.haveName() ? device.getName().c_str() : "";

    DeviceInfo dev;
    dev.mac = mac;
    dev.name = name;
    dev.vendor = "";  // Lookup lazily when viewing device list
    dev.rssi = rssi;

    // Detect trackers by name (faster than OUI lookup)
    bool isTracker = false;
    if (name.indexOf("AirTag") >= 0) isTracker = true;
    if (name.indexOf("Tile") >= 0) isTracker = true;
    if (name.indexOf("SmartTag") >= 0) isTracker = true;
    // Apple devices often have specific manufacturer data - check MAC prefix for Apple
    String macPrefix = mac.substring(0, 8);
    macPrefix.replace(":", "");
    // Common Apple OUI prefixes (for AirTags)
    if (macPrefix.startsWith("00") || macPrefix.startsWith("AC") ||
        macPrefix.startsWith("F0") || macPrefix.startsWith("DC")) {
      // Could be Apple - mark as potential tracker if strong signal and no name
      if (name.length() == 0 && rssi > -60) {
        isTracker = true;
      }
    }

    dev.isNew = isTracker;
    trackerBleDevices.push_back(dev);
  }

  pBLEScan->clearResults();
  lastTrackerScan = millis();
}

void runTrackerDetector() {
  // Auto-scan every 8 seconds
  if (millis() - lastTrackerScan > TRACKER_SCAN_INTERVAL) {
    scanTrackerDevices();
    needsRefresh = true;
  }

  // Only redraw when needed
  if (needsRefresh) {
    needsRefresh = false;
    drawTrackerResults();
  }
}

void showTrackerHelp() {
  M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_CYAN);
  M5Cardputer.Display.setCursor(90, 18);
  M5Cardputer.Display.print("-- HELP --");

  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(5, 35);
  M5Cardputer.Display.println("Tracker Detector scans for:");
  M5Cardputer.Display.println("");
  M5Cardputer.Display.println("  - Apple AirTags");
  M5Cardputer.Display.println("  - Tile trackers");
  M5Cardputer.Display.println("  - Samsung SmartTags");
  M5Cardputer.Display.println("");
  M5Cardputer.Display.println("Uses OUI database on SD card");

  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.setCursor(50, 120);
  M5Cardputer.Display.print("Press any key...");
}

// ============== MAIN MENU ==============
void drawMainMenu() {
  M5Cardputer.Display.fillScreen(COLOR_BACKGROUND);
  drawStatusBar();

  M5Cardputer.Display.setTextSize(1);
  drawSectionTitle(MSG_MENU_TITLE, 18);

  const char* menuItems[] = {
    "1: WiFi Scanner",
    "2: BLE Scanner",
    "3: Ultrasonic Detector",
    "4: Signal Mapper",
    "5: Tracker Detector"
  };

  for (int i = 0; i < 5; i++) {
    M5Cardputer.Display.setCursor(30, 40 + i * 16);
    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.print(menuItems[i]);
  }

  M5Cardputer.Display.setTextColor(COLOR_SUBTLE);
  M5Cardputer.Display.setCursor(60, 118);
  M5Cardputer.Display.print("Press 1-5 to select");

  drawFooter();
}

// ============== KEYBOARD HANDLING ==============
void handleKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

  Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
  char key = 0;

  if (status.word.size() > 0) key = status.word[0];

  bool backspacePressed = status.del;
  bool enterPressed = status.enter;

  if (backspacePressed && currentTool != MAIN_MENU) {
    if (currentTool == DEVICE_DETAIL) {
      currentTool = previousTool;
      scanPaused = false;
      graphScrollOffset = 0;
    } else {
      // Stop mic if leaving ultrasonic
      if (currentTool == ULTRASONIC && micInitialized) {
        M5Cardputer.Mic.end();
        micInitialized = false;
      }
      currentTool = MAIN_MENU;
      drawMainMenu();
    }
    playClickTone();
    scrollOffset = 0;
    selectedIndex = 0;
    trackerShowAllDevices = false;
    needsRefresh = true;
    return;
  }

  if (currentTool == HELP_SCREEN) {
    currentTool = TRACKER_DETECTOR;
    needsRefresh = true;
    return;
  }

  if (currentTool == MAIN_MENU) {
    if (key >= '1' && key <= '5') {
      playClickTone();
      currentTool = (ToolState)(key - '1' + WIFI_SNIFFER);
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      needsRefresh = true;
      scrollOffset = 0;
      selectedIndex = 0;
      scanPaused = false;
      lastAutoScan = 0;

      if (currentTool == WIFI_SNIFFER) {
        wifiDevices.clear();
        viewingWifi = true;
        // Draw scanning message first
        drawStatusBar();
        M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
        M5Cardputer.Display.setTextColor(TFT_CYAN);
        M5Cardputer.Display.setCursor(60, 16);
        M5Cardputer.Display.print("=== WiFi Scanner ===");
        M5Cardputer.Display.setTextColor(TFT_YELLOW);
        M5Cardputer.Display.setCursor(70, 60);
        M5Cardputer.Display.print("Scanning...");
        // Now scan
        scanWifi();
        lastAutoScan = millis();
      } else if (currentTool == BLE_SNIFFER) {
        bleDevices.clear();
        viewingWifi = false;
        // Draw scanning message first
        drawStatusBar();
        M5Cardputer.Display.fillRect(0, 14, 240, 121, TFT_BLACK);
        M5Cardputer.Display.setTextColor(TFT_CYAN);
        M5Cardputer.Display.setCursor(60, 16);
        M5Cardputer.Display.print("=== BLE Scanner ===");
        M5Cardputer.Display.setTextColor(TFT_YELLOW);
        M5Cardputer.Display.setCursor(70, 60);
        M5Cardputer.Display.print("Scanning...");
        // Now scan
        scanBle();
        lastAutoScan = millis();
      } else if (currentTool == ULTRASONIC) {
        initMic();
      } else if (currentTool == MAPPER) {
        initMapper();
      } else if (currentTool == TRACKER_DETECTOR) {
        trackerBleDevices.clear();
        trackerShowAllDevices = false;
        scanTrackerDevices();
      }
    }
    return;
  }

  if (currentTool == DEVICE_DETAIL) {
    if (key == 'r' || key == 'R') {
      // Show scanning indicator
      M5Cardputer.Display.setTextColor(TFT_YELLOW);
      M5Cardputer.Display.setCursor(100, 70);
      M5Cardputer.Display.print("Scanning...");

      // Scan and update this device's RSSI
      if (viewingWifi) {
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; i++) {
          if (WiFi.BSSIDstr(i) == selectedDevice->mac) {
            selectedDevice->rssi = WiFi.RSSI(i);
            selectedDevice->rssiHistory.push_back(selectedDevice->rssi);
            if (selectedDevice->rssiHistory.size() > RSSI_HISTORY_SIZE)
              selectedDevice->rssiHistory.pop_front();
            break;
          }
        }
        WiFi.scanDelete();
      } else {
        if (pBLEScan) {
          BLEScanResults* results = pBLEScan->start(2, false);
          int n = results->getCount();
          for (int i = 0; i < n; i++) {
            BLEAdvertisedDevice dev = results->getDevice(i);
            String mac = dev.getAddress().toString().c_str();
            mac.toUpperCase();
            if (mac == selectedDevice->mac) {
              selectedDevice->rssi = dev.getRSSI();
              selectedDevice->rssiHistory.push_back(selectedDevice->rssi);
              if (selectedDevice->rssiHistory.size() > RSSI_HISTORY_SIZE)
                selectedDevice->rssiHistory.pop_front();
              break;
            }
          }
          pBLEScan->clearResults();
        }
      }
      needsRefresh = true;
      playClickTone();
    }
    if (needsRefresh) { drawStatusBar(); drawDeviceDetail(); needsRefresh = false; }
    return;
  }

  if (currentTool == WIFI_SNIFFER || currentTool == BLE_SNIFFER) {
    std::vector<DeviceInfo>& devices = (currentTool == WIFI_SNIFFER) ? wifiDevices : bleDevices;

    if (key == 'w' || key == 'W') {
      if (selectedIndex > 0) {
        selectedIndex--;
        if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
        needsRefresh = true; playClickTone();
      }
    }
    if (key == 's' || key == 'S') {
      if (selectedIndex < (int)devices.size() - 1) {
        selectedIndex++;
        if (selectedIndex >= scrollOffset + maxVisibleItems) scrollOffset = selectedIndex - maxVisibleItems + 1;
        needsRefresh = true; playClickTone();
      }
    }

    if (enterPressed && !devices.empty() && selectedIndex < (int)devices.size()) {
      previousTool = currentTool;
      selectedDevice = &devices[selectedIndex];
      currentTool = DEVICE_DETAIL;
      graphScrollOffset = 0;
      needsRefresh = true;
      playClickTone();
      drawStatusBar();
      drawDeviceDetail();
      return;
    }

    if (key == 'r' || key == 'R') {
      // Show scanning indicator
      M5Cardputer.Display.setTextColor(TFT_YELLOW);
      M5Cardputer.Display.setCursor(180, 16);
      M5Cardputer.Display.print("...");
      if (currentTool == WIFI_SNIFFER) scanWifi(); else scanBle();
      needsRefresh = true; playClickTone();
    }
    return;
  }

  if (currentTool == ULTRASONIC) {
    if (key == 'r' || key == 'R') { needsRefresh = true; playClickTone(); }
    if (key == 't' || key == 'T') { M5Cardputer.Speaker.tone(1000, 500); }
    return;
  }

  if (currentTool == MAPPER) {
    if (mapperShowDevices) {
      if (key == 'l' || key == 'L' || backspacePressed) {
        mapperShowDevices = false; needsRefresh = true; playClickTone();
      }
      return;
    }

    if (key == 'w' || key == 'W') { if (mapperCursorY > 0) { mapperCursorY--; needsRefresh = true; playClickTone(); } }
    if (key == 's' || key == 'S') { if (mapperCursorY < MAPPER_GRID_SIZE - 1) { mapperCursorY++; needsRefresh = true; playClickTone(); } }
    if (key == 'a' || key == 'A') { if (mapperCursorX > 0) { mapperCursorX--; needsRefresh = true; playClickTone(); } }
    if (key == 'd' || key == 'D') { if (mapperCursorX < MAPPER_GRID_SIZE - 1) { mapperCursorX++; needsRefresh = true; playClickTone(); } }
    if (key == ' ' || enterPressed) { sampleMapperPoint(); }
    if (key == 'q' || key == 'Q') { mapperUseWifi = !mapperUseWifi; needsRefresh = true; playClickTone(); }
    if (key == 'l' || key == 'L') { mapperShowDevices = true; needsRefresh = true; playClickTone(); }
    return;
  }

  if (currentTool == TRACKER_DETECTOR) {
    if (key == 'r' || key == 'R') { scanTrackerDevices(); playClickTone(); }
    if (key == 'h' || key == 'H') { currentTool = HELP_SCREEN; showTrackerHelp(); }
    if (key == 'l' || key == 'L') {
      trackerShowAllDevices = !trackerShowAllDevices;
      scrollOffset = 0;
      playClickTone();
    }
    if (trackerShowAllDevices) {
      if (key == 'w' || key == 'W') {
        if (scrollOffset > 0) { scrollOffset--; playClickTone(); }
      }
      if (key == 's' || key == 'S') {
        if (scrollOffset < (int)trackerBleDevices.size() - 5) { scrollOffset++; playClickTone(); }
      }
    }
    return;
  }
}

// ============== SETUP ==============
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Speaker.setVolume(80);

  initSD();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  drawBootScreen();

  currentTool = MAIN_MENU;
  drawMainMenu();
}

// ============== MAIN LOOP ==============
void loop() {
  M5Cardputer.update();
  handleKeyboard();

  switch (currentTool) {
    case MAIN_MENU:
    case HELP_SCREEN:
      delay(50);
      break;
    case WIFI_SNIFFER:
      runWifiSniffer();
      delay(50);
      break;
    case BLE_SNIFFER:
      runBleSniffer();
      delay(50);
      break;
    case DEVICE_DETAIL:
      delay(50);
      break;
    case ULTRASONIC:
      runUltrasonic();
      delay(100);
      break;
    case MAPPER:
      runMapper();
      delay(50);
      break;
    case TRACKER_DETECTOR:
      runTrackerDetector();
      delay(50);
      break;
    default:
      break;
  }
}
