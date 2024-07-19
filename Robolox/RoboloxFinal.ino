#include <Wire.h>
#include <esp_now.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_TCS34725.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>

// Motor pins
const int motor1Pin1 = 27;
const int motor1Pin2 = 26;
const int motor2Pin1 = 25;
const int motor2Pin2 = 33;
const int enA = 14;
const int enB = 12;

// IR sensor pins
#define IR_SENSOR_1 2
#define IR_SENSOR_2 4
#define IR_SENSOR_3 5
#define IR_SENSOR_4 13

// Initialize the I2C LCD with the I2C address 0x27 and size 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Initialize color sensor
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_600MS, TCS34725_GAIN_1X);

AsyncWebServer server(80); // Create a web server on port 80
AsyncWebSocket ws("/ws"); // Create a WebSocket instance

int score = 0;
bool gameRestart = true; // Track if the game needs to restart
bool gameRestartIR = true;//Ir sensor detection 

bool colorDetected[5] = {false, false, false, false, false}; // Track detected colors
unsigned long lastBlackDetectedTime = 0;
String Message ="None";
String lastColor = "None";
String lastIRPenalty = "None";
int lastColorPoints = 0;


// HTML code for the webpage
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Game Score</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" type="text/css" href="/style.css">
  <script src="/script.js"></script>
</head>
<body>
   
   <header>
        <h1 id="roboloxname"> ROBOLOX </h1>
        
        <h2 id="scoreboard">Game Score Dashboard</h2>
    </header>

  <h1>Game Score Dashboard</h1>
  <div id="score-container">
    <h2><span id="Message"></span></h2>
    <h2 id="tpoints">Total Score: <span id="total-points"></span></h2>
    <h2>Color Detected: <span id="color-detected"></span></h2>
    <h2 id="cpoints">Points Added: <span id="color-points"></span></h2>
    <h2 id="irpoints">Penalty Points: <span id="ir-penalty"></span></h2>
    
  </div>
</body>
</html>
)rawliteral";

// CSS code for styling the webpage
const char style_css[] PROGMEM = R"rawliteral(
body {
  font-family: Arial, sans-serif;
  text-align: center;
  background-color: #f0f0f0;
  color: #050505;
  background-color: rgb(189, 194, 199);

}

#roboloxname{
    font-family:Georgia, 'Times New Roman', Times, serif;
    font-size: 3.0 em;
}

#score-container {
  display: inline-block;
  margin-top: 100px;
  width: 300px;
  border: 10px solid rgb(11, 11, 11);
  padding: 20px;
  padding-right: 80px;
  padding-top: 50px;
  margin: 20px;
}

h1 {
  font-size: 3.5em;
  margin-top: 50px;
  
}

h2 {
  font-size: 1.5em;
}

#scoreboard{
    font-size: 2.5em;
}

#color-points,#cpoints{
    color: darkgreen;
}
#ir-penalty, #irpoints{
    color: crimson;
}

#tpoints{
    color: blue;
}
)rawliteral";

// JavaScript code for real-time updates using WebSocket
const char script_js[] PROGMEM = R"rawliteral(
var websocket = new WebSocket('ws://' + window.location.hostname + '/ws');
websocket.onmessage = function(event) {
  var jsonResponse = JSON.parse(event.data);
  document.getElementById("total-points").innerHTML = jsonResponse.score;
  document.getElementById("color-detected").innerHTML = jsonResponse.lastColor;
  document.getElementById("color-points").innerHTML = jsonResponse.lastColorPoints;
  document.getElementById("ir-penalty").innerHTML = jsonResponse.lastIRPenalty;
  document.getElementById("Message").innerHTML = jsonResponse.Message;
};
)rawliteral";


//
typedef struct struct_message {
  int x;
  int y;
  int score;
} struct_message;

struct_message myData;
struct_message incomingData;
uint8_t transmitterAddress[] = {0x48, 0xE7, 0x29, 0x98, 0xBB, 0xF0};  // Transmitter MAC Address

// Callback function executed when data is sent
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// Callback when data is received
void onDataRecv(const esp_now_recv_info *recvInfo, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("X: ");
  Serial.print(incomingData.x);
  Serial.print("\tY: ");
  Serial.println(incomingData.y);

  // Control motors based on direction
  if ((incomingData.x >= 45 && incomingData.x <= 55) && (incomingData.y >= 145 && incomingData.y <= 155)) {
    stopCar();
    Serial.println("stop");
  } else if (incomingData.x > 60) {
    moveRight();
    Serial.println("right");
  } else if (incomingData.x < 40) {
    moveLeft();
    Serial.println("left");
  } else if (incomingData.y > 160) {
    moveBackward();
    Serial.println("reverse");
  } else if (incomingData.y < 140) {
    moveForward();
    Serial.println("forward");
  } else {
    stopCar();
    Serial.println("stop");
  }
}

void setup() {
  Serial.begin(115200);

  // Set motor pins as output
  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  pinMode(motor2Pin1, OUTPUT);
  pinMode(motor2Pin2, OUTPUT);
  pinMode(enA, OUTPUT);
  pinMode(enB, OUTPUT);

  // Set IR sensor pins as input
  pinMode(IR_SENSOR_1, INPUT);
  pinMode(IR_SENSOR_2, INPUT);
  pinMode(IR_SENSOR_3, INPUT);
  pinMode(IR_SENSOR_4, INPUT);

  // Initialize the LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  // Initialize color sensor
  if (tcs.begin()) {
    Serial.println("Found sensor");
    lcd.setCursor(0, 0);
    lcd.print("Found sensor");
    delay(2000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Game Start!");
    lcd.setCursor(0, 1);
    lcd.print("Good Luck!");
  } else {
    Serial.println("No TCS34725 found ... check your connections");
    lcd.setCursor(0, 0);
    lcd.print("No sensor found");
    lcd.setCursor(0, 1);
    lcd.print("Check connections");
    while (1);
  }

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.begin("JAVA", "12345678");

  // Init ESP-NOW
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
  memcpy(peerInfo.peer_addr, transmitterAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  // Initial score
  updateScore();

  // Serve the HTML webpage
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Serve the CSS file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/css", style_css);
  });

  // Serve the JavaScript file
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "application/javascript", script_js);
  });

  // Add WebSocket event handler
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
}

void loop() {
  ws.cleanupClients(); // Keep WebSocket clients connected


  // Read IR sensors
  int sensor1 = digitalRead(IR_SENSOR_1);
  int sensor2 = digitalRead(IR_SENSOR_2);
  int sensor3 = digitalRead(IR_SENSOR_3);
  int sensor4 = digitalRead(IR_SENSOR_4);

  int blackDetected = sensor1 + sensor2 + sensor3 + sensor4;

if(!gameRestartIR){
  // IR sensor logic
  if (blackDetected > 0) {
    //lastBlackDetectedTime = millis();
    if (blackDetected == 1) {
      score -= 5;
      lastIRPenalty = "-5 for 1 black line";
      //delay(4000);
    } else if (blackDetected == 2) {
      score -= 10;
      lastIRPenalty = "-10 for 2 black lines";
      //delay(4000);
    } else {
      Serial.println("Over 1");
      Message = "Game Over";
      Serial.println("Game over");
      stopCar();
      endGame();
      lastIRPenalty = "Game Over";
      
      delay(10000);
      score = 10;
      return; // End the game, no further code execution in loop
    }
    updateScore();
    delay(2000);
  }
  }

  
  // Color sensor logic
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);

  // Calculate ratios
  float red_ratio = (float)r / (r + g + b);
  float green_ratio = (float)g / (r + g + b);
  float blue_ratio = (float)b / (r + g + b);

  String color = determineColor(r, g, b);
  Serial.print("Detected Color: ");
  Serial.println(color);

  // lcd.clear();
  // lcd.setCursor(0, 0);
  // lcd.print("Color: " + color);
 
  if (color == "Red" && !gameRestart) {
    lastColor = "Red";
    lastColorPoints = 0;
    colorDetected[3] = true;
    Message = "Game Over!";
    
    stopCar();
    
    Serial.print("Final score: ");
    Serial.println(score);
    lcd.print("Red! Game Over!");
    updateScore();
    //Serial.println("Game Over!");
    endGame();
    delay(100);
    score=0;
    gameRestart = true;
    gameRestartIR = true;
    return;

  } else if (color == "Blue" && gameRestart) {
    lastColor = "Blue";
    score = 10;
    Message = "Game Start! Good Luck!";
    lastColorPoints = 10;
    
    
    
    Serial.println("Game Start! Good Luck!");
    lcd.setCursor(0, 0);
    lcd.print("Game Start!");
    lcd.setCursor(0, 1);
    lcd.print("Good Luck!");
    //delay(4000);
    gameRestart = false;
    gameRestartIR = false;
    

  }
  else if (!gameRestart){
    if (color == "Green" && !colorDetected[0]) {
    score += 20;
    colorDetected[0] = true;
    lastColor = "Green";
    lastColorPoints = 20;

    Serial.println("20 added.");
    lcd.print("Green");
    updateScore();
    }
    
    else if (color == "Purple" && !colorDetected[1]) {
    score += 30;
    colorDetected[1] = true;
    lastColor = "Purple";
    lastColorPoints = 30;
    Serial.println("30 added.");
    lcd.print("Purple");
    updateScore();
  } else if (color == "Silver" && !colorDetected[2]) {
    score += 40;
    colorDetected[2] = true;
    lastColor = "Silver";
    lastColorPoints = 40;
    Serial.println("40 added.");
    lcd.print("Silver");
    updateScore();
  } else if (color == "Unknown" && alreadyDetected(color)) {
    score += 0;
    Serial.println("Color already detected, no points added.");
    updateScore();
  }

  }

}

void updateScore() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Score: " + String(score));

  // Send score data via ESP-NOW
  myData.score = score;
  esp_now_send(transmitterAddress, (uint8_t *)&myData, sizeof(myData));

  // Send score data via WebSocket
  String jsonString = createJsonString();
  ws.textAll(jsonString); // Broadcast to all connected clients
}

void endGame() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Game Over!");
  lcd.setCursor(0, 1);
  lcd.print("Score: " + String(score));

  // Send score data via WebSocket
  String jsonString = createJsonString();
  ws.textAll(jsonString); // Broadcast to all connected clients

  delay(10000); // Delay for displaying final score
}

String determineColor(uint16_t r, uint16_t g, uint16_t b) {
 // Normalize the values
  uint32_t sum = r + g + b;
  if (sum == 0) {
    return "Unknown";
  }
  float red_ratio = (float)r / sum;
  float green_ratio = (float)g / sum;
  float blue_ratio = (float)b / sum;

  // Determine color based on dominant color ratio
  if (red_ratio > 0.4 && red_ratio > green_ratio && red_ratio > blue_ratio) {
    return "Red";  ///ok
  } else if (green_ratio > 0.4 && green_ratio > red_ratio && green_ratio > blue_ratio) {
    return "Green"; ///ok
  } else if (blue_ratio > 0.4 && blue_ratio > red_ratio && blue_ratio > green_ratio) {
    return "Blue"; 
  } else if (red_ratio > 0.2 && red_ratio < 0.3 && blue_ratio > 0.35 && blue_ratio < 0.4 && green_ratio > 0.34 && green_ratio < 0.37) {
    return "Purple";  ///ok
  } else if (red_ratio > 0.25 && red_ratio < 0.284 && green_ratio > 0.39 and green_ratio < 0.41 && blue_ratio > 0.32 and blue_ratio < 0.338) {
    return "Silver";
  } else {
    return "Unknown";
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected");
  } else if (type == WS_EVT_DATA) {
    // Handle WebSocket data received
  }
}

String createJsonString() {
  StaticJsonDocument<200> doc;
  doc["score"] = score;
  doc["lastColor"] = lastColor;
  doc["lastColorPoints"] = lastColorPoints;
  doc["lastIRPenalty"] = lastIRPenalty;
  doc["Message"] = Message;

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

bool alreadyDetected(String color) {
  if (color == "Green") {
    return colorDetected[1];
  } else if (color == "Purple") {
    return colorDetected[2];
  } else if (color == "Silver") {
    return colorDetected[3];
  } else {
    return false;
  }
}


void moveForward() {
  digitalWrite(motor1Pin1, HIGH);
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(motor2Pin1, HIGH);
  digitalWrite(motor2Pin2, LOW);
  analogWrite(enA, 255); // Full speed
  analogWrite(enB, 255); // Full speed
}

void moveBackward() {
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, HIGH);
  digitalWrite(motor2Pin1, LOW);
  digitalWrite(motor2Pin2, HIGH);
  analogWrite(enA, 255); // Full speed
  analogWrite(enB, 255); // Full speed
}

void moveLeft() {
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, HIGH);
  digitalWrite(motor2Pin1, HIGH);
  digitalWrite(motor2Pin2, LOW);
  analogWrite(enA, 255); // Full speed
  analogWrite(enB, 255); // Full speed
}

void moveRight() {
  digitalWrite(motor1Pin1, HIGH);
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(motor2Pin1, LOW);
  digitalWrite(motor2Pin2, HIGH);
  analogWrite(enA, 255); // Full speed
  analogWrite(enB, 255); // Full speed
}

void stopCar() {
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(motor2Pin1, LOW);
  digitalWrite(motor2Pin2, LOW);
  analogWrite(enA, 0); // Stop
  analogWrite(enB, 0); // Stop
}