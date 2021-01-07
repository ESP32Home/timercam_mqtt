#include "Arduino.h"
#include "esp_camera.h"
#include <WiFi.h>

#include "camera_pins.h"
#include "battery.h"
#include "bmm8563.h"

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



void mqtt_publish_config(){
  String str_config;
  serializeJson(config,str_config);
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/config";
  mqtt.publish(str_topic,str_config,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s)=>(%s)\r\n",str_topic.c_str(),str_config.c_str());
}


void mqtt_publish_time(){
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/wakeup";
  float time_f = millis();
  time_f /=1000;
  String str_wakeup = String(time_f);
  mqtt.publish(str_topic,str_wakeup,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s)=>(%s)\r\n",str_topic.c_str(),str_wakeup.c_str());
}

void mqtt_publish_battery(){
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/battery";
  float battery_f = bat_get_voltage();
  battery_f /=1000;
  String str_battery = String(battery_f);
  mqtt.publish(str_topic,str_battery,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s)=>(%s)\r\n",str_topic.c_str(),str_battery.c_str());
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

void mqtt_publish_camera(){
  camera_fb_t * frame = NULL;

  timelog("esp_camera_fb_get()");
  frame = esp_camera_fb_get();
  if (!frame) {
      Serial.println("Camera capture failed");
  }

  size_t frame_len = frame->width * frame->height * 3;
  uint16_t width = frame->width;
  uint16_t height = frame->height;
  uint8_t quality = 50;
  uint8_t * jpg_out;
  size_t jpg_out_len;

  timelog("fmt2jpg()");
  bool res = fmt2jpg(frame->buf, frame_len, width, height, frame->format, quality, &jpg_out, &jpg_out_len);
  if (!res) {
    Serial.println("camera> CAPTURE FAIL");
    return;
  }

  Serial.printf("camera> CAPTURE OK %dx%d %db\n", frame->width, frame->height, frame_len);
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/jpg_len";//cannot be added in the function call as String overload is missing
  mqtt.publish( str_topic,String(jpg_out_len),true,2);//LWMQTT_QOS2 = 2
  //mqtt.publish( str_topic.c_str(),reinterpret_cast<const char *>(jpg_out),jpg_out_len,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s) => len(%u)\r\n",str_topic.c_str(),jpg_out_len);
}

void setup() {
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  timelog("Boot ready");
  WiFi.begin(ssid, password);

  bat_init();
  bmm8563_init();
  bmm8563_setTimerIRQ(30);//in sec

  load_config(config,true);
  timelog("config loaded");

  camera_start(config);
  timelog("camera started");

  timelog("wifi setup");

  Serial.print("checking wifi");
  int max_timeout = 10;
  while ((WiFi.status() != WL_CONNECTED)&&(max_timeout>0)) {
    Serial.print(".");
    delay(500);
    max_timeout--;
  }
  Serial.print("\n");
  if(max_timeout == 0){
    timelog("wifi timeout!");
    return;
  }
  max_timeout = 10;
  timelog("wifi connected!");
  mqtt.begin(config["mqtt"]["host"],config["mqtt"]["port"], wifi);
  Serial.print("connecting mqtt");
  while ((!mqtt.connect(config["mqtt"]["client_id"]))&&(max_timeout>0)) {
    Serial.print(".");
    delay(500);
    max_timeout--;
  }
  if(max_timeout == 0){
    timelog("MQTT timeout!\n");
    return;
  }
  Serial.print("\n");
  timelog("mqtt connected!\n");

  if(true){
    mqtt_publish_config();
    timelog("=>config");
    mqtt_publish_battery();
    timelog("=>battery");
  }

  mqtt_publish_camera();
  timelog("=>camera");
  mqtt_publish_time();
  timelog("=>time");
  mqtt.loop();
  timelog("=>loop");

  timelog("setup done - disabling battery");
  Serial.flush();
  bat_disable_output();
  delay(100);//dies here
}

void loop() {
  Serial.println("Cycling with usb power");
  delay(30000);
}
