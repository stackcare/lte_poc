#ifndef __PPPOS_CLIENT_MAIN_H__
#define __PPPOS_CLIENT_MAIN_H__

extern EventGroupHandle_t event_group;
extern const int CONNECT_BIT;
extern const int STOP_BIT;
extern const int GOT_DATA_BIT;

void pppos_main(void);
void pppos_main_task_init();
unsigned char get_ppp_mode(void);
void get_sim_iccid(char * iccid);
void get_lte_lqi(char * rsrp, char * rsrq, char * rssi);
#endif