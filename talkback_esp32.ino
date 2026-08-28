/*
   ESP32 Stage-Talkback / Switched Mic + Battery Level Monitor
   Логіка: При натисканні глушить Main (FOH) та обрані шини (Bus).
   Всі символи кирилиці передаються в UTF-8.
*/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>

// ---------- НАЛАШТУВАННЯ СВІТЛОДІОДІВ (RGBW) ----------
#define LED_PIN      15     
#define NUM_LEDS     3      

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);

const uint32_t COLOR_OFF = strip.Color(0, 0, 0, 0);

// ---------- ПІНИ ГАРДВЕРА ----------
const int buttonPin = 4;    
const int resetPin  = 16;   
#define BATTERY_PIN 34   // Аналоговий пін ADC для дільника 10k + 10k
#define ADC_MAX 4095.0  // 12-бітний АЦП ESP32
#define V_REF 3.65       // Опорна напруга АЦП

// ---------- ПЕРЕЛІК МІКШЕРНИХ ПУЛЬТІВ ----------
enum MixerModel {
  MIXER_X32 = 0,        
  MIXER_XAIR = 1,       
  MIXER_WING = 2,       
  MIXER_YAMAHA = 3,     
  MIXER_AH = 4,         
  MIXER_WAVES = 5,      
  MIXER_SOUNDCRAFT = 6  
};

// ---------- МОВНІ ПЕРЕКЛАДИ ----------
enum Language { LANG_EN = 0, LANG_DE = 1, LANG_UA = 2, LANG_RU = 3 };
int currentLang = LANG_UA;

struct Translations {
  const char* title;
  const char* statusConnected;
  const char* localAddressLabel;
  const char* selectWifi;
  const char* manualSsid;
  const char* wifiPass;
  const char* mixerModelLabel;
  const char* mixerIp;
  const char* channel;
  const char* btnModeLabel;
  const char* btnModePTT;
  const char* btnModeLatch;
  const char* ledBrightnessLabel;
  const char* selectLang;
  const char* busSelectTitle;
  const char* btnSave;
  const char* msgSaved;
  const char* msgRebooting;
};

const Translations txt[] = {
  // 0: ENGLISH
  {
    "Talkback Settings", "Status: Connected to Wi-Fi", "Local Web Address:",
    "Select Wi-Fi Network:", "Or enter SSID manually:", "Wi-Fi Password:",
    "Mixer Model:", "Mixer IP Address:", "Channel Number (1-32):", "Button Mode:",
    "Push-To-Talk (Hold to Speak)", "Latch (Toggle On/Off)", "LED Brightness (%):",
    "Interface Language:", "Select Buses to MUTE during Talkback:", "Save & Reboot", "Saved!", "ESP32 is rebooting..."
  },
  // 1: GERMAN
  {
    "Talkback Einstellungen", "Status: Verbunden mit Wi-Fi", "Lokale Webadresse:",
    "Wi-Fi-Netzwerk auswählen:", "Oder SSID manuell eingeben:", "Wi-Fi-Passwort:",
    "Mischpult Modell:", "Mischpult IP-Adresse:", "Kanalnummer (1-32):", "Tastenmodus:",
    "Push-To-Talk (Gedrückt halten)", "Latch (Umschalten An/Aus)", "LED-Helligkeit (%):",
    "Schnittstellensprache:", "Busse zum STUMMSCHALTEN bei Talkback wählen:", "Speichern & Neustarten", "Gespeichert!", "ESP32 startet neu..."
  },
  // 2: UKRAINIAN
  {
    "Налаштування Talkback", "Статус: Підключено до Wi-Fi", "Локальна веб-адреса:",
    "Оберіть Wi-Fi мережу:", "Або введіть SSID вручну:", "Пароль Wi-Fi:",
    "Модель мікшерного пульта:", "IP адреса мікшерного пульта:", "Номер каналу (1-32):", "Режим роботи кнопки:",
    "Push-To-Talk (Утримувати для розмови)", "Latch (Перемикач Увімк/Вимк)", "Яскравість світлодіодів (%):",
    "Мова інтерфейсу:", "Оберіть Bus шини, які ЗАГЛУШИТИ при Talkback:", "Зберегти та Перезавантажити", "Збережено!", "ESP32 перезавантажується..."
  },
  // 3: RUSSIAN
  {
    "Настройки Talkback", "Подключено к Wi-Fi", "Локальный веб-адрес:",
    "Выберите Wi-Fi сеть:", "Или введите SSID вручную:", "Пароль Wi-Fi:",
    "Модель микшерного пульта:", "IP адрес микшерного пульта:", "Номер канала (1-32):", "Режим кнопки:",
    "Push-To-Talk (Удержание)", "Latch (Переключатель Вкл/Выкл)", "Яркость светодиодов (%):",
    "Язык интерфейса:", "Выберите Bus шины, которые ЗАГЛУШИТЬ при Talkback:", "Сохранить и Перезагрузить", "Сохранено!", "Перезагрузка..."
  }
};

// ---------- МЕНЕДЖЕР НАЛАШТУВАНЬ ----------
Preferences preferences;

String wifi_ssid = "";
String wifi_pass = "";
String mixer_ip_str = ""; 
int mixer_model = MIXER_X32;
int channel = 0;          
int button_mode = 0; 
int led_brightness = 30; 
bool bus_mute_flags[16] = {0};

// Мережеві налаштування
bool use_static_ip = false;
String static_ip_str = "";
String gateway_str = "";
String subnet_str = "255.255.255.0";
String mdns_name = "talkback";

// Кастомні кольори (HEX)
String color_standby_hex = "#0000FF";
String color_active_hex  = "#00FF00";

// Зворотний зв'язок з мікшером
bool mixer_online = false;
unsigned long lastPingTime = 0;
const unsigned long pingInterval = 3000;

IPAddress mixerIP;

WiFiUDP udp;
WebServer server(80);
DNSServer dnsServer;

bool isAPMode = false;

// Debounce & State
bool lastButtonState = HIGH;
bool stableButtonState = HIGH;
bool latchActiveState = false; 
bool isTalkbackActive = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 40;

// Reset Check
unsigned long resetHoldStart = 0;
bool resetHandled = false;

// Wi-Fi Reconnect & Timeout
unsigned long lastWifiCheck = 0;
const unsigned long wifiCheckInterval = 3000; 
bool wifiWasConnected = false;
unsigned long wifiConnectStart = 0;
const unsigned long wifiTimeout = 20000; 

// ---------- ВИМІРЮВАННЯ БАТАРЕЇ ----------
void getBatteryStatus(float &voltage, int &percentage) {
  long summV = 0;
  for (int i = 0; i < 20; i++) {
    summV += analogReadMilliVolts(BATTERY_PIN); // Читає напругу на піні в мілівольтах
    delay(2);
  }
  float avgVoltsOnPin = (summV / 20.0) / 1000.0; // Переводимо у Вольти

  // Множимо на 2.0 (через дільник 10k + 10k)
  voltage = avgVoltsOnPin * 2.0; 

  // Розрахунок відсотків заряду для Li-Ion (3.3V - 4.2V)
  percentage = (int)(((voltage - 3.3) / (4.2 - 3.3)) * 100.0);

  if (percentage > 100) percentage = 100;
  if (percentage < 0) percentage = 0;
}

// ---------- ДОПОМІЖНІ ФУНКЦІЇ КОЛЬОРУ ----------
uint32_t hexToColor(String hex) {
  if (hex.startsWith("#")) hex.remove(0, 1);
  long number = strtol(hex.c_str(), NULL, 16);
  long r = (number >> 16) & 0xFF;
  long g = (number >> 8) & 0xFF;
  long b = number & 0xFF;
  return strip.Color(r, g, b, 0);
}

// ---------- ПОРТИ ТА КІЛЬКІСТЬ ШИН ----------
int getMixerPort(int model) {
  switch (model) {
    case MIXER_X32: return 10023;
    case MIXER_XAIR: return 10024;
    case MIXER_WING: return 2223;
    case MIXER_YAMAHA: return 49280;
    case MIXER_AH: return 51325;
    case MIXER_WAVES: return 10023;
    case MIXER_SOUNDCRAFT: return 8080;
    default: return 10023;
  }
}

int getMaxBuses(int model) {
  switch (model) {
    case MIXER_XAIR: return 6;
    case MIXER_SOUNDCRAFT: return 6;
    default: return 16;
  }
}

// ---------- УПРАВЛІННЯ СВІТЛОДІОДАМИ ----------
void updateLedBrightness(int percent) {
  uint8_t scaledBrightness = map(constrain(percent, 0, 100), 0, 100, 0, 255);
  strip.setBrightness(scaledBrightness);
  strip.show();
}

void setAllLeds(uint32_t color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void setBreathingLed(uint8_t r, uint8_t g, uint8_t b, float speed = 0.0025) {
  float breath = (exp(sin(millis() * speed)) - 0.36787944) * 108.0; 
  uint8_t red = (r * breath) / 255;
  uint8_t green = (g * breath) / 255;
  uint8_t blue = (b * breath) / 255;

  setAllLeds(strip.Color(red, green, blue, 0));
}

void updateLedState() {
  if (isAPMode || WiFi.status() != WL_CONNECTED) return;

  if (mixer_ip_str.length() == 0 || !mixer_online) {
    if ((millis() / 500) % 2 == 0) {
      setAllLeds(strip.Color(255, 0, 255, 0)); 
    } else {
      setAllLeds(COLOR_OFF);
    }
    return;
  }

  if (isTalkbackActive) {
    setAllLeds(hexToColor(color_active_hex));
  } else {
    setAllLeds(hexToColor(color_standby_hex));
  }
}

// ---------- ФУНКЦІЇ ВІДПРАВКИ OSC ТА PING ----------
void sendOSCFloat(const char* address, float value) {
  if (isAPMode || WiFi.status() != WL_CONNECTED || mixer_ip_str.length() == 0) return;

  uint8_t packet[64];
  memset(packet, 0, sizeof(packet));

  int addrLen = strlen(address);
  int addrPad = ((addrLen + 4) / 4) * 4;
  memcpy(packet, address, addrLen);

  int offset = addrPad;
  packet[offset]     = ',';
  packet[offset + 1] = 'f';
  packet[offset + 2] = 0;
  packet[offset + 3] = 0;
  offset += 4;

  union { float f; uint8_t b[4]; } u;
  u.f = value;

  packet[offset]     = u.b[3];
  packet[offset + 1] = u.b[2];
  packet[offset + 2] = u.b[1];
  packet[offset + 3] = u.b[0];

  udp.beginPacket(mixerIP, getMixerPort(mixer_model));
  udp.write(packet, offset + 4);
  udp.endPacket();
}

void sendMixerPing() {
  if (isAPMode || WiFi.status() != WL_CONNECTED || mixer_ip_str.length() == 0) return;

  const char* pingCmd = "/xinfo";
  uint8_t packet[32];
  memset(packet, 0, sizeof(packet));

  int addrLen = strlen(pingCmd);
  int addrPad = ((addrLen + 4) / 4) * 4;
  memcpy(packet, pingCmd, addrLen);

  udp.beginPacket(mixerIP, getMixerPort(mixer_model));
  udp.write(packet, addrPad);
  udp.endPacket();
}

void checkMixerResponse() {
  if (isAPMode || WiFi.status() != WL_CONNECTED || mixer_ip_str.length() == 0) {
    mixer_online = false;
    return;
  }

  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    mixer_online = true;
    while (udp.available()) udp.read();
  } else {
    if (millis() - lastPingTime > (pingInterval * 2)) {
      mixer_online = false;
    }
  }

  if (millis() - lastPingTime >= pingInterval) {
    lastPingTime = millis();
    sendMixerPing();
  }
}

// ---------- ЛОГІКА TALKBACK ----------
void setTalkback(bool active) {
  isTalkbackActive = active;
  if (channel <= 0) return; 

  int maxBus = getMaxBuses(mixer_model);

  if (active) {
    // 1. Глушимо Main FOH
    if (mixer_model == MIXER_X32 || mixer_model == MIXER_WAVES) {
      char mainAddr[32];
      snprintf(mainAddr, sizeof(mainAddr), "/ch/%02d/mix/st", channel);
      sendOSCFloat(mainAddr, 0.0f);
    } else if (mixer_model == MIXER_XAIR) {
      char mainAddr[32];
      snprintf(mainAddr, sizeof(mainAddr), "/ch/%02d/mix/lr", channel);
      sendOSCFloat(mainAddr, 0.0f);
    } else if (mixer_model == MIXER_WING) {
      char mainAddr[32];
      snprintf(mainAddr, sizeof(mainAddr), "/ch/%d/main/1/on", channel);
      sendOSCFloat(mainAddr, 0.0f);
    }

    // 2. Глушимо обрані шини (Bus)
    for (int i = 0; i < maxBus; i++) {
      if (bus_mute_flags[i]) {
        char busAddr[32];
        if (mixer_model == MIXER_WING) {
          snprintf(busAddr, sizeof(busAddr), "/ch/%d/send/%d/on", channel, i + 1);
        } else {
          snprintf(busAddr, sizeof(busAddr), "/ch/%02d/mix/%02d/on", channel, i + 1);
        }
        sendOSCFloat(busAddr, 0.0f); 
      }
    }

  } else {
    // 1. Повертаємо Main FOH
    if (mixer_model == MIXER_X32 || mixer_model == MIXER_WAVES) {
      char mainAddr[32];
      snprintf(mainAddr, sizeof(mainAddr), "/ch/%02d/mix/st", channel);
      sendOSCFloat(mainAddr, 1.0f);
    } else if (mixer_model == MIXER_XAIR) {
      char mainAddr[32];
      snprintf(mainAddr, sizeof(mainAddr), "/ch/%02d/mix/lr", channel);
      sendOSCFloat(mainAddr, 1.0f);
    } else if (mixer_model == MIXER_WING) {
      char mainAddr[32];
      snprintf(mainAddr, sizeof(mainAddr), "/ch/%d/main/1/on", channel);
      sendOSCFloat(mainAddr, 1.0f);
    }

    // 2. Відновлюємо посили на обрані шини
    for (int i = 0; i < maxBus; i++) {
      if (bus_mute_flags[i]) {
        char busAddr[32];
        if (mixer_model == MIXER_WING) {
          snprintf(busAddr, sizeof(busAddr), "/ch/%d/send/%d/on", channel, i + 1);
        } else {
          snprintf(busAddr, sizeof(busAddr), "/ch/%02d/mix/%02d/on", channel, i + 1);
        }
        sendOSCFloat(busAddr, 1.0f); 
      }
    }
  }

  updateLedState();
}

// ---------- AJAX СТАТУС (ВКЛЮЧАЮЧИ БАТАРЕЮ) ----------
void handleApiStatus() {
  float voltage = 0.0;
  int percentage = 0;
  getBatteryStatus(voltage, percentage);

  String json = "{";
  json += "\"talkback\":" + String(isTalkbackActive ? "true" : "false") + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"mixer_online\":" + String(mixer_online ? "true" : "false") + ",";
  json += "\"battery_pct\":" + String(percentage) + ",";
  json += "\"battery_v\":" + String(voltage, 2);
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

// ---------- ВЕБ-ІНТЕРФЕЙС ----------
void handleRoot() {
  if (isAPMode && server.hostHeader() != "5.5.5.5") {
    server.sendHeader("Location", "http://5.5.5.5/", true);
    server.send(302, "text/plain", "");
    return;
  }

  const Translations& t = txt[currentLang];

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + String(t.title) + "</title>";

  html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff;} ";
  html += "input[type=text],input[type=number],input[type=password],select{width:100%;padding:10px;margin:8px 0;box-sizing:border-box;border-radius:5px;border:none;} ";
  html += "input[type=range]{width:100%;margin:10px 0;} ";
  html += "input[type=color]{width:100%;height:40px;border:none;border-radius:5px;margin:8px 0;cursor:pointer;} ";
  html += "input[type=submit]{background:#007bff;color:#fff;font-weight:bold;cursor:pointer;padding:12px;border:none;border-radius:5px;width:100%;margin-top:10px;} ";
  html += ".card{background:#2a2a2a;padding:20px;border-radius:10px;max-width:420px;margin:auto;} a{color:#007bff;} ";
  html += ".pass-box{position:relative;display:flex;align-items:center;} ";
  html += ".pass-box input{padding-right:45px;} ";
  html += ".toggle-btn{position:absolute;right:10px;background:none;border:none;color:#aaa;cursor:pointer;font-size:18px;padding:5px;} ";
  html += ".range-container{display:flex;align-items:center;justify-content:space-between;gap:10px;} ";
  html += ".range-val{font-weight:bold;min-width:40px;text-align:right;} ";
  html += ".bus-grid{display:grid;grid-template-columns:repeat(4, 1fr);gap:8px;margin:10px 0 15px 0;} ";
  html += ".bus-item{background:#3a3a3a;border-radius:5px;padding:8px;text-align:center;font-size:12px;font-weight:bold;cursor:pointer;user-select:none;} ";
  html += ".bus-item input{display:none;} ";
  html += ".bus-item:has(input:checked){background:#dc3545;color:#fff;} ";
  html += ".status-box{background:#333;padding:12px;border-radius:5px;margin-bottom:15px;} ";
  html += ".badge{padding:3px 8px;border-radius:3px;font-weight:bold;font-size:12px;} ";
  html += ".bg-online{background:#28a745;color:#fff;} .bg-offline{background:#dc3545;color:#fff;} ";
  html += ".row-inline{display:flex;gap:10px;align-items:center;} ";
  
  // Стилі для прогрес-бару батареї
  html += ".battery-box{background:#222;padding:10px;border-radius:8px;margin-top:10px;} ";
  html += ".progress-bg{width:100%;background:#444;height:18px;border-radius:9px;overflow:hidden;margin-top:5px;border:1px solid #555;} ";
  html += ".progress-fill{height:100%;width:0%;background:#28a745;transition:width 0.4s, background 0.4s;} ";
  html += "</style>";

  html += "<script>";
  html += "function togglePass(){var p=document.getElementById('pass-field'),b=document.getElementById('toggle-btn');if(p.type==='password'){p.type='text';b.innerHTML='🔓';}else{p.type='password';b.innerHTML='🔒';}}";
  html += "function updateBrightVal(val){document.getElementById('bright-val').innerText = val + '%';}";

  html += "function updateBusGrid(){";
  html += "  var model = parseInt(document.getElementById('mixer-select').value);";
  html += "  var maxBus = 16;";
  html += "  if(model === 1 || model === 6){ maxBus = 6; }"; 
  html += "  for(var i = 1; i <= 16; i++){";
  html += "    var el = document.getElementById('bus-box-' + i);";
  html += "    if(el){ el.style.display = (i <= maxBus) ? 'block' : 'none'; }";
  html += "  }";
  html += "}";

  html += "function toggleStaticIp(){";
  html += "  var chk = document.getElementById('use-static').checked;";
  html += "  document.getElementById('static-box').style.display = chk ? 'block' : 'none';";
  html += "}";

  html += "setInterval(function(){";
  html += "  fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "    var st = document.getElementById('mixer-status');";
  html += "    if(d.mixer_online){ st.innerText='ONLINE'; st.className='badge bg-online'; }";
  html += "    else { st.innerText='OFFLINE / UNREACHABLE'; st.className='badge bg-offline'; }";
  html += "    document.getElementById('wifi-rssi').innerText = d.rssi + ' dBm';";
  html += "    document.getElementById('tb-state').innerText = d.talkback ? 'TALKBACK ACTIVE' : 'LIVE (FOH)';";
  
  // Оновлення батареї у JS
  html += "    document.getElementById('bat-pct').innerText = d.battery_pct + '%';";
  html += "    document.getElementById('bat-v').innerText = d.battery_v.toFixed(2) + ' V';";
  html += "    var bar = document.getElementById('bat-bar');";
  html += "    bar.style.width = d.battery_pct + '%';";
  html += "    if(d.battery_pct > 50) bar.style.backgroundColor='#28a745';";
  html += "    else if(d.battery_pct > 20) bar.style.backgroundColor='#ffc107';";
  html += "    else bar.style.backgroundColor='#dc3545';";

  html += "  }).catch(e=>{});";
  html += "}, 2000);";

  html += "window.onload = function(){ updateBusGrid(); toggleStaticIp(); };"; 
  html += "</script>";

  html += "</head><body>";
  html += "<div class='card'><h2>" + String(t.title) + "</h2>";

  if (!isAPMode) {
    html += "<div class='status-box'>";
    html += "<p style='margin:3px 0;'><b>Wi-Fi:</b> " + WiFi.localIP().toString() + " (<span id='wifi-rssi'>" + String(WiFi.RSSI()) + " dBm</span>)</p>";
    html += "<p style='margin:3px 0;'><b>Mixer Link:</b> <span id='mixer-status' class='badge " + String(mixer_online ? "bg-online" : "bg-offline") + "'>" + String(mixer_online ? "ONLINE" : "OFFLINE / UNREACHABLE") + "</span></p>";
    html += "<p style='margin:3px 0;'><b>Mode:</b> <span id='tb-state'>" + String(isTalkbackActive ? "TALKBACK ACTIVE" : "LIVE (FOH)") + "</span></p>";
    
    // Блок батареї у Веб-інтерфейсі
    html += "<div class='battery-box'>";
    html += "<div style='display:flex;justify-content:space-between;font-size:13px;'><b>Акумулятор:</b> <span id='bat-pct'>0%</span> (<span id='bat-v'>0.00 V</span>)</div>";
    html += "<div class='progress-bg'><div id='bat-bar' class='progress-fill'></div></div>";
    html += "</div>";

    html += "<p style='margin:8px 0 0 0;font-size:12px;'><b>Host:</b> <a href='http://" + mdns_name + ".local' target='_blank'>http://" + mdns_name + ".local</a></p>";
    html += "</div>";
  }

  html += "<form action='/save' method='POST'>";

  // Мова
  html += "<label>" + String(t.selectLang) + "</label><select name='lang'>";
  html += "<option value='0'" + String(currentLang == LANG_EN ? " selected" : "") + ">English</option>";
  html += "<option value='1'" + String(currentLang == LANG_DE ? " selected" : "") + ">Deutsch</option>";
  html += "<option value='2'" + String(currentLang == LANG_UA ? " selected" : "") + ">Українська</option>";
  html += "<option value='3'" + String(currentLang == LANG_RU ? " selected" : "") + ">Русский</option>";
  html += "</select>";

  // Wi-Fi
  html += "<label>" + String(t.selectWifi) + "</label><select name='ssid'>";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    String sel = (WiFi.SSID(i) == wifi_ssid) ? " selected" : "";
    html += "<option value='" + WiFi.SSID(i) + "'" + sel + ">" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  html += "</select>";

  html += "<label>" + String(t.manualSsid) + "</label><input type='text' name='manual_ssid' placeholder='SSID' value='" + wifi_ssid + "'>";

  html += "<label>" + String(t.wifiPass) + "</label>";
  html += "<div class='pass-box'>";
  html += "<input type='password' id='pass-field' name='password' value='" + wifi_pass + "'>";
  html += "<button type='button' id='toggle-btn' class='toggle-btn' onclick='togglePass()'>🔒</button>";
  html += "</div>";

  // mDNS та Static IP
  html += "<label>mDNS Hostname (.local)</label>";
  html += "<input type='text' name='mdns' value='" + mdns_name + "' placeholder='talkback'>";

  html += "<div style='margin:10px 0;'>";
  html += "<label><input type='checkbox' id='use-static' name='use_static' value='1'" + String(use_static_ip ? " checked" : "") + " onchange='toggleStaticIp()'> Use Static IP for ESP32</label>";
  html += "</div>";

  html += "<div id='static-box' style='display:none;'>";
  html += "<label>ESP32 Static IP</label><input type='text' name='static_ip' placeholder='192.168.1.50' value='" + static_ip_str + "'>";
  html += "<label>Gateway (Router IP)</label><input type='text' name='gateway' placeholder='192.168.1.1' value='" + gateway_str + "'>";
  html += "<label>Subnet Mask</label><input type='text' name='subnet' placeholder='255.255.255.0' value='" + subnet_str + "'>";
  html += "</div>";

  // Мікшер
  html += "<label>" + String(t.mixerModelLabel) + "</label>";
  html += "<select name='model' id='mixer-select' onchange='updateBusGrid()'>";
  html += "<option value='0'" + String(mixer_model == MIXER_X32 ? " selected" : "") + ">Behringer X32 / Midas M32</option>";
  html += "<option value='1'" + String(mixer_model == MIXER_XAIR ? " selected" : "") + ">Behringer X Air / Midas MR</option>";
  html += "<option value='2'" + String(mixer_model == MIXER_WING ? " selected" : "") + ">Behringer WING</option>";
  html += "<option value='3'" + String(mixer_model == MIXER_YAMAHA ? " selected" : "") + ">Yamaha (CL/QL/TF/RIVAGE)</option>";
  html += "<option value='4'" + String(mixer_model == MIXER_AH ? " selected" : "") + ">Allen & Heath (SQ/Qu/dLive)</option>";
  html += "<option value='5'" + String(mixer_model == MIXER_WAVES ? " selected" : "") + ">Waves eMotion LV1</option>";
  html += "<option value='6'" + String(mixer_model == MIXER_SOUNDCRAFT ? " selected" : "") + ">Soundcraft Ui Series</option>";
  html += "</select>";

  html += "<label>" + String(t.mixerIp) + "</label>";
  html += "<input type='text' name='mixerip' placeholder='192.168.1.100' value='" + mixer_ip_str + "'>";

  String channelVal = (channel > 0) ? String(channel) : "";
  html += "<label>" + String(t.channel) + "</label>";
  html += "<input type='number' name='channel' min='1' max='32' placeholder='1-32' value='" + channelVal + "'>";

  // Режим кнопки
  html += "<label>" + String(t.btnModeLabel) + "</label><select name='btnmode'>";
  html += "<option value='0'" + String(button_mode == 0 ? " selected" : "") + ">" + String(t.btnModePTT) + "</option>";
  html += "<option value='1'" + String(button_mode == 1 ? " selected" : "") + ">" + String(t.btnModeLatch) + "</option>";
  html += "</select>";

  // Яскравість
  html += "<label>" + String(t.ledBrightnessLabel) + "</label>";
  html += "<div class='range-container'>";
  html += "<input type='range' name='brightness' min='0' max='100' value='" + String(led_brightness) + "' oninput='updateBrightVal(this.value)'>";
  html += "<span id='bright-val' class='range-val'>" + String(led_brightness) + "%</span>";
  html += "</div>";

  // Кольори
  html += "<div class='row-inline'>";
  html += "<div style='flex:1;'><label>Standby (FOH Live)</label><input type='color' name='col_standby' value='" + color_standby_hex + "'></div>";
  html += "<div style='flex:1;'><label>Active (Talkback)</label><input type='color' name='col_active' value='" + color_active_hex + "'></div>";
  html += "</div>";

  // Сітка шин ДЛЯ ЗАГЛУШЕННЯ
  html += "<label><b>" + String(t.busSelectTitle) + "</b></label>";
  html += "<div class='bus-grid'>";
  for (int i = 0; i < 16; i++) {
    String chk = bus_mute_flags[i] ? " checked" : "";
    html += "<label class='bus-item' id='bus-box-" + String(i + 1) + "'>";
    html += "<input type='checkbox' name='bus" + String(i + 1) + "' value='1'" + chk + ">";
    html += "BUS " + String(i + 1);
    html += "</label>";
  }
  html += "</div>";

  html += "<input type='submit' value='" + String(t.btnSave) + "'>";
  html += "</form>";

  html += "</div></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

// ---------- ЗБЕРЕЖЕННЯ НАЛАШТУВАНЬ ТА РЕДИРЕКТ ----------
void handleSave() {
  String selected_ssid = server.arg("ssid");
  String manual_ssid = server.arg("manual_ssid");
  if (manual_ssid.length() > 0) {
    selected_ssid = manual_ssid;
  }

  wifi_ssid = selected_ssid;
  wifi_pass = server.arg("password");
  mixer_model = server.arg("model").toInt();
  mixer_ip_str = server.arg("mixerip");
  channel = server.arg("channel").toInt();
  button_mode = server.arg("btnmode").toInt();
  led_brightness = server.arg("brightness").toInt();
  currentLang = server.arg("lang").toInt();

  use_static_ip = (server.arg("use_static") == "1");
  static_ip_str = server.arg("static_ip");
  gateway_str   = server.arg("gateway");
  subnet_str    = server.arg("subnet");
  mdns_name     = server.arg("mdns");
  if (mdns_name.length() == 0) mdns_name = "talkback";

  color_standby_hex = server.arg("col_standby");
  color_active_hex  = server.arg("col_active");

  for (int i = 0; i < 16; i++) {
    String argName = "bus" + String(i + 1);
    bus_mute_flags[i] = (server.arg(argName) == "1");
  }

  preferences.begin("settings", false);
  preferences.putString("ssid", wifi_ssid);
  preferences.putString("pass", wifi_pass);
  preferences.putInt("model", mixer_model);
  preferences.putString("mixerip", mixer_ip_str);
  preferences.putInt("channel", channel);
  preferences.putInt("btnmode", button_mode);
  preferences.putInt("brightness", led_brightness);
  preferences.putInt("lang", currentLang);

  preferences.putBool("usestatic", use_static_ip);
  preferences.putString("staticip", static_ip_str);
  preferences.putString("gateway", gateway_str);
  preferences.putString("subnet", subnet_str);
  preferences.putString("mdns", mdns_name);

  preferences.putString("c_standby", color_standby_hex);
  preferences.putString("c_active", color_active_hex);

  preferences.putBytes("busmutes", bus_mute_flags, sizeof(bus_mute_flags));
  preferences.end();

  const Translations& t = txt[currentLang];

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "</head><body style='background:#1a1a1a;color:#fff;font-family:Arial;text-align:center;padding-top:50px;'>";
  html += "<h2>" + String(t.msgSaved) + "</h2><p>Перенаправлення на головну сторінку...</p></body></html>";
  
  server.send(200, "text/html; charset=utf-8", html);

  delay(2000);
  ESP.restart();
}

// ---------- ТОЧКА ДОСТУПУ ТА МАРШРУТИ ----------
void startAPMode() {
  isAPMode = true;
  WiFi.mode(WIFI_AP);
  IPAddress apIP(5, 5, 5, 5);
  IPAddress netMsk(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP("ESP32-Talkback-Setup");
  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/api/status", handleApiStatus);
  server.onNotFound(handleRoot);
  server.begin();
}

void setupServerRoutes() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/api/status", handleApiStatus);
  server.onNotFound(handleRoot);
}

void handleWiFiConnection() {
  if (isAPMode) return;

  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      wifiConnectStart = currentMillis; 
    }

    setBreathingLed(255, 180, 0, 0.003);

    if (currentMillis - wifiConnectStart >= wifiTimeout) {
      WiFi.disconnect(true);
      startAPMode();
      return;
    }

    if (currentMillis - lastWifiCheck >= wifiCheckInterval) {
      lastWifiCheck = currentMillis;
      WiFi.reconnect();
    }

  } else {
    if (!wifiWasConnected) {
      wifiWasConnected = true;

      for (int i = 0; i < 3; i++) {
        setAllLeds(hexToColor(color_standby_hex));
        delay(100);
        setAllLeds(COLOR_OFF);
        delay(100);
      }

      MDNS.begin(mdns_name.c_str());
      udp.stop();
      udp.begin(getMixerPort(mixer_model));
      updateLedState();
    }
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);

  analogReadResolution(12); // Налаштування 12-бітного розділення АЦП
  analogSetAttenuation(ADC_11db);

  preferences.begin("settings", true);
  wifi_ssid = preferences.getString("ssid", "");
  wifi_pass = preferences.getString("pass", "");
  mixer_model = preferences.getInt("model", MIXER_X32);
  
  mixer_ip_str = preferences.getString("mixerip", "");
  channel = preferences.getInt("channel", 0);
  
  button_mode = preferences.getInt("btnmode", 0); 
  led_brightness = preferences.getInt("brightness", 30); 
  currentLang = preferences.getInt("lang", LANG_UA);

  use_static_ip = preferences.getBool("usestatic", false);
  static_ip_str = preferences.getString("staticip", "");
  gateway_str   = preferences.getString("gateway", "");
  subnet_str    = preferences.getString("subnet", "255.255.255.0");
  mdns_name     = preferences.getString("mdns", "talkback");

  color_standby_hex = preferences.getString("c_standby", "#0000FF");
  color_active_hex  = preferences.getString("c_active", "#00FF00");

  size_t bytesRead = preferences.getBytes("busmutes", bus_mute_flags, sizeof(bus_mute_flags));
  if (bytesRead != sizeof(bus_mute_flags)) {
    memset(bus_mute_flags, 0, sizeof(bus_mute_flags));
  }
  preferences.end();

  strip.begin();
  updateLedBrightness(led_brightness);
  setAllLeds(COLOR_OFF);

  if (wifi_ssid == "") {
    startAPMode();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); 
    WiFi.persistent(true);

    if (use_static_ip && static_ip_str.length() > 0) {
      IPAddress ip, gw, sn;
      if (ip.fromString(static_ip_str) && gw.fromString(gateway_str) && sn.fromString(subnet_str)) {
        WiFi.config(ip, gw, sn);
      }
    }

    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

    wifiConnectStart = millis(); 
    if (mixer_ip_str.length() > 0) {
      mixerIP.fromString(mixer_ip_str);
    }

    setupServerRoutes();
    server.begin();
  }
}

// ---------- LOOP ----------
void loop() {
  // 1. Reset Pin (Скинути налаштування утримуванням 3 сек)
  if (digitalRead(resetPin) == LOW) {
    if (resetHoldStart == 0) {
      resetHoldStart = millis();
    } else if (millis() - resetHoldStart > 3000 && !resetHandled) {
      resetHandled = true;
      preferences.begin("settings", false);
      preferences.clear();
      preferences.end();

      for (int i = 0; i < 10; i++) {
        setAllLeds(strip.Color(255, 255, 255, 255));
        delay(100);
        setAllLeds(COLOR_OFF);
        delay(100);
      }
      ESP.restart();
    }
  } else {
    resetHoldStart = 0;
  }

  // 2. AP Mode
  if (isAPMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    setBreathingLed(0, 255, 0, 0.002);
    return;
  }

  // 3. Wi-Fi Manager
  handleWiFiConnection();

  // 4. Web Server
  server.handleClient();

  // 5. Перевірка відповіді від пульта
  checkMixerResponse();
  updateLedState();

  // 6. Логіка кнопки
  if (WiFi.status() == WL_CONNECTED) {
    bool reading = digitalRead(buttonPin);

    if (reading != lastButtonState) {
      lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != stableButtonState) {
        stableButtonState = reading;

        if (button_mode == 0) {
          // Push-To-Talk
          setTalkback(stableButtonState == LOW);
        } else if (button_mode == 1) {
          // Latch
          if (stableButtonState == LOW) { 
            latchActiveState = !latchActiveState;
            setTalkback(latchActiveState);
          }
        }
      }
    }
    lastButtonState = reading;
  }
}