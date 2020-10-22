//
//  Copyright © 2020 Stack Care Inc. All rights reserved.
//

#pragma once

#define COMMUNITY_ID "da7zgwvk7gx8xbj7nek9boyy6kyyyv"
#define UNIT_ID "zwpbygokn8zznkj9q69orj587eyqa"
#define HUB_ID "z85yo5dwdo7gxjy8r7pwzedomqbdz7"
#define PROJECT_ID "care-api-test"
#define IOTC_LOCALE "us-central1"
#define IOTC_REGISTRY_ID "care-mqtt"
#define DEVICE_ID "kmjzyaypen6w6oxyoornna5y8ddpw"
#define SENSOR_TYPE "motion"

#define PRIV_KEY "-----BEGIN EC PRIVATE KEY-----\n"                     \
                 "MHcCAQEEINB98ZKA+ELOGPMB4Rh07tPdtqxIsxCAIS4wkSWstmGzoAoGCCqGSM49\n"\
                 "AwEHoUQDQgAEfbpOGLuewJF1Dh2vcnSgVxpfF42nCAtuChQXdoARe+zgGLRpeAhc\n"\
                 "Irj8vQXID3U0oqMdKiwnJ9BONBPMnjRZJg==\n"\
                 "-----END EC PRIVATE KEY-----\n"

#define MAX_NUM_SENSORS 20
#define ID_MAX    40
#define DEFAULT_PRIORITY 12

typedef struct {
    char siteId[ID_MAX];
    char hubId[ID_MAX];
    int  provState;
    char projectId[ID_MAX];
    char locale[ID_MAX];
    char registryId[ID_MAX];
    uint32_t restart_time;
    bool config_on_bootup;
} HubInfo;

esp_err_t mqtt_init_iotc();
const char *mqtt_current_jwt();
void mqtt_start();
void mqtt_stop();
esp_err_t mqtt_publish(const char *topic, const char *msg);
esp_err_t mqtt_publish_data(const char *topic, const uint8_t *msg, size_t len);
void mqtt_attach_device(const char* device_id);
