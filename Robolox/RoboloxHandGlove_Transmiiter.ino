#include <Wire.h>
#include <MPU6050_6Axis_MotionApps20.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define INTERRUPT_PIN 2
#define LED_PIN 13

MPU6050 mpu;
bool blinkState = false;
bool dmpReady = false;
uint8_t mpuIntStatus;
uint8_t devStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
float ypr[3];
float pitch = 0;
float roll = 0;
float yaw = 0;
int x;
int y;

volatile bool mpuInterrupt = false;
void dmpDataReady() {
  mpuInterrupt = true;
}

typedef struct struct_message {
  int x;
  int y;
  int score;
} struct_message;

struct_message myData;
struct_message incomingData;
uint8_t receiverAddress[] = {0x30, 0xC9, 0x22, 0x28, 0x47, 0xF8};  // Receiver MAC Address

// Callback function executed when data is sent
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// Callback function executed when data is received
//void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
//  memcpy(&incomingData, data, sizeof(incomingData));
//  Serial.print("Score received: ");
//  Serial.println(incomingData.score);
//  display.clearDisplay();
//  display.setCursor(0, 0);
//  display.print("Score:");
//  display.setCursor(0, 10);
//  display.print(incomingData.score);
//  display.display();
//}

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
  memcpy(&incomingData, data, sizeof(incomingData));
  Serial.print("Score received: ");
  Serial.println(incomingData.score);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Score:");
  display.setCursor(0, 10);
  display.print(incomingData.score);
  display.display();
}




void setup() {
  Wire.begin();
  Wire.setClock(400000);

  Serial.begin(115200);
  while (!Serial);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.display();
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Score:");
  display.display();

  Serial.println(F("Initializing I2C devices..."));
  mpu.initialize();
  pinMode(INTERRUPT_PIN, INPUT);

  Serial.println(F("Testing device connections..."));
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

  Serial.println(F("Initializing DMP..."));
  devStatus = mpu.dmpInitialize();

  mpu.setXGyroOffset(126);
  mpu.setYGyroOffset(57);
  mpu.setZGyroOffset(-69);
  mpu.setZAccelOffset(1869);

  if (devStatus == 0) {
    Serial.println(F("Enabling DMP..."));
    mpu.setDMPEnabled(true);

    Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();

    Serial.println(F("DMP ready! Waiting for first interrupt..."));
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    Serial.print(F("DMP Initialization failed (code "));
    Serial.print(devStatus);
    Serial.println(F(")"));
  }

  pinMode(LED_PIN, OUTPUT);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the send callback
  esp_now_register_send_cb(onDataSent);

  // Register the receive callback
  esp_now_register_recv_cb(onDataRecv);

  // Register peer (receiver) MAC address
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void loop() {
  if (!dmpReady) return;

  while (!mpuInterrupt && fifoCount < packetSize) {}

  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();
  fifoCount = mpu.getFIFOCount();

  if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
    mpu.resetFIFO();
    Serial.println(F("FIFO overflow!"));
  } else if (mpuIntStatus & 0x02) {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;

    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    yaw = ypr[0] * 180 / M_PI;
    pitch = ypr[1] * 180 / M_PI;
    roll = ypr[2] * 180 / M_PI;

    if (roll > -100 && roll < 100)
      x = map(roll, -100, 100, 0, 100);

    if (pitch > -100 && pitch < 100)
      y = map(pitch, -100, 100, 100, 200);

    myData.x = x;
    myData.y = y;

    esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)&myData, sizeof(myData));

    if (result == ESP_OK) {
      Serial.println("Sent with success");
    } else {
      Serial.println("Error sending the data");
    }

    Serial.print("X: ");
    Serial.print(x);
    Serial.print("\tY: ");
    Serial.println(y);

    blinkState = !blinkState;
    digitalWrite(LED_PIN, blinkState);
  }
}