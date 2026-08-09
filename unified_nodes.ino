/*
  UNIFIED NODE FIRMWARE v3 -- auto-discovery mesh, no mic dependency
  Flash this exact file to every board. Nodes identified by their own MAC
  address (auto-discovery, no manual ID assignment). Every node listens to
  every other node's broadcasts, building a full pairwise RSSI matrix.
  The AP host collects and serves that matrix as JSON; the dashboard's
  browser JS does relative-position (MDS) math and renders the radar.

  Set IS_AP_HOST = true on exactly ONE board.

  help_detected/confidence/amplitude fields are placeholders (always 0/false)
  until real sensing (mic + model, or anything else) is wired back in --
  the mesh/positioning pipeline doesn't depend on them.
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <map>

// ==================== CONFIG ====================
const bool IS_AP_HOST = false;   // true on exactly ONE board
const char* AP_SSID = "helpnet";
const uint8_t AP_CHANNEL = 1;
const int MAX_PEERS_PER_NODE = 20;

struct __attribute__((packed)) PeerReading {
  uint8_t mac[6];
  int8_t rssi;
};
struct __attribute__((packed)) NodePacket {
  uint8_t mac[6];
  bool help_detected;
  float confidence;
  float amplitude;
  uint8_t peer_count;
  PeerReading peers[MAX_PEERS_PER_NODE];
};

uint8_t broadcastAddr[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t myMac[6];

struct HeardPeer { uint8_t mac[6]; int8_t rssi; unsigned long lastHeard; };
HeardPeer heardPeers[MAX_PEERS_PER_NODE];
int heardPeerCount = 0;

void recordHeardPeer(const uint8_t* mac, int8_t rssi) {
  for (int i = 0; i < heardPeerCount; i++) {
    if (memcmp(heardPeers[i].mac, mac, 6) == 0) {
      heardPeers[i].rssi = rssi; heardPeers[i].lastHeard = millis();
      return;
    }
  }
  if (heardPeerCount < MAX_PEERS_PER_NODE) {
    memcpy(heardPeers[heardPeerCount].mac, mac, 6);
    heardPeers[heardPeerCount].rssi = rssi;
    heardPeers[heardPeerCount].lastHeard = millis();
    heardPeerCount++;
  }
}

// ==================== HOST-SIDE STATE ====================
WebServer server(80);

struct NodeState {
  bool help_detected=false; float confidence=0, amplitude=0;
  unsigned long last_seen=0; bool ever_seen=false; bool is_anchor=false;
  HeardPeer peers[MAX_PEERS_PER_NODE]; int peerCount=0;
};
std::map<String, NodeState> knownNodes;

String macToStr(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

// Updates (or creates) a node's entry in knownNodes, then auto-registers any
// of its peers we don't already have a top-level entry for as stub "anchor"
// nodes. This is what lets an ambient WiFi router -- or the AP host itself,
// which can't hear its own broadcast -- show up as a point for MDS even
// though neither of them ever sends us a NodePacket of their own.
//
// isAnchor is always assigned here, not just set-when-true: a node can get
// stub-registered as an anchor (isAnchor=true, below) before we ever hear
// its own broadcast directly -- e.g. another node reports it as a peer
// first. Once we DO hear that node's own NodePacket, this call always
// passes isAnchor=false, and that has to overwrite the earlier stub flag,
// or the node stays permanently marked as an anchor -- unselectable in the
// dashboard picker even though it is a real, addressable node.
void updateNodeState(const uint8_t* mac, bool help, float conf, float amp,
                      const HeardPeer* peers, int peerCount, bool isAnchor=false) {
  String id = macToStr(mac);
  NodeState &n = knownNodes[id];
  n.help_detected = help;
  n.confidence = conf;
  n.amplitude = amp;
  n.last_seen = millis();
  n.ever_seen = true;
  n.is_anchor = isAnchor;
  n.peerCount = min(peerCount, MAX_PEERS_PER_NODE);
  for (int i = 0; i < n.peerCount; i++) {
    memcpy(n.peers[i].mac, peers[i].mac, 6);
    n.peers[i].rssi = peers[i].rssi;
  }
  for (int i = 0; i < n.peerCount; i++) {
    String peerId = macToStr(n.peers[i].mac);
    if (knownNodes.find(peerId) == knownNodes.end()) {
      NodeState &stub = knownNodes[peerId];
      stub.ever_seen = true;
      stub.is_anchor = true;
      stub.last_seen = millis();
    }
  }
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(NodePacket)) return;
  NodePacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  int rssi = info->rx_ctrl->rssi;
  recordHeardPeer(pkt.mac, rssi);

  if (!IS_AP_HOST) return;

  int n = min((int)pkt.peer_count, MAX_PEERS_PER_NODE);
  HeardPeer converted[MAX_PEERS_PER_NODE];
  for (int i = 0; i < n; i++) {
    memcpy(converted[i].mac, pkt.peers[i].mac, 6);
    converted[i].rssi = pkt.peers[i].rssi;
  }
  Serial.printf("[%s] direct RSSI to host=%d, reports %d peer readings\n", macToStr(pkt.mac).c_str(), rssi, n);
  updateNodeState(pkt.mac, pkt.help_detected, pkt.confidence, pkt.amplitude, converted, n);
}

void handleState() {
  String json = "{\"nodes\":{";
  bool first = true;
  for (auto &kv : knownNodes) {
    if (!first) json += ","; first = false;
    NodeState &n = kv.second;
    json += "\"" + kv.first + "\":{";
    json += "\"help_detected\":" + String(n.help_detected?"true":"false") + ",";
    json += "\"amplitude\":" + String(n.amplitude,3) + ",";
    json += "\"is_anchor\":" + String(n.is_anchor?"true":"false") + ",";
    json += "\"peers\":[";
    for (int i = 0; i < n.peerCount; i++) {
      if (i) json += ",";
      json += "{\"mac\":\"" + macToStr(n.peers[i].mac) + "\",\"rssi\":" + String(n.peers[i].rssi) + "}";
    }
    json += "]}";
  }
  json += "}}";
  server.send(200, "application/json", json);
}

#include "dashboard_html.h"  // keeps the embedded JS out of ctags' reach, see comment in that file

void setupApHost() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL);
  Serial.print("Join WiFi '"); Serial.print(AP_SSID); Serial.println("' then browse http://192.168.4.1/");
  Serial.print("Host actual WiFi channel: "); Serial.println(WiFi.channel());
  esp_now_init();
  esp_now_register_recv_cb(onDataRecv);
  server.on("/", [](){ server.send(200,"text/html", DASHBOARD_HTML); });
  server.on("/state.json", handleState);
  server.begin();
}

// ==================== AMBIENT WIFI ANCHOR ====================
// Stand-in for missing physical nodes: scan for nearby WiFi routers and
// treat each one's BSSID as a shared, stationary reference point. As long
// as 2+ of your boards can see the SAME router, its BSSID becomes a free
// anchor node for MDS to triangulate against.
//
// AMBIENT_MAX_ANCHORS is the knob to turn down if ambient routers are
// crowding out real nodes on the map or the layout won't settle -- each
// board scans independently, so with several boards all reporting several
// APs apiece (and only partial overlap in which APs any two boards share),
// the anchor count multiplies fast and adds more shifting than it fixes.
// With up to 7 real nodes clustered close together (tabletop/room demo),
// 1-2 strong shared anchors give MDS an orientation reference without
// drowning out the real nodes -- raise this again later if boards spread
// out enough that a couple of anchors no longer overlap between them.
const bool ENABLE_AMBIENT_ANCHOR = true;
const unsigned long AMBIENT_SCAN_INTERVAL_MS = 15000; // scanning briefly steals the radio from ESP-NOW -- keep this infrequent
const int AMBIENT_MAX_ANCHORS = 3;    // record up to this many ambient APs per scan -- see note above
const int8_t AMBIENT_MIN_RSSI = -70;  // ignore APs weaker than this -- raised from -85 so only strong, stable APs qualify
unsigned long lastAmbientScan = 0;

void scanAmbientAnchor() {
  int found = WiFi.scanNetworks(false /*async*/, false /*show hidden*/);
  int cnt = min(found, 64);
  int order[64];
  for (int i = 0; i < cnt; i++) order[i] = i;
  // simple selection sort by RSSI descending -- cnt is always small
  for (int i = 0; i < cnt; i++) {
    int best = i;
    for (int j = i + 1; j < cnt; j++) if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[best])) best = j;
    int t = order[i]; order[i] = order[best]; order[best] = t;
  }
  int added = 0;
  for (int k = 0; k < cnt && added < AMBIENT_MAX_ANCHORS; k++) {
    int i = order[k];
    int32_t rssi = WiFi.RSSI(i);
    if (rssi < AMBIENT_MIN_RSSI) break; // sorted descending -- everything after this is weaker too
    recordHeardPeer(WiFi.BSSID(i), (int8_t)constrain(rssi, -128, 127));
    Serial.printf("[ambient] anchor %d: %s rssi=%d ssid=%s\n", added, WiFi.BSSIDstr(i).c_str(), rssi, WiFi.SSID(i).c_str());
    added++;
  }
  if (added == 0) Serial.println("[ambient] no APs above threshold this scan");
  WiFi.scanDelete();
  // scanning knocks the radio off AP_CHANNEL -- put it back for ESP-NOW
  esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// ==================== ON-BOARD ID DISPLAY ====================
// Shows this board's own short ID (same last-5-hex suffix the dashboard
// uses) plus its role, so you can match a physical board to a dot on the
// map. The two display types are genuinely different hardware/libraries,
// so -- like IS_AP_HOST -- each board picks its own DISPLAY_TYPE below;
// the other branch is compiled out entirely, so it's safe to leave both
// #include lines in the one shared file.
#define DISPLAY_NONE     0
#define DISPLAY_C3_OLED  1   // ESP32-C3 SuperMini + 0.42" SSD1306 OLED (ABRobot-style, 72x40, SDA=GPIO5 SCL=GPIO6)
#define DISPLAY_TTGO_TFT 2   // TTGO T-Display, built-in 135x240 ST7789 TFT

#define DISPLAY_TYPE DISPLAY_C3_OLED   // <-- set per board: DISPLAY_C3_OLED or DISPLAY_TTGO_TFT

#if DISPLAY_TYPE == DISPLAY_C3_OLED
  // Requires the "U8g2" library (by olikraus) via Library Manager.
  #include <U8g2lib.h>
  #include <Wire.h>
  // No coordinate offset needed with this specific constructor -- some
  // tutorials use the generic 128x64 constructor plus a manual offset
  // instead, but this one maps directly onto the real 72x40 panel.
  U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, /*scl*/6, /*sda*/5);
#elif DISPLAY_TYPE == DISPLAY_TTGO_TFT
  // Requires the "TFT_eSPI" library (by Bodmer). Before compiling, edit
  // TFT_eSPI's User_Setup_Select.h and uncomment the TTGO T-Display line
  // (Setup25_TTGO_T_Display.h) -- it won't pick the right pins otherwise.
  #include <TFT_eSPI.h>
  TFT_eSPI tft = TFT_eSPI();
  // TTGO T-Display's backlight is on GPIO4, active HIGH. Some TFT_eSPI
  // User_Setup files turn this on automatically inside tft.init(), but
  // plenty don't -- and a display that's drawing correctly with the
  // backlight simply never switched on looks identical to "not working
  // at all". Driving it explicitly here is a safe no-op if the setup file
  // already handles it, and the fix if it doesn't.
  #define TTGO_BACKLIGHT_PIN 4
#endif

void setupDisplay() {
  Serial.printf("[display] DISPLAY_TYPE=%d (0=none,1=C3 OLED,2=TTGO TFT)\n", DISPLAY_TYPE);
#if DISPLAY_TYPE == DISPLAY_C3_OLED
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);
#elif DISPLAY_TYPE == DISPLAY_TTGO_TFT
  pinMode(TTGO_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TTGO_BACKLIGHT_PIN, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
#endif
}

void updateDisplay() {
#if DISPLAY_TYPE == DISPLAY_C3_OLED
  String id = macToStr(myMac).substring(12); // last 5 chars -- matches dashboard's nodeId.slice(-5)
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 12, id.c_str());
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 24, IS_AP_HOST ? "HOST" : "NODE");
  char buf[20]; snprintf(buf, sizeof(buf), "peers:%d", heardPeerCount);
  u8g2.drawStr(0, 34, buf);
  u8g2.sendBuffer();
#elif DISPLAY_TYPE == DISPLAY_TTGO_TFT
  String id = macToStr(myMac).substring(12);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 20);
  tft.print(id);
  tft.setTextSize(2);
  tft.setCursor(10, 60);
  tft.print(IS_AP_HOST ? "HOST" : "NODE");
  tft.setCursor(10, 90);
  tft.print("peers: "); tft.print(heardPeerCount);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (IS_AP_HOST) {
    setupApHost();
  } else {
    WiFi.mode(WIFI_STA);
    delay(100);
    esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_now_init();
    esp_now_register_recv_cb(onDataRecv);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddr, 6);
    peer.channel = AP_CHANNEL; peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  delay(100);
  WiFi.macAddress(myMac);
  Serial.print("This node's MAC: "); Serial.println(WiFi.macAddress());
  setupDisplay();
}

unsigned long lastSend = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastDisplayUpdate = 0;
void loop() {
  if (millis() - lastHeartbeat > 3000) {
    Serial.printf("[heartbeat] alive, heardPeerCount=%d\n", heardPeerCount);
    lastHeartbeat = millis();
  }

  if (IS_AP_HOST) server.handleClient();

  if (millis() - lastDisplayUpdate > 500) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  if (ENABLE_AMBIENT_ANCHOR && millis() - lastAmbientScan > AMBIENT_SCAN_INTERVAL_MS) {
    scanAmbientAnchor();
    lastAmbientScan = millis();
  }

  if (millis() - lastSend > 300) {
    NodePacket pkt = {};
    memcpy(pkt.mac, myMac, 6);
    pkt.help_detected = false;   // placeholder until real sensing is added back
    pkt.confidence = 0;
    pkt.amplitude = 0;
    pkt.peer_count = min(heardPeerCount, MAX_PEERS_PER_NODE);
    for (int i = 0; i < pkt.peer_count; i++) {
      memcpy(pkt.peers[i].mac, heardPeers[i].mac, 6);
      pkt.peers[i].rssi = heardPeers[i].rssi;
    }
    esp_now_send(broadcastAddr, (uint8_t*)&pkt, sizeof(pkt));

    // the host never receives its own ESP-NOW broadcast, so it has to
    // register itself into knownNodes directly here -- otherwise the AP
    // host never shows up as a node in its own dashboard.
    if (IS_AP_HOST) {
      updateNodeState(myMac, pkt.help_detected, pkt.confidence, pkt.amplitude,
                       heardPeers, pkt.peer_count);
    }
    lastSend = millis();
  }
}
