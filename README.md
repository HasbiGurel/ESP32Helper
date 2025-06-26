ESP32Helper Kütüphanesi
ESP32Helper, ESP32 projeleri için geliştirme sürecini hızlandırmak ve standartlaştırmak amacıyla tasarlanmış, çok fonksiyonlu bir yardımcı kütüphanedir. WiFi yönetimi, web tabanlı arayüz, uzaktan güncelleme (OTA), dinamik kontroller ve anlık log takibi gibi birçok karmaşık işlemi basit fonksiyon çağrılarıyla yönetmenizi sağlar.

Bu kütüphane sayesinde, projenizin ana mantığına odaklanırken, bağlantı ve arayüz gibi standart ihtiyaçları kütüphaneye devredebilirsiniz.

✨ Temel Özellikler
Otomatik WiFi Yönetimi:

Cihaz, başlangıçta hafızaya kaydedilmiş WiFi bilgilerine bağlanmaya çalışır.

Başarılı olamazsa veya kayıtlı ağ yoksa, kendi Access Point'ini (AP) oluşturarak bir kurulum arayüzü sunar.

Gelişmiş Web Paneli:

Cihaza, IP adresi üzerinden (http://[cihazin_ip_adresi]/panel) erişilebilen modern ve mobil uyumlu bir web paneli sunar.

Panel üzerinden cihazın durumu izlenebilir, kontroller ve ayarlar yapılabilir.

Anlık Log Takibi (WebSerial):

Standart Serial.print() gibi çalışan webPrintln() ve webPrintf() fonksiyonları ile hem seri porta hem de web arayüzündeki "Canlı Log" ekranına aynı anda mesaj gönderebilirsiniz.

Arayüzdeki metin giriş alanından cihaza komutlar gönderebilir ve bu komutları ana kodunuzda işleyebilirsiniz.

Dinamik Kontrol Arayüzü:

addButton() fonksiyonu ile arayüzün sol tarafına kolayca butonlar ve anahtarlar (switch) ekleyebilirsiniz.

Her bir butona, tıklandığında veya durumu değiştiğinde çalışacak özel bir C++ fonksiyonu (callback) atayabilirsiniz.

3 Farklı OTA Güncelleme Yöntemi:

Arduino OTA: WiFi'a bağlıyken Arduino IDE üzerinden kablosuz olarak kod yüklemenizi sağlar.

Web OTA: Paneldeki "Ayarlar" menüsünden, bilgisayarınızdan bir .bin dosyası seçerek güncelleme yapmanızı sağlar.

URL OTA (Otomatik Güncelleme Kontrolü ile):

Panel üzerinden bir firmware URL'si kaydedebilirsiniz.

Cihaz her yeniden başladığında bu URL'yi kontrol eder. Sunucudaki versiyon, cihazdaki versiyondan daha yeniyse, güncellemeyi otomatik olarak indirip kurar.

Kalıcı Hafıza (Preferences):

WiFi bilgileri ve OTA güncelleme URL'si gibi ayarları, cihazın kalıcı hafızasında (NVS) güvenli bir şekilde saklar.

📦 Bağımlılıklar
Bu kütüphanenin çalışması için PlatformIO projenizin platformio.ini dosyasında aşağıdaki kütüphanenin tanımlı olması gerekir:

links2004/WebSockets

lib_deps =
    links2004/WebSockets

🛠️ Kurulum
Kütüphaneyi kendi projenize eklemek için:

Bu kütüphanenin klasörünü (ESP32Helper) projenizin lib/ dizini altına kopyalayın.

main.cpp dosyanızı aşağıdaki kullanım örneğine göre düzenleyin.

🚀 Temel Kullanım
Aşağıda, kütüphanenin temel özelliklerini gösteren basit bir main.cpp dosyası bulunmaktadır.

#include "ESP32Helper/ESP32Helper.h"

// Projenizin mevcut versiyonunu burada tanımlayın.
// Otomatik URL OTA güncellemesi için bu numara sunucudakiyle karşılaştırılır.
#define FIRMWARE_VERSION "1.0.0"

// Prototip: gelenMesajiIsle fonksiyonunun var olduğunu derleyiciye bildirir.
void gelenMesajiIsle(String message);

// 'helper' nesnesini global olarak oluşturun.
// Parametreler: (cihaz adı, firmware versiyonu, mesaj işleme fonksiyonu)
ESP32Helper helper("cihazim-v1", FIRMWARE_VERSION, gelenMesajiIsle);

// Bu fonksiyon, web arayüzündeki metin alanından bir mesaj gönderildiğinde çalışır.
void gelenMesajiIsle(String message) {
  helper.webPrintf("[Sizden Gelen Mesaj]: %s\n", message.c_str());
  
  // Gelen mesaja göre işlem yapabilirsiniz
  if (message.equalsIgnoreCase("led ac")) {
    digitalWrite(BUILTIN_LED, HIGH);
    helper.webPrintln("Dahili LED yakildi.");
  } else if (message.equalsIgnoreCase("led kapat")) {
    digitalWrite(BUILTIN_LED, LOW);
    helper.webPrintln("Dahili LED sonduruldu.");
  }
}

// "Dahili LED" anahtarı için callback fonksiyonu
void ledToggleCallback(bool state) {
  // 'state' değişkeni, anahtarın yeni durumunu (true: açık, false: kapalı) tutar.
  helper.webPrintf("Dahili LED durumu degisti: %s\n", state ? "ACIK" : "KAPALI");
  digitalWrite(BUILTIN_LED, state ? HIGH : LOW);
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT); // LED pinini çıkış olarak ayarla

  // Kütüphaneyi başlatmadan önce arayüze butonlarınızı ekleyin
  helper.addButton("Dahili LED", BTN_SWITCH, ledToggleCallback);
  helper.addButton("Test Butonu", BTN_BUTTON, [](bool state){
    helper.webPrintln("--- TEST BUTONUNA BASILDI ---");
  });
  
  // Kütüphaneyi başlat (WiFi'a bağlanır, web sunucusunu ve diğer her şeyi ayarlar)
  helper.begin();

  helper.webPrintln("--- KURULUM TAMAMLANDI ---");
}

void loop() {
  // Kütüphanenin arka plan işlemlerini (web sunucusu, OTA vb.) yürütebilmesi için
  // loop() içinde bu fonksiyonun sürekli çağrılması gerekir.
  helper.loop();

  // Kendi diğer kodlarınızı buraya ekleyebilirsiniz.
}

🔄 Otomatik URL OTA Güncelleme Detayları
Bu özelliğin çalışabilmesi için, .bin dosyanızı barındıran sunucunun, dosya isteğine cevap verirken özel bir HTTP başlığı (Header) göndermesi zorunludur.

Başlık Adı: x-firmware-version

Değeri: Sunucudaki .bin dosyasının versiyon numarası (örn: 1.0.1).

Cihaz, başlangıçta ve ayarlar panelinden URL kaydedildiğinde bu adrese bir istek gönderir. Gelen cevaptaki x-firmware-version başlığındaki değeri, kendi #define FIRMWARE_VERSION değeri ile karşılaştırır. Eğer sunucudaki versiyon daha yeniyse, güncellemeyi otomatik olarak indirir ve kurar.

📋 API Referansı
ESP32Helper(const char* hostname, const char* firmwareVersion, std::function<void(String)> messageCallback)

Kütüphaneyi başlatmak için kullanılır. Cihaz adı, firmware versiyonu ve mesaj işleme fonksiyonu gibi temel parametreleri alır.

void begin()

setup() içinde çağrılır. Tüm sistemleri (WiFi, Web Sunucusu vb.) başlatır.

void loop()

loop() içinde çağrılır. Sunucunun ve diğer arka plan işlemlerinin çalışmasını sağlar.

void addButton(const String& name, ButtonType type, std::function<void(bool)> callback)

Web arayüzüne dinamik bir kontrol elemanı ekler.

name: Arayüzde görünecek isim.

type: BTN_BUTTON veya BTN_SWITCH.

callback: Etkileşim olduğunda çalışacak fonksiyon.

void webPrintln(const String& message)

Verilen metni hem seri porta hem de web arayüzüne yeni bir satırla yazar.

void webPrintf(const char* format, ...)

printf gibi formatlı metinleri hem seri porta hem de web arayüzüne yazar.

📄 Lisans
Bu proje MIT Lisansı altında lisanslanmıştır. Detaylar için LICENSE dosyasına bakınız.