/*
 CHEMTRACE SIMPLE ESP32
 pH -> GPIO34 | TDS -> GPIO35 | DS18B20 -> GPIO4
 Libraries: OneWire and DallasTemperature
 Replace Wi-Fi details and calibrate pH/TDS before relying on readings.
*/
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

const char* WIFI_SSID = "thxbby";
const char* WIFI_PASSWORD = "diktieppp";
const char* AP_NAME = "CHEMTRACE";
const char* AP_PASSWORD = "chemtrace123";

const int PH_PIN = 34, TDS_PIN = 35, TEMP_PIN = 4;

// EXAMPLE calibration voltages only. Measure these with pH 7 and pH 4 buffers.
float PH7_VOLTAGE = 2.50;
float PH4_VOLTAGE = 3.00;
// Adjust TDS_FACTOR using a known TDS calibration solution.
float TDS_FACTOR = 1.00;
float TDS_SCALE = 0.50;

WebServer server(80);
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

float phVoltage, tdsVoltage, phValue = NAN, tdsValue = NAN, tempC = NAN;
unsigned long lastRead = 0;

float readVoltage(int pin) {
  uint32_t total = 0;
  for (int i = 0; i < 15; i++) {
    total += analogReadMilliVolts(pin);
    delay(3);
  }
  return (total / 15.0) / 1000.0;
}

float calculatePH(float voltage) {
  float diff = PH4_VOLTAGE - PH7_VOLTAGE;
  if (fabs(diff) < 0.001) return NAN;
  float value = 7.0 + (voltage - PH7_VOLTAGE) * ((4.0 - 7.0) / diff);
  if (value < 0 || value > 14) return NAN;
  return value;
}

float calculateTDS(float voltage, float temperature) {
  if (isnan(temperature)) temperature = 25.0;
  float compensated = voltage / (1.0 + 0.02 * (temperature - 25.0));
  float value = (133.42 * pow(compensated, 3)
               - 255.86 * pow(compensated, 2)
               + 857.39 * compensated) * TDS_SCALE;
  value *= TDS_FACTOR;
  return max(0.0f, value);
}

String jsonValue(float v, int decimals) {
  if (isnan(v) || isinf(v)) return "null";
  return String(v, decimals);
}

void readSensors() {
  phVoltage = readVoltage(PH_PIN);
  tdsVoltage = readVoltage(TDS_PIN);
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  tempC = (t == DEVICE_DISCONNECTED_C || t < -50 || t > 125) ? NAN : t;
  phValue = calculatePH(phVoltage);
  tdsValue = calculateTDS(tdsVoltage, tempC);
}

void handleData() {
  String json = "{";
  json += "\"ph\":" + jsonValue(phValue, 2) + ",";
  json += "\"tds\":" + jsonValue(tdsValue, 0) + ",";
  json += "\"temperature\":" + jsonValue(tempC, 1);
  json += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CHEMTRACE Water Monitor</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#f2f6f2;color:#26382e;font-family:Arial,sans-serif}
header{background:#245b42;color:white;text-align:center;padding:25px 15px}
h1{margin:0;letter-spacing:2px;font-size:25px}header p{margin:7px 0 0;color:#d9e9de;font-size:12px}
main{max-width:820px;margin:35px auto;padding:0 16px}.intro{text-align:center;margin-bottom:25px}
h2{font-size:22px;margin:0 0 8px}.intro p{color:#718077;font-size:14px;margin:0;line-height:1.5}
.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}
.card{background:white;border:1px solid #dce6dd;border-radius:12px;padding:25px 12px;text-align:center}
.card h3{font-size:12px;color:#718077;letter-spacing:1px;margin:0 0 17px}
.value{font-size:36px;font-weight:bold;color:#28734e}.unit{font-size:14px;color:#718077;font-weight:normal}
.desc{font-size:11px;color:#89968d;margin-top:7px}
.status{background:white;border:1px solid #dce6dd;border-radius:10px;margin-top:18px;padding:14px;text-align:center;font-size:13px;color:#557261}
.dot{display:inline-block;width:8px;height:8px;background:#38a36b;border-radius:50%;margin-right:7px}
.note{text-align:center;color:#78867c;font-size:12px;line-height:1.6;margin:22px 5px}
footer{text-align:center;padding:18px;color:#87948a;font-size:11px}
@media(max-width:600px){.cards{grid-template-columns:1fr}.card{padding:19px}.card h3{margin-bottom:10px}.value{font-size:32px}main{margin-top:25px}}
</style></head><body>
<header><h1>CHEMTRACE</h1><p>FRESHWATER MONITORING SYSTEM</p></header>
<main><div class="intro"><h2>Water readings</h2>
<p>Live measurements from your CHEMTRACE monitoring station.</p></div>
<div class="cards">
<div class="card"><h3>pH LEVEL</h3><div class="value" id="ph">--</div><div class="desc">Acidity / alkalinity</div></div>
<div class="card"><h3>TOTAL DISSOLVED SOLIDS</h3><div class="value"><span id="tds">--</span> <span class="unit">ppm</span></div><div class="desc">Estimated dissolved substances</div></div>
<div class="card"><h3>WATER TEMPERATURE</h3><div class="value"><span id="temp">--</span> <span class="unit">°C</span></div><div class="desc">DS18B20 sensor</div></div>
</div>
<div class="status" id="status"><span class="dot"></span>Connecting to CHEMTRACE...</div>
<div class="note">Readings refresh every 2 seconds. These sensors help identify changes in water conditions, but cannot identify every individual pollutant. Calibrate sensors before using results for environmental decisions.</div>
</main><footer>CHEMTRACE · Monitoring freshwater ecosystems</footer>
<script>
function fmt(v,n){return v===null||v===undefined?"--":Number(v).toFixed(n)}
async function update(){
 try{
  const response=await fetch("/data?t="+Date.now(),{cache:"no-store"});
  if(!response.ok)throw Error("offline");
  const d=await response.json();
  document.getElementById("ph").textContent=fmt(d.ph,2);
  document.getElementById("tds").textContent=fmt(d.tds,0);
  document.getElementById("temp").textContent=fmt(d.temperature,1);
  document.getElementById("status").innerHTML='<span class="dot"></span>Connected · Updating automatically';
 }catch(e){document.getElementById("status").textContent="Connection lost. Reconnecting...";}
}
update();setInterval(update,2000);
</script></body></html>
)HTML";

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  tempSensor.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Open: http://"); Serial.println(WiFi.localIP());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_NAME, AP_PASSWORD);
    Serial.println("Router Wi-Fi failed. Connect to CHEMTRACE network.");
    Serial.print("Open: http://"); Serial.println(WiFi.softAPIP());
  }

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PAGE);
  });
  server.on("/data", HTTP_GET, handleData);
  server.begin();
  readSensors();
  Serial.println("CHEMTRACE ready.");
}

void loop() {
  server.handleClient();
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    readSensors();
    Serial.print("pH: "); Serial.print(phValue);
    Serial.print(" | TDS: "); Serial.print(tdsValue);
    Serial.print(" ppm | Temp: "); Serial.println(tempC);
  }
}
