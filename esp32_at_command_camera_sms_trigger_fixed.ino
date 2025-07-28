
#include <HardwareSerial.h>
#include "esp_camera.h"
#include "base64.h"

#define MODEM_RX 16
#define MODEM_TX 17
#define WAKE_PIN 33

const char* apn = "TM";
const char* uploadUrl = "https://hcwj02vpak.execute-api.us-east-1.amazonaws.com/test/upload";

HardwareSerial sim7600(1);

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5; config.pin_d1 = 18; config.pin_d2 = 19; config.pin_d3 = 21;
  config.pin_d4 = 36; config.pin_d5 = 39; config.pin_d6 = 34; config.pin_d7 = 35;
  config.pin_xclk = 0; config.pin_pclk = 22; config.pin_vsync = 25; config.pin_href = 23;
  config.pin_sscb_sda = 26; config.pin_sscb_scl = 27;
  config.pin_pwdn = 32; config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  Serial.printf("Camera init result: 0x%x\n", err);
  if (err != ESP_OK) {
    Serial.println("Camera init failed. Halting.");
    while (true) delay(1000);
  } else {
    Serial.println("Camera init successful");
  }
}

String takePhotoAsBase64() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return "";
  String base64Str = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return base64Str;
}

void initSIM7600() {
  sim7600.println("AT+CFUN=1");
  delay(500);
  sim7600.println("AT+CNMI=2,1,0,0,0");
  delay(500);
  sim7600.println("AT+CSCLK=1");
  delay(500);
  sim7600.println("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
  delay(1000);
  sim7600.println("AT+SAPBR=3,1,\"APN\",\"" + String(apn) + "\"");
  delay(1000);
  sim7600.println("AT+SAPBR=1,1");
  delay(5000);
  sim7600.println("AT+SAPBR=2,1");
  delay(1000);
}

void sendChunkedImage(String base64Img) {
  int chunkSize = 1800;
  int total = (base64Img.length() + chunkSize - 1) / chunkSize;
  String session = String(millis());

  for (int i = 0; i < total; i++) {
    String chunk = base64Img.substring(i * chunkSize, std::min((i + 1) * chunkSize, (int)base64Img.length()));
    String payload = "{\"device_id\":\"esp32cam01\",\"session\":\"" + session +
                     "\",\"part\":" + String(i + 1) +
                     ",\"total\":" + String(total) +
                     ",\"image_base64\":\"" + chunk + "\"}";
    int len = payload.length();

    sim7600.println("AT+HTTPINIT");
    delay(500);
    sim7600.println("AT+HTTPPARA=\"CID\",1");
    delay(500);
    sim7600.println("AT+HTTPPARA=\"URL\",\"" + String(uploadUrl) + "\"");
    delay(500);
    sim7600.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
    delay(500);
    sim7600.println("AT+HTTPDATA=" + String(len) + ",10000");
    delay(2000);
    sim7600.print(payload);
    delay(500 + len / 10);
    sim7600.println("AT+HTTPACTION=1");
    delay(6000);
    sim7600.println("AT+HTTPTERM");
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Boot reason: " + String(esp_reset_reason()));
  sim7600.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  bool trigger = false;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    sim7600.println("AT+CMGF=1");
    delay(1000);
    sim7600.println("AT+CMGL=\"REC UNREAD\"");
    delay(2000);
    String sms = "";
    while (sim7600.available()) sms += (char)sim7600.read();
    if (sms.indexOf("CAMERA") >= 0 || sms.indexOf("CAMSNAP") >= 0) {
      trigger = true;
    }
    sim7600.println("AT+CMGD=1,4");
  }

  if (trigger) {
    initCamera();
    initSIM7600();
    String img = takePhotoAsBase64();
    if (img.length()) {
      sendChunkedImage(img);
      sim7600.println("SMS: OK");
    } else {
      sim7600.println("SMS: FAILED");
    }
  }

  //esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);
  //esp_deep_sleep_start();
  Serial.println("Staying awake for AT command test...");
}

void loop() {
  // Serial.println("Waiting for AT command...");
  if (sim7600.available()) {
    String cmd = sim7600.readStringUntil('\n');
    cmd.trim();
    Serial.println("Received command: " + cmd);
    if (cmd == "AT+CAMSNAP") {
      Serial.println("Triggering camera snapshot and upload...");
      initCamera();
      initSIM7600();
      String img = takePhotoAsBase64();
      if (img.length()) {
        sendChunkedImage(img);
        sim7600.println("CAMSNAP: OK");
      } else {
        sim7600.println("CAMSNAP: FAILED - No image captured");
      }
    }
  } else {
    delay(100);
  }
}
