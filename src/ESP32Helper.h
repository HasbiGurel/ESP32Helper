// lib/ESP32Helper/ESP32Helper.h

#ifndef ESP32HELPER_H
#define ESP32HELPER_H

#include <Arduino.h>
#include <WebSocketsServer.h> 
#include <WebServer.h>
#include <vector>
#include <functional>

// Buton türlerini tanımlayan enum yapısı
enum ButtonType {
    BTN_BUTTON, // Bas-bırak tarzı normal buton
    BTN_SWITCH  // Açık/Kapalı durumuna sahip anahtar
};

// Her bir butonun özelliklerini saklayacak yapı
struct CustomButton {
    String name;
    String id;
    ButtonType type;
    bool currentState = false;
    std::function<void(bool)> callback;
};

class WebSerial_ : public Print {
public:
    WebSerial_();
    void begin(WebSocketsServer* socket);
    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buffer, size_t size);
private:
    WebSocketsServer* _socket = nullptr;
};
extern WebSerial_ WebSerial;

class ESP32Helper {
public:
    ESP32Helper(const char* hostname = "esp32-helper", const char* firmwareVersion = "0.0.0", std::function<void(String)> messageCallback = nullptr);
    void begin();
    void loop();

    void webPrintln(const String& message);
    void webPrintf(const char* format, ...);

    void addButton(const String& name, ButtonType type, std::function<void(bool)> callback);

private:
    void connectToWifi();
    void startAPMode();
    void startWebServer();
    void handleRoot();
    void handleSaveWifi();
    void handleNotFound();
    void setupOTA();
    void handleUrlUpdate();
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
    String getDynamicPanelHtml();
    void handleFileUpload();
    void autoUpdateCheck();

    String _hostname;
    String _firmwareVersion;
    bool _sta_connected = false;

    // YENİ: Yeniden bağlanma denemeleri arasındaki zamanı takip etmek için değişken
    unsigned long _lastReconnectAttempt = 0;

    std::vector<CustomButton> _buttons;
    std::function<void(String)> _messageCallback;
};

#endif
