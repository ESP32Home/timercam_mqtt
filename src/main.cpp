#include "Arduino.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include <WiFi.h>

#include "camera_pins.h"
#include "battery.h"
#include "bmm8563.h"
#include "led.h"

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
MQTTClient mqtt(35*1024);// 40KB for jpg images
WiFiClient wifi;//needed to stay on global scope

void task_delay_ms(int ms){
  vTaskDelay(pdMS_TO_TICKS(ms));
}

void timelog(String Text){
  Serial.println(String(millis())+" : "+Text);//micros()
}

void blink(int duty, int duration_ms){
  led_brightness(duty);
  task_delay_ms(duration_ms);
  led_brightness(0);
}

void mqtt_publish_config(){
  String str_config;
  serializeJson(config,str_config);
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/config";
  mqtt.publish(str_topic,str_config,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s)=>(%s)\r\n",str_topic.c_str(),str_config.c_str());
}

void mqtt_publish_status(){
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/status";
  float time_f = millis();
  time_f /=1000;
  String str_wakeup = String(time_f);
  float battery_f = bat_get_voltage();
  battery_f /=1000;
  String str_battery = String(battery_f);
  String json_payload = "{\"battery\":"+str_battery+",\"wakeup\":"+str_wakeup+"}";
  mqtt.publish(str_topic,json_payload,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s)=>(%s)\r\n",str_topic.c_str(),json_payload.c_str());
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
  cam_config.jpeg_quality = 10;//0-63 lower means higher quality
  cam_config.fb_count = config["camera"]["buffer_count"];
 
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

  //(OV3660)	QXGA(2048x1536): 15fps / FHD(1080p): 20fps / HD(720p): 45fps / XGA(1024x768) : 45fps / VGA(640x480) : 60fps / QVGA(320x240) : 120fps
  String frame_size = config["camera"]["frame_size"];
  framesize_t fm_size = FRAMESIZE_QVGA;
  if(frame_size.compareTo("QVGA") == 0){
    fm_size = FRAMESIZE_QVGA;
  }else if(frame_size.compareTo("VGA") == 0){
    fm_size = FRAMESIZE_VGA;
  }else if(frame_size.compareTo("XGA") == 0){
    fm_size = FRAMESIZE_XGA;
  }else if(frame_size.compareTo("HD") == 0){
    fm_size = FRAMESIZE_HD;
  }else if(frame_size.compareTo("FHD") == 0){
    fm_size = FRAMESIZE_FHD;
  }else if(frame_size.compareTo("QXGA") == 0){
    fm_size = FRAMESIZE_QXGA;
  }
  sensor->set_framesize(sensor, fm_size);
  //drop down frame size for higher initial frame rate

}

void mqtt_publish_camera(){
  camera_fb_t * frame = NULL;

  timelog("esp_camera_fb_get()");
  frame = esp_camera_fb_get();
  if (!frame) {
      Serial.println("Camera capture failed");
  }

  Serial.printf("camera> CAPTURE OK %dx%d %db\n", frame->width, frame->height, frame->len);
  String str_topic = config["camera"]["base_topic"];
  str_topic += "/jpg";//cannot be added in the function call as String overload is missing
  const char* data = (const char*)frame->buf;
  mqtt.publish( str_topic.c_str(),data,frame->len,true,2);//LWMQTT_QOS2 = 2
  Serial.printf("published (%s) => len(%u)\r\n",str_topic.c_str(),frame->len);
}

bool connect(){
  timelog("wifi check");
  int max_timeout = 10;
  while ((WiFi.status() != WL_CONNECTED)&&(max_timeout>0)) {
    Serial.print(".");
    task_delay_ms(500);
    max_timeout--;
  }
  Serial.print("\n");
  if(max_timeout == 0){
    timelog("wifi timeout!");
    blink(200,10);task_delay_ms(200);
    blink(200,10);task_delay_ms(200);
    return false;
  }
  max_timeout = 10;
  timelog("wifi connected!");
  mqtt.begin(config["mqtt"]["host"],config["mqtt"]["port"], wifi);
  Serial.print("connecting mqtt");
  while ((!mqtt.connect(config["mqtt"]["client_id"]))&&(max_timeout>0)) {
    Serial.print(".");
    blink(300,50);
    task_delay_ms(450);
    max_timeout--;
  }
  if(max_timeout == 0){
    timelog("MQTT timeout!\n");
    blink(500,10);task_delay_ms(500);
    blink(500,10);task_delay_ms(500);
    return false;
  }
  Serial.print("\n");
  timelog("mqtt connected!\n");
  return true;
}

void setup() {

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  timelog("Boot ready");
  WiFi.begin(ssid, password);
  led_init(CAMERA_LED_GPIO);
  led_brightness(0);
  bat_init();
  load_config(config,true);
  timelog("config loaded");
  camera_start(config);
  timelog("camera started");

  const int16_t battery_sleep = config["camera"]["battery_sleep"];
  const int16_t usb_sleep = config["camera"]["usb_sleep"];
  bmm8563_init();
  bmm8563_setTimerIRQ(battery_sleep);//in sec



  if(connect()){
    mqtt_publish_camera();    timelog("=>camera");
    mqtt.loop();
    task_delay_ms(100);//allow mqtt and serial transmission
    mqtt_publish_status();    timelog("=>status");
    mqtt.loop();
    task_delay_ms(100);//allow mqtt and serial transmission
  }

  timelog("setup done - disabling battery");
  task_delay_ms(100);//allow mqtt and serial transmission
  Serial.flush();
  bat_disable_output();
  task_delay_ms(100);//dies here

  Serial.println("Still alive - usb power");
  Serial.flush();

  blink(512,1000);task_delay_ms(500);blink(512,1000);

  Serial.println("ESP going to deep sleep");
  esp_deep_sleep(usb_sleep*1000000);
  esp_deep_sleep_start();

}

void loop() {

}
