#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>


// --- 用户配置区 ---
const char* ssid = "your_wifi";
const char* password = "your_wifi";
const char* mqtt_server = "broker.emqx.io"; // 推荐用公网，最稳
const int mqtt_port = 1883;


// --- 硬件引脚定义 ---
#define BTN_PIN 14     // 街机按键
#define NEO_PIN 27     // 灯环
#define POT_PIN 34     // 电位器 (模拟温度)
#define RELAY_PIN 26   // 继电器 (控制风扇)
#define NEO_NUM 24     // 灯珠数量
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64


// --- 对象初始化 ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_NeoPixel strip(NEO_NUM, NEO_PIN, NEO_GRB + NEO_KHZ800);
WiFiClient espClient;
PubSubClient client(espClient);


// --- 全局变量 ---
int localCount = 0;
bool lastBtnState = HIGH;
unsigned long btnPressTime = 0;
bool isFaultMode = false;
bool ignoreNextRelease = false;
float currentTemp = 0.0; // 全局温度


// --- 灯光辅助函数 ---
void setRingColor(uint32_t color) {
  for(int i=0; i<strip.numPixels(); i++) strip.setPixelColor(i, color);
  strip.show();
}


void rotatingRed() {
  static int head = 0;
  strip.clear();
  for(int i=0; i<3; i++) strip.setPixelColor((head + i) % NEO_NUM, strip.Color(255, 0, 0));
  strip.show();
  head++;
  if(head >= NEO_NUM) head = 0;
  delay(40);
}


void breathingGreen() {
  static unsigned long lastUpdate = 0;
  static int val = 0;
  static int dir = 1;
  if (millis() - lastUpdate > 20) {
    lastUpdate = millis();
    val += dir * 2;
    if (val >= 100) dir = -1;
    if (val <= 5)   dir = 1; 
    setRingColor(strip.Color(0, val, 0));
  }
}


// --- 屏幕刷新 ---
void updateUI() {
  display.clearDisplay();
  
  // 第一行：MQTT状态
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.print("MQTT:");
  display.print(client.connected() ? "OK" : "NO");
  
  // 第二行：温度
  display.setCursor(64, 0);
  display.print("T:");
  display.print(currentTemp, 1);
  display.print("C");
  
  // 中间：大数字产量
  display.setTextSize(3);
  display.setCursor(45, 25);
  display.print(localCount);
  
  // 底部：状态提示
  display.setTextSize(1);
  display.setCursor(0, 55);
  if (currentTemp > 80) display.print("!! OVERHEAT !!");
  else if (isFaultMode) display.print("!! FAULT !!");
  else display.print("RUNNING");
  
  display.display();
}


// --- WiFi & MQTT 连接 ---
void setup_wifi() {
  delay(10);
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.display();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}


void reconnect() {
  if (!client.connected()) {
    String clientId = "ESP32-";
    clientId += String(random(0xffff), HEX);
    client.connect(clientId.c_str());
  }
}


// ===========================
//          SETUP
// ===========================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // 初始化：高电平 -> 继电器断开 -> 风扇停


  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  strip.begin();
  strip.setBrightness(50);
  strip.show();


  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  
  updateUI();
}


// ===========================
//          LOOP
// ===========================
void loop() {
  if (!client.connected()) reconnect();
  client.loop(); 


  // ============================================
  // 1. 独立定时上报温度 (每1秒发一次)
  // ============================================
  static unsigned long lastTempReport = 0;
  if (millis() - lastTempReport > 1000) { // 1000ms = 1秒
    lastTempReport = millis();
    
    // 读取温度
    int adc = analogRead(POT_PIN);
    currentTemp = (adc / 4095.0) * 100.0;
    
    // 只有当温度变化超过 0.5 度时才发送，节省流量 (可选，这里为了演示效果直接发)
    // 构建 JSON: 只发 temp，不发 count (count: 0 表示不增加产量)
    String payload = "{\"count\": 0, \"temp\": " + String(currentTemp, 1) + "}";
    client.publish("factory/line1/data", payload.c_str());
    
    // 顺便刷新一下本地屏幕温度
    updateUI(); 
  }
  
  // ============================================
  // 2. 本地温控逻辑 (继电器控制)
  // ============================================
  if (currentTemp > 80.0) {
      digitalWrite(RELAY_PIN, LOW); // 风扇转
  } else if (currentTemp < 60.0) {
      digitalWrite(RELAY_PIN, HIGH); // 风扇停
  }


  // ============================================
  // 3. 按键逻辑 (保持不变)
  // ============================================
  bool currentBtnState = digitalRead(BTN_PIN);


  if (lastBtnState == HIGH && currentBtnState == LOW) {
    btnPressTime = millis();
    ignoreNextRelease = false;
  }


  // 长按触发故障
  if (currentBtnState == LOW && !isFaultMode) {
    if (millis() - btnPressTime > 2000) {
      isFaultMode = true;
      ignoreNextRelease = true;
      client.publish("factory/line1/data", "{\"status\": \"fault\"}");
    }
  }


  // 松开处理
  if (lastBtnState == LOW && currentBtnState == HIGH) {
    if (millis() - btnPressTime > 50) { 
        if (ignoreNextRelease) {
           ignoreNextRelease = false;
        }
        else if (isFaultMode) {
          isFaultMode = false;
          client.publish("factory/line1/data", "{\"status\": \"running\"}");
          delay(200); 
        }
        else {
          // 正常生产
          localCount++;
          updateUI();
          setRingColor(strip.Color(255, 255, 255)); // 闪白光
          
          // 发送产量数据 (带上当前温度)
          String payload = "{\"count\": 1, \"temp\": " + String(currentTemp, 1) + "}";
          client.publish("factory/line1/data", payload.c_str());
          
          delay(100); 
          setRingColor(strip.Color(0,0,0)); 
        }
    }
  }
  
  // 灯光状态机
  if (isFaultMode) {
    rotatingRed(); 
  } else if (currentTemp > 80.0) {
    // 过热时，显示全红呼吸
    static int val = 0, dir = 5;
    val += dir;
    if(val>=255 || val<=0) dir = -dir;
    strip.fill(strip.Color(val, 0, 0));
    strip.show();
    delay(10);
  } else {
    if (currentBtnState == HIGH) breathingGreen();
  }


  lastBtnState = currentBtnState;
}
