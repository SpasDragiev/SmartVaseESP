#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SensirionI2cScd4x.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ================= WIFI =================
const char* WIFI_SSID = "WI-FI";
const char* WIFI_PASS = "PASSWORD";   

// ================= PINS (CHECK YOUR WIRING!) =================
#define DHTPIN          25
#define DHTTYPE         DHT11
#define MOISTURE_POWER  26
#define MOISTURE_PIN    36   // ADC1 pin
#define PH_PIN          39   // ADC1 pin

// ================= CALIBRATION =================
// ESP32 ADC is 12-bit (0-4095), so these differ from the old Arduino values.
// Read the "soil raw" and "pH volt" values on Serial and set your own.
#define DRY_VAL 2817
#define WET_VAL 1700
#define PH_NEUTRAL_VOLTAGE 2.50
#define PH_ACID_VOLTAGE    2.03

// ================= TIMING =================
const unsigned long READ_INTERVAL     = 5000;
const unsigned long SCREEN_DURATION   = 1500;
const unsigned long HIST_INTERVAL     = 30000;  // one history point every 30 s
#define HIST_N 120                               // 120 points = 1 hour

// ================= OBJECTS =================
DHT dht(DHTPIN, DHTTYPE);
SensirionI2cScd4x scd;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

// ================= DATA =================
float tempSCD = 0, rhSCD = 0, phValue = 0, phVolt = 0, dhtT = 0, dhtH = 0;
uint16_t co2 = 0;
int soil = 0, soilPct = 0;
bool haveData = false;

int currentScreen = 0;
unsigned long lastScreenChange = 0, lastRead = 0, lastHist = 0;

float hT[HIST_N], hRH[HIST_N], hCO2[HIST_N], hSoil[HIST_N], hPH[HIST_N];
int hHead = 0, hCount = 0;

// ================= SENSOR FUNCTIONS =================
int readMoisture() {
  digitalWrite(MOISTURE_POWER, HIGH);
  delay(300);
  int value = analogRead(MOISTURE_PIN);
  digitalWrite(MOISTURE_POWER, LOW);
  return value;
}

int moisturePercent(int raw) {
  int pct = map(raw, DRY_VAL, WET_VAL, 0, 100);
  return constrain(pct, 0, 100);
}

float readPH() {
  int buf[10];
  for (int i = 0; i < 10; i++) {
    buf[i] = analogRead(PH_PIN);
    delay(10);
  }
  for (int i = 0; i < 9; i++)
    for (int j = i + 1; j < 10; j++)
      if (buf[i] > buf[j]) { int t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
  long sum = 0;
  for (int i = 2; i < 8; i++) sum += buf[i];
  phVolt = (float)sum / 6.0 * 3.3 / 4095.0;
  float slope = (7.0 - 4.0) / (PH_NEUTRAL_VOLTAGE - PH_ACID_VOLTAGE);
  float ph = 7.0 + slope * (phVolt - PH_NEUTRAL_VOLTAGE);
  return constrain(ph, 0, 14);
}

void pushHistory() {
  hT[hHead] = tempSCD; hRH[hHead] = rhSCD; hCO2[hHead] = co2;
  hSoil[hHead] = soilPct; hPH[hHead] = phValue;
  hHead = (hHead + 1) % HIST_N;
  if (hCount < HIST_N) hCount++;
}

// ================= LCD =================
void showScreen(int screen) {
  lcd.clear();
  switch (screen) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print(F("Temp: ")); lcd.print(tempSCD, 1); lcd.print(F("C"));
      lcd.setCursor(0, 1);
      if (tempSCD < 0)        lcd.print(F("Plant will frezz"));
      else if (tempSCD < 10)  lcd.print(F("Too cold 4 plant"));
      else if (tempSCD < 18)  lcd.print(F("Bit cold 4 plant"));
      else if (tempSCD < 24)  lcd.print(F("Plant is happy! "));
      else if (tempSCD < 28)  lcd.print(F("Plant feels warm"));
      else if (tempSCD < 33)  lcd.print(F("Too hot 4 plant!"));
      else                    lcd.print(F("Plant in danger!"));
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print(F("CO2: ")); lcd.print(co2); lcd.print(F(" ppm"));
      lcd.setCursor(0, 1);
      if (co2 < 500)          lcd.print(F("Great 4 growth! "));
      else if (co2 < 800)     lcd.print(F("Plant loves this"));
      else if (co2 < 1000)    lcd.print(F("Still OK 4 plant"));
      else if (co2 < 2000)    lcd.print(F("Open window now "));
      else                    lcd.print(F("Bad 4 ur plant! "));
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print(F("Humidity: ")); lcd.print(rhSCD, 0); lcd.print(F("%"));
      lcd.setCursor(0, 1);
      if (rhSCD < 20)         lcd.print(F("Plant is too dry"));
      else if (rhSCD < 40)    lcd.print(F("Needs more humid"));
      else if (rhSCD < 60)    lcd.print(F("Plant feels good"));
      else if (rhSCD < 70)    lcd.print(F("Nice 4 ur plant "));
      else if (rhSCD < 85)    lcd.print(F("Getting too damp"));
      else                    lcd.print(F("Too humid 4 root"));
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print(F("Soil: ")); lcd.print(soilPct); lcd.print(F("%"));
      lcd.setCursor(0, 1);
      if (soilPct < 10)       lcd.print(F("Water me NOW!!! "));
      else if (soilPct < 25)  lcd.print(F("Plant needs H2O!"));
      else if (soilPct < 50)  lcd.print(F("Soil is doing ok"));
      else if (soilPct < 75)  lcd.print(F("Soil is perfect!"));
      else if (soilPct < 90)  lcd.print(F("Ease up watering"));
      else                    lcd.print(F("Root rot risk!  "));
      break;
    case 4:
      lcd.setCursor(0, 0);
      lcd.print(F("pH: ")); lcd.print(phValue, 2); lcd.print(F("        "));
      lcd.setCursor(0, 1);
      if (phValue < 4.0)      lcd.print(F("Way too acidic! "));
      else if (phValue < 5.5) lcd.print(F("Too acid 4 roots"));
      else if (phValue < 6.5) lcd.print(F("Slightly acidic "));
      else if (phValue < 7.5) lcd.print(F("Perfect 4 plant!"));
      else if (phValue < 8.5) lcd.print(F("Slightly alkalin"));
      else                    lcd.print(F("Too alkaline!   "));
      break;
  }
}

// ================= WEB PAGE =================
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartVase</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<style>
:root{--bg:#0f1512;--card:#18211c;--text:#e8f0ea;--muted:#8fa196;--line:#2a372f;--ok:#4ade80;--warn:#fbbf24;--bad:#f87171;--accent:#34d399}
:root[data-theme=light]{--bg:#f3f7f4;--card:#ffffff;--text:#1a2a20;--muted:#5f7566;--line:#dbe5de;--ok:#16a34a;--warn:#d97706;--bad:#dc2626;--accent:#059669}
*{box-sizing:border-box}
html,body,#app{background:var(--bg);color:var(--text);margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;min-height:100%}
header{background:var(--bg);display:flex;justify-content:space-between;align-items:center;padding:16px 24px;border-bottom:1px solid var(--line)}
header h1{margin:0;font-size:1.4rem}
button{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 14px;cursor:pointer;font-size:.9rem}
.status-bar{background:var(--bg);padding:8px 24px;font-size:.85rem;color:var(--muted)}
main{padding:16px 24px 32px;max-width:1200px;margin:0 auto}
.grid{display:grid;grid-template-columns:repeat(5,1fr);gap:14px;margin-bottom:18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px}
.card .name{color:var(--muted);font-size:.8rem;text-transform:uppercase;letter-spacing:.05em}
.card .val{font-size:1.9rem;font-weight:700;margin:6px 0}
.card .st{font-weight:600;font-size:.9rem}
.card .tip{color:var(--muted);font-size:.8rem;margin-top:6px;line-height:1.35}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.charts{display:grid;grid-template-columns:repeat(2,1fr);gap:14px}
.chartbox{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;height:260px}
.extra{color:var(--muted);font-size:.8rem;margin:0 0 18px}
@media(max-width:900px){.grid{grid-template-columns:repeat(2,1fr)}.charts{grid-template-columns:1fr}}
@media(max-width:600px){header,main,.status-bar{padding-left:14px;padding-right:14px}.card .val{font-size:1.6rem}.chartbox{height:220px}}
@media(max-width:380px){.grid{grid-template-columns:1fr}header h1{font-size:1.15rem}button{padding:6px 10px}}
</style>
</head>
<body>
<div id="app">
<header><h1>SmartVase</h1><button id="theme">Toggle theme</button></header>
<div class="status-bar" id="st">Connecting...</div>
<main>
<div class="grid" id="cards"></div>
<p class="extra" id="extra"></p>
<div class="charts" id="charts"></div>
</main>
</div>
<script>
const $=id=>document.getElementById(id);
const root=document.documentElement;
const cfg=[
 {k:'temp',name:'Temperature',unit:' °C',d:1,color:'#f97316',
  s:v=>v<0?['Will freeze','bad','Move the plant somewhere warm immediately.']:v<10?['Too cold','bad','Keep it away from windows and drafts.']:v<18?['A bit cold','warn','Most houseplants prefer 18-27 °C.']:v<24?['Happy!','ok','Ideal range for most plants.']:v<28?['Warm','ok','Fine, just keep the soil from drying out.']:v<33?['Too hot','warn','Shade it and water a little more often.']:['Danger!','bad','Move it out of the heat now.']},
 {k:'co2',name:'CO₂',unit:' ppm',d:0,color:'#a78bfa',
  s:v=>v<500?['Great','ok','Fresh air, excellent for growth.']:v<800?['Loves this','ok','Good air quality.']:v<1000?['OK','ok','Consider ventilating soon.']:v<2000?['Ventilate','warn','Open a window for a few minutes.']:['Bad','bad','Air is stale. Ventilate now.']},
 {k:'rh',name:'Humidity',unit:' %',d:0,color:'#38bdf8',
  s:v=>v<20?['Too dry','bad','Mist the leaves or use a humidifier.']:v<40?['Needs more','warn','Group plants or add a water tray.']:v<60?['Feels good','ok','Comfortable range.']:v<70?['Nice','ok','Good for tropical plants.']:v<85?['Getting damp','warn','Improve airflow to avoid mould.']:['Too humid','bad','Roots and leaves may rot. Ventilate.']},
 {k:'soil',name:'Soil moisture',unit:' %',d:0,color:'#84cc16',
  s:v=>v<10?['Water NOW','bad','Water thoroughly until it drains.']:v<25?['Needs water','warn','Give it a good watering soon.']:v<50?['Doing ok','ok','Check again in a day or two.']:v<75?['Perfect','ok','Do not change anything.']:v<90?['Ease up','warn','Skip the next watering.']:['Root rot risk','bad','Stop watering and check drainage.']},
 {k:'ph',name:'Soil pH',unit:'',d:2,color:'#f472b6',
  s:v=>v<4?['Way too acidic','bad','Add garden lime or repot in fresh soil.']:v<5.5?['Too acidic','warn','Consider a little lime.']:v<6.5?['Slightly acidic','ok','Great for many plants.']:v<7.5?['Perfect','ok','Ideal range for most plants.']:v<8.5?['Slightly alkaline','warn','Some plants may struggle to take up iron.']:['Too alkaline','bad','Acidify with peat or sulfur.']}
];
$('cards').innerHTML=cfg.map(c=>`<div class="card"><div class="name">${c.name}</div><div class="val" id="v_${c.k}">--</div><div class="st" id="s_${c.k}">--</div><div class="tip" id="t_${c.k}"></div></div>`).join('');
$('charts').innerHTML=cfg.map(c=>`<div class="chartbox"><canvas id="c_${c.k}"></canvas></div>`).join('');

Chart.defaults.backgroundColor='rgba(0,0,0,0)';
const charts={};
function themeColors(){const cs=getComputedStyle(root);Chart.defaults.color=cs.getPropertyValue('--muted').trim();Chart.defaults.borderColor=cs.getPropertyValue('--line').trim();}
function applyTheme(t){root.setAttribute('data-theme',t);try{localStorage.setItem('sv-theme',t)}catch(e){}themeColors();Object.values(charts).forEach(c=>{c.options.scales.x.grid.color=Chart.defaults.borderColor;c.options.scales.y.grid.color=Chart.defaults.borderColor;c.update('none')});}
let saved='dark';try{saved=localStorage.getItem('sv-theme')||'dark'}catch(e){}
root.setAttribute('data-theme',saved);themeColors();
cfg.forEach(c=>{
 charts[c.k]=new Chart($('c_'+c.k),{type:'line',
  data:{labels:[],datasets:[{label:c.name,data:[],borderColor:c.color,backgroundColor:'rgba(0,0,0,0)',borderWidth:2,tension:.35,pointRadius:0}]},
  options:{responsive:true,maintainAspectRatio:false,animation:false,
   plugins:{legend:{labels:{usePointStyle:true,pointStyle:'line'}}},
   scales:{x:{ticks:{maxTicksLimit:6},grid:{color:Chart.defaults.borderColor}},y:{grid:{color:Chart.defaults.borderColor}}}}});
});
$('theme').onclick=()=>applyTheme(root.getAttribute('data-theme')==='dark'?'light':'dark');

async function update(){
 try{
  const d=await (await fetch('/api')).json();
  cfg.forEach(c=>{
   const v=d[c.k];const r=c.s(v);
   $('v_'+c.k).textContent=v.toFixed(c.d)+c.unit;
   const s=$('s_'+c.k);s.textContent=r[0];s.className='st '+r[1];
   $('t_'+c.k).textContent=r[2];
   const h=d.hist[c.k],n=h.length;
   charts[c.k].data.labels=h.map((_,i)=>i===n-1?'now':(-(n-1-i)*d.step/60).toFixed(1)+'m');
   charts[c.k].data.datasets[0].data=h;
   charts[c.k].update('none');
  });
  $('extra').textContent='DHT11: '+d.dhtT.toFixed(1)+' °C / '+d.dhtH.toFixed(0)+' %  |  Soil raw: '+d.soilRaw+'  |  pH voltage: '+d.phV.toFixed(3)+' V';
  $('st').textContent='Live · updated '+new Date().toLocaleTimeString();
 }catch(e){$('st').textContent='Offline, reconnecting...';}
}
update();setInterval(update,5000);
</script>
</body>
</html>
)rawliteral";

// ================= WEB HANDLERS =================
void addArr(String &j, const char *name, float *a, int dec) {
  j += "\""; j += name; j += "\":[";
  int start = (hHead - hCount + HIST_N) % HIST_N;
  for (int i = 0; i < hCount; i++) {
    if (i) j += ",";
    j += String(a[(start + i) % HIST_N], dec);
  }
  j += "]";
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleApi() {
  String j;
  j.reserve(4096);
  j += "{\"temp\":";   j += String(tempSCD, 1);
  j += ",\"rh\":";     j += String(rhSCD, 1);
  j += ",\"co2\":";    j += String(co2);
  j += ",\"soil\":";   j += String(soilPct);
  j += ",\"soilRaw\":"; j += String(soil);
  j += ",\"ph\":";     j += String(phValue, 2);
  j += ",\"phV\":";    j += String(phVolt, 3);
  j += ",\"dhtT\":";   j += String(dhtT, 1);
  j += ",\"dhtH\":";   j += String(dhtH, 0);
  j += ",\"step\":";   j += String(HIST_INTERVAL / 1000);
  j += ",\"hist\":{";
  addArr(j, "temp", hT, 1);   j += ",";
  addArr(j, "rh", hRH, 1);    j += ",";
  addArr(j, "co2", hCO2, 0);  j += ",";
  addArr(j, "soil", hSoil, 0); j += ",";
  addArr(j, "ph", hPH, 2);
  j += "}}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(MOISTURE_POWER, OUTPUT);
  digitalWrite(MOISTURE_POWER, LOW);
  analogReadResolution(12);

  Wire.begin();          // SDA 21, SCL 22
  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Starting..."));

  scd.begin(Wire, SCD41_I2C_ADDR_62);
  scd.stopPeriodicMeasurement();
  delay(500);
  scd.startPeriodicMeasurement();

  // WiFi
  lcd.clear();
  lcd.print(F("WiFi..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300);
    Serial.print(".");
  }
  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("\nIP: ")); Serial.println(WiFi.localIP());
    lcd.print(F("Open in browser:"));
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
  } else {
    Serial.println(F("\nWiFi failed"));
    lcd.print(F("WiFi failed"));
  }
  delay(3000);

  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.begin();

  lcd.clear();
  lcd.print(F("System Ready"));
  delay(1000);
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  if (millis() - lastRead >= READ_INTERVAL) {
    lastRead = millis();

    soil    = readMoisture();
    soilPct = moisturePercent(soil);
    phValue = readPH();

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) dhtT = t;
    if (!isnan(h)) dhtH = h;

    bool ready = false;
    uint16_t c = 0; float tt = 0, hh = 0;
    scd.getDataReadyStatus(ready);
    if (ready && scd.readMeasurement(c, tt, hh) == 0 && c != 0) {
      co2 = c; tempSCD = tt; rhSCD = hh;
      haveData = true;
    } else if (!ready) {
      Serial.println(F("SCD40 not ready"));
    } else {
      Serial.println(F("SCD40 read error"));
    }

    Serial.print(F("Temp: "));      Serial.print(tempSCD);
    Serial.print(F(" C | Hum: "));  Serial.print(rhSCD);
    Serial.print(F(" % | CO2: ")); Serial.print(co2);
    Serial.print(F(" ppm | Soil raw: ")); Serial.print(soil);
    Serial.print(F(" | Soil: "));  Serial.print(soilPct);
    Serial.print(F("% | pH V: ")); Serial.print(phVolt, 3);
    Serial.print(F(" | pH: "));    Serial.println(phValue, 2);

    if (haveData && (hCount == 0 || millis() - lastHist >= HIST_INTERVAL)) {
      lastHist = millis();
      pushHistory();
    }
  }

  if (millis() - lastScreenChange >= SCREEN_DURATION) {
    lastScreenChange = millis();
    currentScreen = (currentScreen + 1) % 5;
    showScreen(currentScreen);
  }
}