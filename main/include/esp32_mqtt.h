#ifndef __ESP32_MQTT_H__
#define __ESP32_MQTT_H__

void esp32_mqtt_start(void);
int mqtt_subscribe(char * data, size_t len);
void user_task_init(void);
#endif