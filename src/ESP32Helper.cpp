// lib/ESP32Helper/ESP32Helper.cpp

#include <Preferences.h>
#include <WebSocketsServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <HTTPClient.h>
#include <stdarg.h>

#include "ESP32Helper.h"

// --- Global Nesneler ---
Preferences preferences;
WebServer server(80);
WebSocketsServer webSocket(81);
WebSerial_ WebSerial;

// --- WebSerial_ Sınıfı Implementasyonu ---
WebSerial_::WebSerial_() {}
void WebSerial_::begin(WebSocketsServer* socket) { _socket = socket; }
size_t WebSerial_::write(uint8_t c) { if (_socket) { _socket->broadcastTXT(&c, 1); } return 1; }
size_t WebSerial_::write(const uint8_t *buffer, size_t size) { if (_socket) { _socket->broadcastTXT(buffer, size); } return size; }

// --- ESP32Helper Sınıfı Implementasyonu ---

ESP32Helper::ESP32Helper(const char* hostname, const char* firmwareVersion, std::function<void(String)> messageCallback)
    : _hostname(hostname), _firmwareVersion(firmwareVersion), _messageCallback(messageCallback) {}

void ESP32Helper::begin() {
    Serial.begin(115200);
    WebSerial.begin(&webSocket);
    webPrintf("Firmware Versiyonu: %s\n", _firmwareVersion.c_str());
    connectToWifi();
}

void ESP32Helper::loop() {
    server.handleClient();
    webSocket.loop();
    if (_sta_connected) {
        // --- YENİ: WiFi yeniden bağlanma mantığı ---
        if (WiFi.status() != WL_CONNECTED) {
            // Her 30 saniyede bir yeniden bağlanmayı dene (bloke etmez)
            if (millis() - _lastReconnectAttempt > 30000) {
                webPrintln("WiFi baglantisi koptu. Yeniden baglaniliyor...");
                _lastReconnectAttempt = millis();
                WiFi.reconnect();
            }
        }
        // --- BİTTİ ---

        ArduinoOTA.handle();
    }
}

void ESP32Helper::addButton(const String& name, ButtonType type, std::function<void(bool)> callback) {
    CustomButton newButton;
    newButton.name = name;
    newButton.type = type;
    newButton.callback = callback;
    newButton.id = "btn_" + String(_buttons.size());
    _buttons.push_back(newButton);
}

void ESP32Helper::webPrintln(const String& message) {
    Serial.println(message);
    WebSerial.println(message);
}

void ESP32Helper::webPrintf(const char* format, ...) {
    char loc_buf[128]; char* temp = loc_buf; va_list arg; va_list copy; va_start(arg, format);
    va_copy(copy, arg); int len = vsnprintf(temp, sizeof(loc_buf), format, copy); va_end(copy);
    if (len < 0) { va_end(arg); return; };
    if (len >= sizeof(loc_buf)) {
        temp = (char*)malloc(len + 1);
        if (temp == NULL) { va_end(arg); return; }
        len = vsnprintf(temp, len + 1, format, arg);
    }
    va_end(arg); Serial.print(temp); WebSerial.print(temp);
    if (temp != loc_buf) { free(temp); }
}

void ESP32Helper::connectToWifi() {
    preferences.begin("wifi-creds", true);
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    preferences.end();
    if (ssid.length() > 0) {
        WiFi.begin(ssid.c_str(), password.c_str());
        webPrintf("Kayitli aga baglaniliyor: %s\n", ssid.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); Serial.print("."); attempts++; }
        if (WiFi.status() == WL_CONNECTED) {
            webPrintln("\nBaglanti basarili!");
            webPrintf("IP Adresi: %s\n", WiFi.localIP().toString().c_str());
            _sta_connected = true; 
            _lastReconnectAttempt = millis(); // Başarılı bağlantıda zamanlayıcıyı sıfırla
            setupOTA(); 
            startWebServer(); 
            autoUpdateCheck();
            return;
        }
        webPrintln("\nBaglanti basarisiz.");
    }
    _sta_connected = false; startAPMode(); startWebServer();
}

void ESP32Helper::startAPMode() {
    webPrintln("AP modu baslatiliyor.");
    WiFi.softAP(_hostname.c_str(), "12345678");
    webPrintf("AP IP Adresi: %s\n", WiFi.softAPIP().toString().c_str());
}

void ESP32Helper::setupOTA() {
    ArduinoOTA.setHostname(_hostname.c_str());
    ArduinoOTA.onStart([this]() { webPrintln("Arduino OTA guncellemesi basladi..."); })
        .onEnd([this]() { webPrintln("\nEnd"); })
        .onProgress([this](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
        .onError([this](ota_error_t error) { webPrintf("Error[%u]: \n", error); });
    ArduinoOTA.begin();
}

void ESP32Helper::startWebServer() {
    webSocket.begin();
    webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        this->webSocketEvent(num, type, payload, length);
    });
    server.on("/panel", HTTP_GET, [this]() { this->handleRoot(); });
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Location", "/panel");
        server.send(302, "text/plain", "Redirecting to /panel...");
    });
    server.on("/savewifi", HTTP_POST, [this]() { this->handleSaveWifi(); });
    server.on("/urlupdate", HTTP_POST, [this]() { this->handleUrlUpdate(); });
    server.on("/restart", HTTP_GET, []() { server.send(200, "text/plain", "Cihaz yeniden baslatiliyor..."); delay(200); ESP.restart(); });
    server.on("/update", HTTP_POST, [this]() { server.sendHeader("Connection", "close"); server.send(200, "text/plain", "Guncelleme Tamamlandi! Cihaz yeniden baslatiliyor..."); delay(200); ESP.restart(); });
    server.onFileUpload([this]() { this->handleFileUpload(); });
    server.onNotFound([this]() { this->handleNotFound(); });
    server.begin();
    webPrintln("Web sunucusu baslatildi.");
}

void ESP32Helper::handleFileUpload() {
    if (server.upload().status == UPLOAD_FILE_START) {
        webPrintf("Web OTA guncellemesi basladi: %s\n", server.upload().filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); }
    } else if (server.upload().status == UPLOAD_FILE_WRITE) {
        if (Update.write(server.upload().buf, server.upload().currentSize) != server.upload().currentSize) { Update.printError(Serial); }
    } else if (server.upload().status == UPLOAD_FILE_END) {
        if (Update.end(true)) { webPrintf("Guncelleme basarili: %u bytes. Cihaz yeniden baslatilacak.\n", server.upload().totalSize);
        } else { Update.printError(Serial); webPrintln("Guncelleme sirasinda hata!"); }
    }
}

void ESP32Helper::handleRoot() {
    server.send(200, "text/html", getDynamicPanelHtml());
}

void ESP32Helper::handleSaveWifi() {
    preferences.begin("wifi-creds", false);
    preferences.putString("ssid", server.arg("ssid"));
    preferences.putString("password", server.arg("password"));
    preferences.end();
    webPrintln("WiFi bilgileri kaydedildi. ESP32 yeniden baslatiliyor...");
    delay(1000);
    ESP.restart();
}

void ESP32Helper::handleUrlUpdate() {
    if (!server.hasArg("url")) { server.send(400, "text/plain", "URL gerekli!"); return; }
    String url = server.arg("url");
    preferences.begin("ota", false);
    preferences.putString("ota_url", url);
    preferences.end();
    webPrintln("OTA URL'si kaydedildi.");
    autoUpdateCheck();
}

void ESP32Helper::autoUpdateCheck() {
    preferences.begin("ota", true);
    String url = preferences.getString("ota_url", "");
    preferences.end();

    if (url.isEmpty()) {
        webPrintln("Kayitli OTA URL'si bulunamadi, otomatik guncelleme atlandi.");
        return;
    }

    webPrintf("Guncelleme kontrol ediliyor: %s\n", url.c_str());
    webPrintf("Mevcut versiyon: %s\n", _firmwareVersion.c_str());

    HTTPClient http;
    http.begin(url);
    const char* headerKeys[] = {"x-firmware-version"};
    http.collectHeaders(headerKeys, 1);
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        if (http.hasHeader("x-firmware-version")) {
            String serverVersion = http.header("x-firmware-version");
            webPrintf("Sunucu versiyonu: %s\n", serverVersion.c_str());
            
            if (serverVersion != _firmwareVersion && serverVersion.length() > 0) {
                webPrintln("Yeni bir firmware versiyonu bulundu! Guncelleme baslatiliyor...");
                int len = http.getSize();
                if (!Update.begin(len)) { 
                    Update.printError(Serial); 
                    webPrintln("Guncelleme icin yeterli alan yok!");
                    http.end();
                    return; 
                }
                WiFiClient* stream = http.getStreamPtr();
                size_t written = Update.writeStream(*stream);
                if (written == len) {
                    if (Update.end()) {
                        webPrintln("Guncelleme tamamlandi. Cihaz yeniden baslatiliyor.");
                        ESP.restart();
                    } else {
                        Update.printError(Serial);
                        webPrintln("Guncelleme sonlandirilamadi.");
                    }
                } else {
                    webPrintf("Yazma hatasi: %u / %d bytes yazildi.\n", written, len);
                }
            } else {
                webPrintln("Firmware guncel.");
            }
        } else {
            webPrintln("Sunucu versiyon bilgisi ('x-firmware-version' header) gondermedi. Guncelleme atlandi.");
        }
    } else {
        webPrintf("Guncelleme kontrolu basarisiz, HTTP Kodu: %d\n", httpCode);
    }
    http.end();
}

void ESP32Helper::handleNotFound() {
    server.send(404, "text/plain", "404: Not found");
}

void ESP32Helper::webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            webPrintf("[%u] WebSocket baglantisi kesildi.\n", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            webPrintf("[%u] WebSocket baglantisi kuruldu: %s\n", num, ip.toString().c_str());
            webSocket.sendTXT(num, "ESP32'ye Hosgeldiniz!");
            break;
        }
        case WStype_TEXT: {
            String msg = (char*)payload;
            if (msg.startsWith("BTN_CLICK:")) {
                int firstColon = msg.indexOf(':');
                int secondColon = msg.indexOf(':', firstColon + 1);
                String id = msg.substring(firstColon + 1, secondColon);
                bool state = msg.substring(secondColon + 1).toInt() == 1;
                for (auto& btn : _buttons) {
                    if (btn.id == id) {
                        btn.currentState = state;
                        if (btn.callback) {
                            btn.callback(state);
                        }
                        break;
                    }
                }
            } 
            else if (msg.startsWith("MSG:")) {
                String userMessage = msg.substring(4);
                if (_messageCallback) {
                    _messageCallback(userMessage);
                }
            }
            break;
        }
    }
}

String ESP32Helper::getDynamicPanelHtml() {
    String html = R"rawliteral(
<!DOCTYPE html><html lang="tr"><head><title>ESP32 Yonetim Paneli</title><meta name="viewport" content="width=device-width, initial-scale=1"><meta charset="UTF-8">
<style>
    :root { --nav-height: 50px; --gap: 20px; --primary-color: #007bff; --light-gray: #f0f2f5; }
    html { height: 100%; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; background: var(--light-gray); margin: 0; color: #333; height: 100%; display: flex; flex-direction: column; }
    .card { background: #fff; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.08); padding: var(--gap); margin-bottom: var(--gap); }
    h2 { color: #333; border-bottom: 2px solid #eee; padding-bottom: 10px; margin-top: 0; font-size: 1.25em; }
    label { display: block; margin-bottom: 5px; font-weight: 500; color: #555; }
    input[type=text],input[type=password],input[type=url]{width:100%;padding:10px;margin-bottom:15px;border:1px solid #ccc;border-radius:4px;box-sizing: border-box;}
    input[type=submit], .btn { background:var(--primary-color);color:#fff;border:none;padding:12px 18px;border-radius:5px;cursor:pointer;font-size:16px;text-decoration:none;display:inline-block;width:100%;box-sizing:border-box;margin-bottom:10px;text-align:center;transition: background-color 0.2s ease;}
    .btn-secondary { background: #6c757d; }
    input[type=submit]:hover, .btn:hover { background-color: #0056b3; }
    .navbar { height:var(--nav-height);background:#fff;box-shadow:0 2px 5px rgba(0,0,0,0.1);display:flex;align-items:center;justify-content:space-between;padding:0 var(--gap);z-index:1000; flex-shrink: 0; }
    .nav-brand { font-weight: bold; font-size: 1.2em; }
    .nav-actions .btn { margin-left: 10px; width: auto; }
    .nav-status { display:flex;align-items:center;font-size:0.9em;color:#555;}
    .status-indicator { display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:8px;}
    .status-indicator.connected { background-color:#28a745; }
    .status-indicator.disconnected { background-color:#dc3545; }
    .main-container { display: flex; padding: var(--gap); gap: var(--gap); align-items: stretch; flex-grow: 1; overflow: hidden; }
    .left-column { flex: 1; max-width: 300px; }
    .right-column { flex: 2; display: flex; flex-direction: column; min-height: 0; }
    .log-card { flex-grow: 1; display: flex; flex-direction: column; min-height: 0; }
    #log { flex-grow:1; background:#222; color:#0f0; font-family:monospace; overflow-y:auto; border-radius:4px;padding:10px;box-sizing:border-box;white-space:pre-wrap;}
    .log-input-area { display: flex; gap: 10px; margin-top: 15px; flex-shrink: 0; }
    .log-input-area input { flex-grow: 1; margin-bottom: 0; }
    .log-input-area .btn { width: auto; margin-bottom: 0; }
    .modal { display:none;position:fixed;z-index:2000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.5);justify-content:center;align-items:center;}
    .modal-content { background-color:#fefefe;padding:0;border:none;width:90%;max-width:700px;border-radius:8px;position:relative;box-shadow: 0 5px 15px rgba(0,0,0,0.3); overflow:hidden;}
    .modal-header { padding: var(--gap); border-bottom: 1px solid #e5e5e5; display: flex; justify-content: space-between; align-items: center; }
    .modal-title-group { display: flex; align-items: baseline; gap: 10px; }
    .modal-header h2 { margin: 0; border: none; }
    .fw-version { font-size: 0.8em; color: #999; font-weight: 500; }
    .close-btn { color:#aaa;font-size:28px;font-weight:bold;cursor:pointer; background: none; border: none; padding: 0;}
    .modal-body { padding: var(--gap); }
    .switch-container { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
    .switch { position: relative; display: inline-block; width: 60px; height: 34px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; }
    .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; box-shadow: 0 1px 3px rgba(0,0,0,0.2); }
    input:checked + .slider { background-color: #2196F3; }
    input:checked + .slider:before { transform: translateX(26px); }
    .slider.round { border-radius: 34px; } .slider.round:before { border-radius: 50%; }
    .footer { text-align: center; padding: 15px; color: #666; font-size: 0.9em; background: #fff; border-top: 1px solid #eee; flex-shrink: 0; }
    .tab-nav { border-bottom: 1px solid #dee2e6; }
    .tab-nav .btn { background: none; border: none; color: #555; border-bottom: 3px solid transparent; margin-bottom: -1px; border-radius: 0; width: auto; font-size: 1em;}
    .tab-nav .btn.active { color: var(--primary-color); border-bottom-color: var(--primary-color); }
    .tab-content { display: none; } .tab-content.active { display: block; }
    .ota-section { padding: 15px; border: 1px solid #eee; border-radius: 5px; margin-bottom: 15px; }
    .ota-section p { margin-top: 0; font-weight: 500; }
</style>
</head><body>
<div class="navbar">
    <div class="nav-brand">)rawliteral";
    html += _hostname;
    html += R"rawliteral(</div><div class="nav-status">)rawliteral";
    if (_sta_connected) { html += R"rawliteral(<span class="status-indicator connected"></span>Bağlı: )rawliteral"; html += WiFi.localIP().toString();
    } else { html += R"rawliteral(<span class="status-indicator disconnected"></span>AP Modu: )rawliteral"; html += WiFi.softAPIP().toString(); }
    html += R"rawliteral(</div><div class="nav-actions"><button class="btn btn-secondary" onclick="restartDevice()">Yeniden Başlat</button><button class="btn" onclick="openSettings()">Ayarlar</button></div></div>
<div class="main-container">
    <div class="left-column">
        <div class="card">
            <h2>Kontroller</h2>
            <div id="buttons-container">)rawliteral";
    for (const auto& btn : _buttons) {
        if (btn.type == BTN_SWITCH) {
            html += "<div class='switch-container'><span>" + btn.name + "</span><label class='switch'><input type='checkbox' id='" + btn.id + "' onchange=\"sendButtonPress('" + btn.id + "', this.checked)\"" + (btn.currentState ? " checked" : "") + "><span class='slider round'></span></label></div>";
        } else {
            html += "<button class='btn' onclick=\"sendButtonPress('" + btn.id + "', 1)\">" + btn.name + "</button>";
        }
    }
    html += R"rawliteral(</div></div></div>
    <div class="right-column"><div class="card log-card"><h2>Canlı Log</h2><div id="log"></div>
        <div class="log-input-area">
            <input type="text" id="logInput" placeholder="Mesaj gönder..." onkeydown="if(event.keyCode==13) sendMessage()">
            <button class="btn" onclick="sendMessage()">Gönder</button>
        </div>
    </div></div>
</div>
<div id="settingsModal" class="modal" onclick="if(event.target == this) closeSettings()">
    <div class="modal-content">
        <div class="modal-header">
            <div class="modal-title-group">
                <h2>Ayarlar</h2>
                <span class="fw-version">v)rawliteral";
    html += _firmwareVersion;
    html += R"rawliteral(</span>
            </div>
            <button class="close-btn" onclick="closeSettings()">&times;</button>
        </div>
        <div class="tab-nav">
            <button class="btn active" onclick="openTab(event, 'wifiSettings')">WiFi Ayarları</button>
            <button class="btn" onclick="openTab(event, 'otaSettings')">Firmware Güncelleme</button>
        </div>
        <div class="modal-body">
            <div id="wifiSettings" class="tab-content active">
                <form action="/savewifi" method="POST">
                    <label for="ssid">WiFi Adı (SSID)</label>
                    <input type="text" id="ssid" name="ssid" required>
                    <label for="password">WiFi Şifresi</label>
                    <input type="password" id="password" name="password">
                    <input type="submit" value="Kaydet ve Yeniden Başlat">
                </form>
            </div>
            <div id="otaSettings" class="tab-content">
)rawliteral";
    preferences.begin("ota", true);
    String savedUrl = preferences.getString("ota_url", "");
    preferences.end();
    if (_sta_connected) { html += "<div class='ota-section'><p>Arduino OTA</p><span>Aktif. Port: '" + _hostname + "'</span></div>"; }
    else { html += "<div class='ota-section'><p>Arduino OTA</p><span>Pasif. Cihaz bir WiFi agina bagli degil.</span></div>"; }
    html += R"rawliteral(
                <div class="ota-section">
                    <p>Web OTA (Dosya Yükle)</p>
                    <form method="POST" action="/update" enctype="multipart/form-data">
                        <input type="file" name="update" accept=".bin" style="margin-bottom:10px;">
                        <input type="submit" value="Yükle ve Güncelle">
                    </form>
                </div>
                <div class="ota-section">
                    <p>URL OTA</p>
                    <form method="POST" action="/urlupdate">
                        <label for="ota_url">Firmware URL (Otomatik Güncelleme için)</label>
                        <input type="url" id="ota_url" name="url" placeholder="http://example.com/firmware.bin" value=")rawliteral";
    html += savedUrl;
    html += R"rawliteral(" required>
                        <input type="submit" value="Kaydet ve Güncellemeyi Kontrol Et">
                    </form>
                </div>
            </div>
        </div>
    </div>
</div>
<footer class="footer">
    <p>&copy; 2025 Telif Hakkı 1Seyler.com</p>
</footer>
<script>
    function openSettings() { document.getElementById('settingsModal').style.display = 'flex'; }
    function closeSettings() { document.getElementById('settingsModal').style.display = 'none'; }
    function restartDevice() { if(confirm('Cihaz yeniden başlatılsın mı?')) { fetch('/restart').then(r => alert('Cihaz yeniden başlatılıyor...')); } }
    function sendButtonPress(id, state) { websocket.send(`BTN_CLICK:${id}:${state ? 1 : 0}`); }
    function sendMessage() {
        const input = document.getElementById('logInput');
        if (input.value.trim()) { websocket.send(`MSG:${input.value.trim()}`); input.value = ''; }
    }
    function openTab(evt, tabName) {
        let i, tabcontent, tablinks;
        tabcontent = document.getElementsByClassName("tab-content");
        for (i = 0; i < tabcontent.length; i++) { tabcontent[i].classList.remove("active"); }
        tablinks = document.querySelectorAll(".tab-nav .btn");
        for (i = 0; i < tablinks.length; i++) { tablinks[i].classList.remove("active"); }
        document.getElementById(tabName).classList.add("active");
        evt.currentTarget.classList.add("active");
    }

    var gateway = `ws://${window.location.hostname}:81/`; var websocket;
    window.addEventListener('load', ()=>{initWebSocket();});
    function initWebSocket() {
        websocket = new WebSocket(gateway);
        websocket.onclose = (e)=>{setTimeout(initWebSocket, 2000);};
        websocket.onmessage = (e)=>{ 
            var log = document.getElementById('log'); 
            var isScrolledToBottom = log.scrollHeight - log.clientHeight <= log.scrollTop + 1;
            log.innerHTML += e.data; 
            if(isScrolledToBottom) { log.scrollTop = log.scrollHeight; }
        };
    }
</script>
</body></html>)rawliteral";
    return html;
}
