#include <ESP32Helper.h>

// Projenizin mevcut versiyonu
#define FIRMWARE_VERSION "1.0.0"

// Bu fonksiyon, web arayüzündeki metin alanından bir mesaj gönderildiğinde çalışacak.
void gelenMesajiIsle(String message);

// helper nesnesini, hostname, versiyon ve callback ile oluşturun
ESP32Helper helper("cihazim", FIRMWARE_VERSION, gelenMesajiIsle);

void gelenMesajiIsle(String message) {
  helper.webPrintf("[Sizden Gelen Mesaj]: %s\n", message.c_str());
  
  if (message.equalsIgnoreCase("led ac")) {
    digitalWrite(BUILTIN_LED, HIGH);
    helper.webPrintln("Dahili LED yakildi.");
  } else if (message.equalsIgnoreCase("led kapat")) {
    digitalWrite(BUILTIN_LED, LOW);
    helper.webPrintln("Dahili LED sonduruldu.");
  }
}

// Butonlar için callback fonksiyonu
void ledToggleCallback(bool state) {
  helper.webPrintf("Dahili LED durumu degisti: %s\n", state ? "ACIK" : "KAPALI");
  digitalWrite(BUILTIN_LED, state ? HIGH : LOW);
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);

  // Kütüphaneyi başlatmadan önce butonları ekleyin
  helper.addButton("Dahili LED", BTN_SWITCH, ledToggleCallback);
  
  // Kütüphaneyi başlat
  helper.begin();

  helper.webPrintln("--- KURULUM TAMAMLANDI ---");
}

void loop() {
  helper.loop();
}