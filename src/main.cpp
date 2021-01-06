#include "Arduino.h"
#include "esp_camera.h"
#include <WiFi.h>

#include "camera_pins.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <MQTT.h>

#include "json_file.h"

//create a new file 'wifi_secret.h' with the two following constants
//the reason is that 'wifi_sercert.h' is ignored by git
//const char* ssid = "SSID";
//const char* password =  "PASSWORD";
#include "wifi_secret.h"

DynamicJsonDocument config(5*1024);//5 KB
MQTTClient mqtt(30*1024);// 30KB for jpg images
WiFiClient wifi;//needed to stay on global scope

void timelog(String Text){
  Serial.println(String(millis())+" : "+Text);//micros()
}



void mqtt_start(DynamicJsonDocument &config){
  mqtt.begin(config["mqtt"]["host"],config["mqtt"]["port"], wifi);
  if(mqtt.connect(config["mqtt"]["client_id"])){
    Serial.println("mqtt>connected");
  }
}

void mqtt_publish_config(){
  String str_config;
  serializeJson(config,str_config);
  String str_topic = config["camera"]["base_topic"];
  mqtt.publish(str_topic+"/config",str_config);
  mqtt.loop();
}


void connect() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  Serial.print("\nmqtt connecting...");
  while (!mqtt.connect(config["mqtt"]["client_id"])) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nconnected!");
}

void mqtt_loop(){
  mqtt.loop();
  if (!mqtt.connected()) {
    connect();
  }
}

void camera_start(DynamicJsonDocument &config){
  camera_config_t cam_config;
  cam_config.ledc_channel = LEDC_CHANNEL_0;
  cam_config.ledc_timer = LEDC_TIMER_0;
  cam_config.pin_d0 = Y2_GPIO_NUM;
  cam_config.pin_d1 = Y3_GPIO_NUM;
  cam_config.pin_d2 = Y4_GPIO_NUM;
  cam_config.pin_d3 = Y5_GPIO_NUM;
  cam_config.pin_d4 = Y6_GPIO_NUM;
  cam_config.pin_d5 = Y7_GPIO_NUM;
  cam_config.pin_d6 = Y8_GPIO_NUM;
  cam_config.pin_d7 = Y9_GPIO_NUM;
  cam_config.pin_xclk = XCLK_GPIO_NUM;
  cam_config.pin_pclk = PCLK_GPIO_NUM;
  cam_config.pin_vsync = VSYNC_GPIO_NUM;
  cam_config.pin_href = HREF_GPIO_NUM;
  cam_config.pin_sscb_sda = SIOD_GPIO_NUM;
  cam_config.pin_sscb_scl = SIOC_GPIO_NUM;
  cam_config.pin_pwdn = PWDN_GPIO_NUM;
  cam_config.pin_reset = RESET_GPIO_NUM;
  cam_config.xclk_freq_hz = 20000000;
  cam_config.pixel_format = PIXFORMAT_JPEG;
  cam_config.frame_size = FRAMESIZE_UXGA;
  cam_config.jpeg_quality = 10;
  cam_config.fb_count = 2;
 
  // camera init
  esp_err_t err = esp_camera_init(&cam_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * sensor = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  sensor->set_vflip(sensor, 1);//flip it back
  sensor->set_brightness(sensor, 1);//up the blightness just a bit
  sensor->set_saturation(sensor, -2);//lower the saturation

  //drop down frame size for higher initial frame rate
  sensor->set_framesize(sensor, FRAMESIZE_QVGA);

}

void camera_publish(){
  camera_fb_t * frame = NULL;

  frame = esp_camera_fb_get();
  if (!frame) {
      Serial.println("Camera capture failed");
  }

  size_t frame_len = frame->width * frame->height * 3;
  uint16_t width = frame->width;
  uint16_t height = frame->height;
  uint8_t quality = 90;
  uint8_t ** jpg_out;
  size_t * jpg_out_len;

  bool res = fmt2jpg(frame->buf, frame_len, width, height, frame->format, quality, jpg_out, jpg_out_len);
  if (!res) {
    Serial.println("camera> CAPTURE FAIL");
    return;
  }

  Serial.printf("camera> CAPTURE OK %dx%d %db\n", frame->width, frame->height, frame_len);
  String str_topic = config["camera"]["base_topic"];
  Serial.printf("mqtt> publishing on %s\n",str_topic.c_str());
  str_topic += "/jpg";//cannot be added in the function call as String overload is missing
  mqtt.publish( str_topic.c_str(),reinterpret_cast<const char *>((*jpg_out)),(*jpg_out_len));
}

void setup() {
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  timelog("Boot ready");
  
  load_config(config,true);
  timelog("config loaded");

  camera_start(config);
  timelog("camera started");

  timelog("wifi setup");
  mqtt_start(config);

  if(true){
    mqtt_publish_config();
  }
  timelog("setup() done");

  mqtt_loop();
  camera_publish();
  timelog("camera publish done");

}

void loop() {

}
