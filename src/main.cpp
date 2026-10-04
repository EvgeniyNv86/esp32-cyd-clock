/*
 * CYD Clock + Weather + Sunrise/Sunset + Rotation
 * for ESP32-2432S028 (Cheap Yellow Display)
 * by Evgeniy - Shitov
 */

#include <CYD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>

CYD tft;

// --- Wi-Fi settings: REPLACE with your credentials ---
const char* ssid     = "WIFI_SSID_HERE";
const char* password = "WIFI_PASS_HERE";

const float LAT = 60.93;
const float LON = 76.28;
const int   TZ_OFFSET = 5;

int cx, cy, radius;
int prev_sec = -1;
float prev_ha = 0, prev_ma = 0, prev_sa = 0;
int brightness = 200;
int bri_level = 0;
bool autoBrightness = true;
unsigned long lastLightCheck = 0;
int smoothLight = 0;

String weatherTemp = "--";
String weatherCond = "--";
String weatherIcon = "SUN";
unsigned long lastWeatherUpdate = 0;

int sunriseH = 0, sunriseM = 0;
int sunsetH = 0,  sunsetM = 0;
bool isDaytime = true;
int prevSunCalcDay = -1;

bool isLandscape;
int panelX;

#define LDR_PIN 34

#define C_BG    0x0000
#define C_FACE  0x2104
#define C_RING  0xFD20
#define C_TICK  0xFFFF
#define C_TICKM 0xC618
#define C_HOUR  0xFFFF
#define C_MIN   0xC618
#define C_SEC   0xF800
#define C_TEXT  0xEF7D
#define C_DATE  0xBDF7
#define C_SIGN  0x528A
#define C_WEATH 0xFFE0
#define C_COND  0x07FF
#define C_SUN   0xFD20
#define C_MOON  0xC618
#define C_SR    0xFDA0
#define C_SS    0x4A1F
#define C_CLOUD 0xC618
#define C_RAIN  0x07FF
#define C_SNOW  0xFFFF
#define C_STORM 0xFFE0
#define C_FOG   0xBDF7

float deg2rad(float d) { return d * PI / 180.0; }
float rad2deg(float r) { return r * 180.0 / PI; }

void setupLayout() {
  int rot = tft.getRotation();
  isLandscape = (rot % 2 == 1);
  if (!isLandscape) {
    radius = 95;
    cx = tft.width() / 2;
    cy = tft.height() / 2 + 5;
    panelX = tft.width();
  } else {
    radius = 90;
    cx = 105;
    cy = tft.height() / 2;
    panelX = 205;
  }
}

int dayOfYear(int year, int month, int day) {
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
    daysInMonth[1] = 29;
  int doy = day;
  for (int i = 0; i < month - 1; i++)
    doy += daysInMonth[i];
  return doy;
}

void calcSunTimes(int year, int month, int day) {
  int doy = dayOfYear(year, month, day);
  bool leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
  int daysInYear = leap ? 366 : 365;
  float gamma = (2.0 * PI / daysInYear) * (doy - 1);
  float eqTime = 229.18 * (0.000075
    + 0.001868 * cos(gamma)
    - 0.032077 * sin(gamma)
    - 0.014615 * cos(2 * gamma)
    - 0.040849 * sin(2 * gamma));
  float decl = (0.006918
    - 0.399912 * cos(gamma)
    + 0.070257 * sin(gamma)
    - 0.006758 * cos(2 * gamma)
    + 0.000907 * sin(2 * gamma)
    - 0.002697 * cos(3 * gamma)
    + 0.00148  * sin(3 * gamma));
  float latRad = deg2rad(LAT);
  float zenith = deg2rad(90.833);
  float cosHa = (cos(zenith) / (cos(latRad) * cos(decl))) - (tan(latRad) * tan(decl));
  if (cosHa > 1.0) { sunriseH = -1; sunsetH = -1; return; }
  if (cosHa < -1.0) { sunriseH = -2; sunsetH = -2; return; }
  float ha = acos(cosHa);
  float haDeg = rad2deg(ha);
  float sunriseMin = 720.0 - 4.0 * (LON + haDeg) - eqTime + TZ_OFFSET * 60.0;
  float sunsetMin  = 720.0 - 4.0 * (LON - haDeg) - eqTime + TZ_OFFSET * 60.0;
  while (sunriseMin < 0)    sunriseMin += 1440;
  while (sunriseMin >= 1440) sunriseMin -= 1440;
  while (sunsetMin < 0)     sunsetMin += 1440;
  while (sunsetMin >= 1440)  sunsetMin -= 1440;
  sunriseH = (int)(sunriseMin / 60);
  sunriseM = (int)(sunriseMin - sunriseH * 60);
  sunsetH  = (int)(sunsetMin / 60);
  sunsetM  = (int)(sunsetMin - sunsetH * 60);
  Serial.printf("Sun: %02d:%02d - %02d:%02d\n", sunriseH, sunriseM, sunsetH, sunsetM);
}

void drawIconSun(int x, int y) {
  tft.fillCircle(x, y, 6, C_SUN);
  for (int a = 0; a < 360; a += 45) {
    float rad = deg2rad(a);
    tft.drawLine(x + cos(rad)*8, y + sin(rad)*8,
                 x + cos(rad)*11, y + sin(rad)*11, C_SUN);
  }
}

void drawIconCloud(int x, int y, uint16_t color) {
  tft.fillCircle(x - 4, y + 1, 5, color);
  tft.fillCircle(x + 4, y + 1, 5, color);
  tft.fillCircle(x, y - 3, 6, color);
  tft.fillRect(x - 8, y + 1, 17, 6, color);
}

void drawIconRain(int x, int y) {
  drawIconCloud(x, y - 2, C_CLOUD);
  tft.drawLine(x - 5, y + 7, x - 7, y + 11, C_RAIN);
  tft.drawLine(x,     y + 7, x - 2, y + 11, C_RAIN);
  tft.drawLine(x + 5, y + 7, x + 3, y + 11, C_RAIN);
}

void drawIconSnow(int x, int y) {
  drawIconCloud(x, y - 2, C_CLOUD);
  tft.drawPixel(x - 5, y + 9, C_SNOW);
  tft.drawPixel(x - 4, y + 10, C_SNOW);
  tft.drawPixel(x - 6, y + 10, C_SNOW);
  tft.drawPixel(x - 5, y + 11, C_SNOW);
  tft.drawPixel(x + 2, y + 9, C_SNOW);
  tft.drawPixel(x + 3, y + 10, C_SNOW);
  tft.drawPixel(x + 1, y + 10, C_SNOW);
  tft.drawPixel(x + 2, y + 11, C_SNOW);
}

void drawIconStorm(int x, int y) {
  drawIconCloud(x, y - 2, C_CLOUD);
  tft.fillTriangle(x - 1, y + 6, x + 3, y + 6, x, y + 9, C_STORM);
  tft.fillTriangle(x, y + 9, x + 3, y + 9, x - 1, y + 13, C_STORM);
}

void drawIconFog(int x, int y) {
  tft.fillRect(x - 8, y - 4, 17, 2, C_FOG);
  tft.fillRect(x - 8, y, 17, 2, C_FOG);
  tft.fillRect(x - 8, y + 4, 17, 2, C_FOG);
}

void drawIconOvercast(int x, int y) {
  drawIconCloud(x, y - 2, C_CLOUD);
  tft.fillCircle(x - 4, y + 1, 5, C_CLOUD);
  tft.fillCircle(x + 4, y + 1, 5, C_CLOUD);
  tft.fillCircle(x, y - 3, 6, C_CLOUD);
  tft.fillRect(x - 8, y + 1, 17, 8, C_CLOUD);
}

void drawWeatherIcon(int x, int y) {
  if (weatherIcon == "SUN" || weatherIcon == "CLEAR")
    drawIconSun(x, y);
  else if (weatherIcon == "RAIN" || weatherIcon == "H.RAIN")
    drawIconRain(x, y);
  else if (weatherIcon == "SNOW" || weatherIcon == "H.SNOW")
    drawIconSnow(x, y);
  else if (weatherIcon == "STORM")
    drawIconStorm(x, y);
  else if (weatherIcon == "FOG")
    drawIconFog(x, y);
  else if (weatherIcon == "OVERCAST")
    drawIconOvercast(x, y);
  else if (weatherIcon == "CLOUDY")
    drawIconCloud(x, y, C_CLOUD);
  else
    drawIconSun(x, y);
}

void drawSunIconSmall(int x, int y, int r, uint16_t color) {
  tft.fillCircle(x, y, r, color);
  for (int a = 0; a < 360; a += 45) {
    float rad = deg2rad(a);
    tft.drawLine(x + cos(rad) * (r + 1), y + sin(rad) * (r + 1),
                 x + cos(rad) * (r + 3), y + sin(rad) * (r + 3), color);
  }
}

void drawMoonIconSmall(int x, int y, int r, uint16_t color) {
  tft.fillCircle(x, y, r, color);
  tft.fillCircle(x + r / 2, y - r / 4, r - 1, C_FACE);
}

void drawHand(float angle, int len, int thick, uint16_t color) {
  float rad = deg2rad(angle - 90);
  int ex = cx + cos(rad) * len;
  int ey = cy + sin(rad) * len;
  tft.drawLine(cx, cy, ex, ey, color);
  if (thick > 1) {
    tft.drawLine(cx - 1, cy, ex - 1, ey, color);
    tft.drawLine(cx + 1, cy, ex + 1, ey, color);
  }
}

void eraseHand(float angle, int len, int thick) {
  drawHand(angle, len, thick, C_FACE);
}

void drawNumbers() {
  tft.setTextColor(C_TICK, C_FACE);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  for (int h = 1; h <= 12; h++) {
    float a = deg2rad(h * 30 - 90);
    int x = cx + cos(a) * (radius - 28);
    int y = cy + sin(a) * (radius - 28);
    char buf[4];
    sprintf(buf, "%d", h);
    tft.drawString(buf, x, y);
  }
  tft.setTextDatum(TL_DATUM);
}

String translateWeather(String cond) {
  cond.toLowerCase();
  if (cond.indexOf("thunder") >= 0) return "STORM";
  if (cond.indexOf("heavy rain") >= 0) return "H.RAIN";
  if (cond.indexOf("heavy snow") >= 0) return "H.SNOW";
  if (cond.indexOf("rain") >= 0 || cond.indexOf("drizzle") >= 0) return "RAIN";
  if (cond.indexOf("snow") >= 0 || cond.indexOf("blizzard") >= 0) return "SNOW";
  if (cond.indexOf("fog") >= 0 || cond.indexOf("mist") >= 0) return "FOG";
  if (cond.indexOf("overcast") >= 0) return "OVERCAST";
  if (cond.indexOf("cloudy") >= 0) return "CLOUDY";
  if (cond.indexOf("clear") >= 0) return "CLEAR";
  if (cond.indexOf("sunny") >= 0) return "SUN";
  if (cond.length() > 10) return cond.substring(0, 10);
  return cond;
}

void drawWeatherInfo() {
  if (!isLandscape) {
    tft.fillRect(0, 2, tft.width(), 44, C_BG);
    drawWeatherIcon(28, 18);
    tft.setTextColor(C_WEATH, C_BG);
    tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(weatherTemp, 50, 14);
    tft.setTextColor(C_COND, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(weatherCond, 50, 34);
    tft.setTextDatum(TL_DATUM);
  } else {
    int pw = tft.width() - panelX;
    tft.fillRect(panelX, 2, pw, 60, C_BG);
    drawWeatherIcon(panelX + 16, 20);
    tft.setTextColor(C_WEATH, C_BG);
    tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(weatherTemp, panelX + 38, 16);
    tft.setTextColor(C_COND, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(weatherCond, panelX + 38, 38);
    tft.setTextDatum(TL_DATUM);
  }
}

void drawClockFace() {
  tft.fillScreen(C_BG);
  if (isLandscape) {
    tft.drawLine(panelX - 5, 8, panelX - 5, tft.height() - 8, C_SIGN);
  }
  tft.drawCircle(cx, cy, radius, C_RING);
  tft.drawCircle(cx, cy, radius - 1, C_RING);
  tft.drawCircle(cx, cy, radius - 2, C_RING);
  tft.fillCircle(cx, cy, radius - 4, C_FACE);
  tft.drawCircle(cx, cy, radius - 4, C_RING);
  for (int i = 0; i < 60; i++) {
    float a = deg2rad(i * 6 - 90);
    int r1 = radius - 6;
    int r2 = (i % 5 == 0) ? radius - 16 : radius - 10;
    int x1 = cx + cos(a) * r1;
    int y1 = cy + sin(a) * r1;
    int x2 = cx + cos(a) * r2;
    int y2 = cy + sin(a) * r2;
    tft.drawLine(x1, y1, x2, y2, (i % 5 == 0) ? C_TICK : C_TICKM);
  }
  drawNumbers();
  tft.setTextColor(C_SIGN, C_BG);
  tft.setTextSize(1);
  tft.setTextDatum(BC_DATUM);
  if (isLandscape) {
    tft.drawString("by Evgeniy", panelX + (tft.width() - panelX) / 2, tft.height() - 5);
  } else {
    tft.drawString("by Evgeniy - Shitov", tft.width() / 2, tft.height() - 5);
  }
  tft.setTextDatum(TL_DATUM);
}

void drawSunInfo() {
  if (!isLandscape) {
    int y = tft.height() - 52;
    tft.fillRect(0, y, tft.width(), 18, C_BG);
    if (isDaytime)
      drawSunIconSmall(tft.width() / 2, y + 9, 4, C_SUN);
    else
      drawMoonIconSmall(tft.width() / 2, y + 9, 4, C_MOON);
    char buf[8];
    if (sunriseH >= 0) sprintf(buf, "%02d:%02d", sunriseH, sunriseM);
    else strcpy(buf, "P.NIGHT");
    tft.setTextColor(C_SR, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    drawSunIconSmall(22, y + 9, 3, C_SR);
    tft.drawString(buf, 32, y + 9);
    if (sunsetH >= 0) sprintf(buf, "%02d:%02d", sunsetH, sunsetM);
    else if (sunsetH == -2) strcpy(buf, "P.DAY");
    else strcpy(buf, "P.NIGHT");
    tft.setTextColor(C_SS, C_BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(buf, tft.width() - 32, y + 9);
    drawMoonIconSmall(tft.width() - 22, y + 9, 3, C_SS);
    tft.setTextDatum(TL_DATUM);
  } else {
    int pw = tft.width() - panelX;
    int qx = panelX + pw / 2;
    tft.fillRect(panelX, 68, pw, 56, C_BG);
    if (isDaytime)
      drawSunIconSmall(qx, 76, 4, C_SUN);
    else
      drawMoonIconSmall(qx, 76, 4, C_MOON);
    char buf[8];
    if (sunriseH >= 0) sprintf(buf, "%02d:%02d", sunriseH, sunriseM);
    else strcpy(buf, "P.NIGHT");
    tft.setTextColor(C_SR, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    drawSunIconSmall(panelX + 14, 96, 3, C_SR);
    tft.drawString(buf, panelX + 26, 96);
    if (sunsetH >= 0) sprintf(buf, "%02d:%02d", sunsetH, sunsetM);
    else if (sunsetH == -2) strcpy(buf, "P.DAY");
    else strcpy(buf, "P.NIGHT");
    tft.setTextColor(C_SS, C_BG);
    tft.setTextDatum(ML_DATUM);
    drawMoonIconSmall(panelX + 14, 112, 3, C_SS);
    tft.drawString(buf, panelX + 26, 112);
    tft.setTextDatum(TL_DATUM);
  }
}

void drawDigitalInfo() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  char buf[32];
  const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                          "Jul","Aug","Sep","Oct","Nov","Dec"};
  sprintf(buf, "%s, %s %d %d",
    days[ti->tm_wday], months[ti->tm_mon], ti->tm_mday, ti->tm_year + 1900);
  if (!isLandscape) {
    int y = tft.height() - 38;
    tft.fillRect(0, y, tft.width(), 16, C_BG);
    tft.setTextColor(C_DATE, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(BC_DATUM);
    tft.drawString(buf, tft.width() / 2, y + 14);
    tft.setTextDatum(TL_DATUM);
  } else {
    int pw = tft.width() - panelX;
    int qx = panelX + pw / 2;
    tft.fillRect(panelX, 130, pw, 30, C_BG);
    tft.setTextColor(C_DATE, C_BG);
    tft.setTextSize(1);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(buf, qx, 135);
    char tbuf[8];
    sprintf(tbuf, "%02d:%02d", ti->tm_hour, ti->tm_min);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setTextSize(2);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(tbuf, qx, 152);
    tft.setTextDatum(TL_DATUM);
  }
}

void updateHands(int h, int m, int s) {
  float ha = (h % 12) * 30.0 + m * 0.5;
  float ma = m * 6.0 + s * 0.1;
  float sa = s * 6.0;
  if (prev_sec >= 0) {
    eraseHand(prev_sa, radius - 22, 1);
    eraseHand(prev_ma, radius - 32, 2);
    eraseHand(prev_ha, radius - 50, 2);
  }
  drawNumbers();
  tft.fillCircle(cx, cy, 5, C_FACE);
  drawHand(ha, radius - 50, 2, C_HOUR);
  drawHand(ma, radius - 32, 2, C_MIN);
  drawHand(sa, radius - 22, 1, C_SEC);
  tft.fillCircle(cx, cy, 5, C_RING);
  tft.fillCircle(cx, cy, 2, C_SEC);
  prev_ha = ha;
  prev_ma = ma;
  prev_sa = sa;
  prev_sec = s;
}

void fetchWeather() {
  HTTPClient http;
  http.setUserAgent("curl/7.68.0");
  http.begin("http://wttr.in/Nizhnevartovsk?format=%t|%C");
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    body.trim();
    while (body.indexOf("\033[") >= 0) {
      int s = body.indexOf("\033[");
      int e = body.indexOf("m", s);
      if (e < 0) break;
      body.remove(s, e - s + 1);
    }
    int sep = body.indexOf('|');
    if (sep > 0) {
      String rawTemp = body.substring(0, sep);
      String rawCond = body.substring(sep + 1);
      rawTemp.trim();
      rawCond.trim();
      rawTemp.replace("\xC2\xB0", "");
      rawTemp.replace("°", "");
      rawTemp.replace("C", "");
      rawTemp.trim();
      if (rawTemp.length() > 0 && rawTemp[0] != '-' && rawTemp[0] != '+') {
        weatherTemp = "+" + rawTemp + "C";
      } else {
        weatherTemp = rawTemp + "C";
      }
      weatherCond = translateWeather(rawCond);
      weatherIcon = weatherCond;
      Serial.printf("Weather: [%s] | [%s] | [%s]\n",
        weatherTemp.c_str(), weatherCond.c_str(), weatherIcon.c_str());
    }
  } else {
    Serial.printf("Weather HTTP error: %d\n", code);
  }
  http.end();
  drawWeatherInfo();
}

void updateAutoBrightness() {
  int raw = analogRead(LDR_PIN);
  if (smoothLight == 0) {
    smoothLight = raw;
  } else {
    smoothLight = (smoothLight * 7 + raw * 3) / 10;
  }
  int bri = map(smoothLight, 0, 2500, 40, 250);
  bri = constrain(bri, 40, 250);
  if (bri != brightness) {
    brightness = bri;
    tft.setBrightness(brightness);
  }
}

void checkSunTimes() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  if (ti->tm_yday != prevSunCalcDay) {
    prevSunCalcDay = ti->tm_yday;
    calcSunTimes(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
    drawSunInfo();
  }
  int nowMin = ti->tm_hour * 60 + ti->tm_min;
  bool wasDay = isDaytime;
  if (sunriseH >= 0 && sunsetH >= 0) {
    if (sunriseH * 60 + sunriseM < sunsetH * 60 + sunsetM) {
      isDaytime = (nowMin >= sunriseH * 60 + sunriseM &&
                   nowMin <  sunsetH * 60 + sunsetM);
    } else {
      isDaytime = (nowMin >= sunriseH * 60 + sunriseM ||
                   nowMin <  sunsetH * 60 + sunsetM);
    }
  }
  if (wasDay != isDaytime) {
    drawSunInfo();
    Serial.printf("Day/night changed: isDay=%d\n", isDaytime);
  }
}

void switchRotation() {
  int rot = tft.getRotation();
  rot = (rot == 0) ? 1 : 0;
  tft.setRotation(rot);
  setupLayout();
  prev_sec = -1;
  drawClockFace();
  drawWeatherInfo();
  drawSunInfo();
  drawDigitalInfo();
  Serial.printf("Rotation: %d\n", rot);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  tft.init();
  tft.setRotation(0);
  CYD::initLED();
  tft.setBrightness(brightness);
  setupLayout();
  smoothLight = analogRead(LDR_PIN);
  tft.fillScreen(C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting WiFi...", cx, cy);
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    tft.drawString("Syncing NTP...", cx, cy + 20);
    configTime(5 * 3600, 0, "pool.ntp.org", "time.google.com");
    time_t now = 0;
    for (int i = 0; i < 30; i++) {
      now = time(nullptr);
      if (now > 1700000000) break;
      delay(500);
    }
    Serial.printf("NTP time: %ld\n", now);
    tft.drawString("Fetching weather...", cx, cy + 40);
    fetchWeather();
    lastWeatherUpdate = millis();
  }
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm* ti = localtime(&now);
    calcSunTimes(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
    prevSunCalcDay = ti->tm_yday;
    int nowMin = ti->tm_hour * 60 + ti->tm_min;
    if (sunriseH >= 0 && sunsetH >= 0) {
      isDaytime = (nowMin >= sunriseH * 60 + sunriseM &&
                   nowMin <  sunsetH * 60 + sunsetM);
    }
  }
  drawClockFace();
  drawWeatherInfo();
  drawSunInfo();
  drawDigitalInfo();
}

void loop() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  int h = ti->tm_hour;
  int m = ti->tm_min;
  int s = ti->tm_sec;
  if (s != prev_sec) {
    updateHands(h, m, s);
    drawDigitalInfo();
  }
  if (autoBrightness && millis() - lastLightCheck > 2000) {
    lastLightCheck = millis();
    updateAutoBrightness();
  }
  if (millis() - lastWeatherUpdate > 600000) {
    lastWeatherUpdate = millis();
    fetchWeather();
  }
  checkSunTimes();
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    unsigned long touchStart = millis();
    bool isLongPress = false;
    while (tft.getTouch(&tx, &ty)) {
      if (millis() - touchStart > 1000) {
        isLongPress = true;
        break;
      }
      delay(50);
    }
    if (isLongPress) {
      switchRotation();
      while (tft.getTouch(&tx, &ty)) delay(50);
    } else {
      if (autoBrightness) {
        autoBrightness = false;
        bri_level = 0;
      }
      bri_level = (bri_level + 1) % 4;
      if (bri_level == 0) {
        autoBrightness = true;
      } else if (bri_level == 1) {
        brightness = 60;
      } else if (bri_level == 2) {
        brightness = 140;
      } else if (bri_level == 3) {
        brightness = 250;
      }
      tft.setBrightness(brightness);
      delay(200);
    }
  }
  delay(50);
}
