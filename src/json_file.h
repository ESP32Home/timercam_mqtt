#include <ArduinoJson.h>

bool load_config(DynamicJsonDocument &config,bool verbose=false);
bool save_config(DynamicJsonDocument &config);

bool load_json(DynamicJsonDocument &config,const char* FileName);
bool save_json(DynamicJsonDocument &config,const char* FileName);

