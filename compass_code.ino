#include <LittleFS.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <QMC5883LCompass.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <math.h>
#include <vector>

#define LED_PIN 5
#define NUM_LEDS 16
#define BRIGHTNESS 35
CRGB leds[NUM_LEDS];

#define CSV_PATH "/stores.csv"
#define MAX_STORES 730

struct Store {
  String name;
  double lat;
  double lon;
  String hours;
  String type;
};

Store stores[MAX_STORES];
int storeCount = 0;

bool parseCSVLine(const String &line, String out[5]) {
  bool inQuotes = false;
  int fieldIdx = 0;
  String current = "";

  for (int i = 0; i < line.length(); i++) {
    char c = line.charAt(i);
    if (c == '"') {
      if (inQuotes && i + 1 < line.length() && line.charAt(i + 1) == '"') {
        current += '"';
        i++;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (c == ',' && !inQuotes) {
      if (fieldIdx >= 5) return false;
      out[fieldIdx++] = current;
      current = "";
    } else {
      current += c;
    }
  }
  if (fieldIdx != 4) return false;
  out[fieldIdx++] = current;

  for (int f = 0; f < 5; f++) {
    String &fld = out[f];
    fld.trim();
    if (fld.length() >= 2 && fld.charAt(0) == '"' && fld.charAt(fld.length() - 1) == '"') {
      fld = fld.substring(1, fld.length() - 1);
    }
    fld.replace("\"\"", "\"");
  }
  return true;
}

void loadCSV() {
  if (!LittleFS.begin(false)) {
    return;
  }
  File file = LittleFS.open(CSV_PATH, "r");
  if (!file) {
    return;
  }
  file.readStringUntil('\n');
  storeCount = 0;

  while (file.available() && storeCount < MAX_STORES) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    String fields[5];
    if (!parseCSVLine(line, fields)) {
      continue;
    }
    stores[storeCount].name = fields[0];
    stores[storeCount].lat = fields[1].toDouble();
    stores[storeCount].lon = fields[2].toDouble();
    stores[storeCount].hours = fields[3];
    stores[storeCount].type = fields[4];
    storeCount++;
  }
  file.close();
}

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

QMC5883LCompass compass;
const long xOffset = 374;
const long yOffset = 224;
const long zOffset = -130;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const double R_earth = 6371000.0;  // meters

double toRad(double deg) {
  return deg * M_PI / 180.0;
}
double toDeg(double rad) {
  return rad * 180.0 / M_PI;
}

double haversine(double lat1, double lon1, double lat2, double lon2) {
  double dLat = toRad(lat2 - lat1);
  double dLon = toRad(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2)
             + cos(toRad(lat1)) * cos(toRad(lat2))
                 * sin(dLon / 2) * sin(dLon / 2);
  return 2 * R_earth * atan2(sqrt(a), sqrt(1 - a));
}

double bearingTo(double lat1, double lon1, double lat2, double lon2) {
  double dLon = toRad(lon2 - lon1);
  double y = sin(dLon) * cos(toRad(lat2));
  double x = cos(toRad(lat1)) * sin(toRad(lat2))
             - sin(toRad(lat1)) * cos(toRad(lat2)) * cos(dLon);
  double brng = toDeg(atan2(y, x));
  return fmod(brng + 360.0, 360.0);
}

int dayAbbrevToIndex(const String &abbr) {
  if (abbr == "Mo") return 1;
  else if (abbr == "Tu") return 2;
  else if (abbr == "We") return 3;
  else if (abbr == "Th") return 4;
  else if (abbr == "Fr") return 5;
  else if (abbr == "Sa") return 6;
  else if (abbr == "Su") return 0;
  else if (abbr == "Nd") return 0;
  return -1;
}

void splitHoursSegments(const String &hoursStr, std::vector<String> &parts) {
  parts.clear();
  bool inQuote = false;
  String current = "";
  for (int i = 0; i < hoursStr.length(); i++) {
    char c = hoursStr.charAt(i);
    if (c == '"') {
      inQuote = !inQuote;
      current += c;
    } else if ((c == ';' || c == ',') && !inQuote) {
      parts.push_back(current);
      current = "";
    } else {
      current += c;
    }
  }
  if (current.length() > 0) parts.push_back(current);
}

bool segmentCoversNow(String part, int weekday, int hh, int mm) {
  part.trim();
  if (part.length() >= 2 && part.charAt(0) == '"' && part.charAt(part.length() - 1) == '"') {
    part = part.substring(1, part.length() - 1);
  }
  part.trim();
  if (part.indexOf("24/7") >= 0) return true;

  int idxHyphen = part.lastIndexOf('-');
  if (idxHyphen < 0) return false;

  int timeStart = idxHyphen - 5;
  int timeEnd = idxHyphen + 6;
  if (timeStart < 0 || timeEnd > part.length()) return false;

  String timeStr = part.substring(timeStart, timeEnd);
  String dayExpr = part.substring(0, timeStart);
  dayExpr.trim();
  if (dayExpr.length() < 2) return false;

  int rangeHyphen = dayExpr.indexOf('-');
  bool thisSegmentToday = false;

  if (rangeHyphen >= 0) {
    String startAbbr = dayExpr.substring(0, rangeHyphen);
    String endAbbr = dayExpr.substring(rangeHyphen + 1);
    startAbbr.trim();
    endAbbr.trim();
    String s2 = startAbbr.substring(0, 2);
    String e2 = endAbbr.substring(0, 2);
    int startIdx = dayAbbrevToIndex(s2);
    int endIdx = dayAbbrevToIndex(e2);
    if (startIdx >= 0 && endIdx >= 0) {
      if (startIdx <= endIdx) {
        if (weekday >= startIdx && weekday <= endIdx) thisSegmentToday = true;
      } else {
        if (weekday >= startIdx || weekday <= endIdx) thisSegmentToday = true;
      }
    }
  } else {
    String abbr = dayExpr.substring(0, 2);
    int dIdx = dayAbbrevToIndex(abbr);
    if (dIdx == weekday) thisSegmentToday = true;
  }
  if (!thisSegmentToday) return false;

  int openH = timeStr.substring(0, 2).toInt();
  int openM = timeStr.substring(3, 5).toInt();
  int closeH = timeStr.substring(6, 8).toInt();
  int closeM = timeStr.substring(9, 11).toInt();
  int now = hh * 60 + mm;
  int openT = openH * 60 + openM;
  int closeT = closeH * 60 + closeM;

  if (closeT >= openT) {
    if (now >= openT && now <= closeT) return true;
  } else {
    if (now >= openT || now <= closeT) return true;
  }
  return false;
}

bool isStoreOpen(const String &hours, int weekday, int hh, int mm) {
  if (hours.length() == 0) return false;
  if (hours.equalsIgnoreCase("Not Available")) return false;
  if (hours.indexOf("24/7") >= 0) return true;

  std::vector<String> parts;
  splitHoursSegments(hours, parts);
  for (auto &seg : parts) {
    if (segmentCoversNow(seg, weekday, hh, mm)) return true;
  }
  return false;
}

bool getTodayClosingTime(const String &hours, int weekday, int &outCloseH, int &outCloseM) {
  if (hours.equalsIgnoreCase("Not Available")) return false;
  std::vector<String> parts;
  splitHoursSegments(hours, parts);
  for (auto &part : parts) {
    String seg = part;
    seg.trim();
    if (seg.length() >= 2 && seg.charAt(0) == '"' && seg.charAt(seg.length() - 1) == '"') {
      seg = seg.substring(1, seg.length() - 1);
    }
    seg.trim();
    // Check if this segment applies to today
    int idxHyphen = seg.lastIndexOf('-');
    if (idxHyphen < 0) continue;
    int timeStart = idxHyphen - 5;
    int timeEnd = idxHyphen + 6;
    if (timeStart < 0 || timeEnd > seg.length()) continue;
    String timeStr = seg.substring(timeStart, timeEnd);  // "HH:MM-HH:MM"
    String dayExpr = seg.substring(0, timeStart);
    dayExpr.trim();
    int rangeHyphen = dayExpr.indexOf('-');
    bool thisToday = false;
    if (rangeHyphen >= 0) {
      String startAbbr = dayExpr.substring(0, rangeHyphen);
      String endAbbr = dayExpr.substring(rangeHyphen + 1);
      startAbbr.trim();
      endAbbr.trim();
      String s2 = startAbbr.substring(0, 2);
      String e2 = endAbbr.substring(0, 2);
      int startIdx = dayAbbrevToIndex(s2);
      int endIdx = dayAbbrevToIndex(e2);
      if (startIdx >= 0 && endIdx >= 0) {
        if (startIdx <= endIdx) {
          if (weekday >= startIdx && weekday <= endIdx) thisToday = true;
        } else {
          if (weekday >= startIdx || weekday <= endIdx) thisToday = true;
        }
      }
    } else {
      String abbr = dayExpr.substring(0, 2);
      int dIdx = dayAbbrevToIndex(abbr);
      if (dIdx == weekday) thisToday = true;
    }
    if (!thisToday) continue;
    // Parse closing time from timeStr
    outCloseH = timeStr.substring(6, 8).toInt();
    outCloseM = timeStr.substring(9, 11).toInt();
    return true;
  }
  return false;
}

int minutesUntilClose(int nowH, int nowM, int closeH, int closeM) {
  int nowT = nowH * 60 + nowM;
  int closeT = closeH * 60 + closeM;
  if (closeT >= nowT) {
    return closeT - nowT;
  } else {
    return (24 * 60 - nowT) + closeT;
  }
}

void pointNorth() {
  FastLED.clear();
  leds[0] = CRGB::Blue;
  FastLED.show();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  Wire.begin(21, 22);
  loadCSV();

  compass.init();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);
}

void loop() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  if (!gps.location.isValid() || !gps.time.isValid() || !gps.date.isValid()) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Waiting for GPS fix");

    display.setCursor(0, 12);
    display.print("Satellites: ");
    display.println(gps.satellites.isValid() ? gps.satellites.value() : 0);

    if (gps.hdop.isValid()) {
      display.setCursor(0, 24);
      display.printf("HDOP: %.1f\n", gps.hdop.hdop());
    }

    display.display();
    FastLED.clear(true);
    delay(1000);
    return;
  }

  int utcH = gps.time.hour();
  int utcM = gps.time.minute();
  int y = gps.date.year();
  int mo = gps.date.month();
  int d = gps.date.day();

  auto zellerWeekday = [&](int Y, int M, int D) -> int {
    int y2 = Y, m2 = M;
    if (m2 < 3) {
      m2 += 12;
      y2--;
    }
    int K = y2 % 100, J = y2 / 100;
    int wZ = (D + (13 * (m2 + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    return wZ;  // 0=Sat, 1=Sun, 2=Mon, …, 6=Fri
  };

  auto lastSundayOf = [&](int Y, int M) -> int {
    int mdays;
    if (M == 2) mdays = ((Y % 4 == 0 && Y % 100 != 0) || (Y % 400 == 0)) ? 29 : 28;
    else if (M == 4 || M == 6 || M == 9 || M == 11) mdays = 30;
    else mdays = 31;

    int wLast = zellerWeekday(Y, M, mdays);
    int offset = (wLast == 1) ? 0 : ((wLast + 6) % 7);
    return mdays - offset;
  };

  int lastSunMar = lastSundayOf(y, 3);
  int lastSunOct = lastSundayOf(y, 10);

  bool isDST = false;
  if (mo > 3 && mo < 10) isDST = true;
  else if (mo == 3) {
    if (d > lastSunMar) isDST = true;
    else if (d == lastSunMar && utcH >= 1) isDST = true;
  } else if (mo == 10) {
    if (d < lastSunOct) isDST = true;
    else if (d == lastSunOct && utcH < 1) isDST = true;
  }

  int localOffset = isDST ? 2 : 1;
  int hh = utcH + localOffset;
  if (hh >= 24) hh -= 24;
  int mm = utcM;

  int wZ = zellerWeekday(y, mo, d);
  int weekday = (wZ == 0) ? 6 : (wZ - 1);

  compass.read();
  long rawX = compass.getX();
  long rawY = compass.getY();
  // Apply calibrated offsets
  long xCal = rawX - xOffset;
  long yCal = rawY - yOffset;
  double head = toDeg(atan2((double)yCal, (double)xCal));
  if (head < 0) head += 360.0;

  double userLat = gps.location.lat();
  double userLon = gps.location.lng();

  double bestDist = 1e9;
  int bestIdx = -1;
  double bestBrng = 0.0;

  for (int i = 0; i < storeCount; i++) {
    if (!isStoreOpen(stores[i].hours, weekday, hh, mm)) continue;
    double dist = haversine(userLat, userLon, stores[i].lat, stores[i].lon);
    if (dist < bestDist) {
      bestDist = dist;
      bestIdx = i;
      bestBrng = bearingTo(userLat, userLon, stores[i].lat, stores[i].lon);
    }
  }

  if (bestIdx < 0) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No stores open :(");
    display.display();

    FastLED.clear(true);
    delay(1000);
    return;
  }

  double rel = bestBrng - head;
  if (rel < 0) rel += 360.0;
  int idxLed = int(rel / (360.0 / NUM_LEDS)) % NUM_LEDS;

  FastLED.clear();
  leds[idxLed] = CRGB::Green;
  FastLED.show();

  display.clearDisplay();

  // Store name 
  display.setTextSize(2);
  display.setCursor(0, 0);
  String nameToShow = stores[bestIdx].name;
  if (nameToShow.length() > 10) {
    nameToShow = nameToShow.substring(0, 10) + "...";
  }
  display.println(nameToShow);  // +-10 chars max

  // Distance
  display.setTextSize(2);
  display.setCursor(0, 32);
  if (bestDist >= 1000.0) {
    display.printf("%4.2fkm", bestDist / 1000.0);
  } else {
    display.printf("%4dm", (int)bestDist);
  }

  // Current time from gps data
  display.setTextSize(1);
  display.setCursor(90, 56);
  display.printf("%02d:%02d", hh, mm);

  // GPS HDOP and satellites
  if (gps.hdop.isValid()) {
    display.setCursor(90, 40);
    display.printf("H:%.1f", gps.hdop.hdop());
  }
  display.setCursor(90, 48);
  display.printf("S:%d", gps.satellites.isValid() ? gps.satellites.value() : 0);

  // Time until closed or NA
  int closeH, closeM;
  bool hasHours = getTodayClosingTime(stores[bestIdx].hours, weekday, closeH, closeM);
  display.setCursor(0, 56);
  if (!hasHours) {
    display.print("Closes: N/A");
  } else {
    int minsLeft = minutesUntilClose(hh, mm, closeH, closeM);
    display.printf("Closes in %dm", minsLeft);
  }

  display.display();
  delay(500);
}
