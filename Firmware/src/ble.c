#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "drivers.h"
#include "vendor/common/user_config.h"
#include "app_config.h"
#include "drivers/8258/gpio_8258.h"
#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"
#include "ota.h"

#include "ble.h"
#include "cmd_parser.h"
#include "flash.h"

RAM	uint8_t	ble_connected = 0;

extern uint8_t my_tempVal[2];
extern uint8_t my_batVal[1];

RAM uint8_t 		 	blt_rxfifo_b[64 * 8] = {0};
RAM	my_fifo_t	blt_rxfifo = { 64, 8, 0, 0, blt_rxfifo_b,};

RAM uint8_t 			blt_txfifo_b[40 * 16] = {0};
RAM	my_fifo_t	blt_txfifo = { 40, 16, 0, 0, blt_txfifo_b,};

RAM uint8_t	ble_name[] = {11, 0x09, 'E', 'S', 'L', '_', '0', '0', '0', '0', '0', '0'};

RAM uint8_t	advertising_data[] = {
 /*Description*/16, 0x16, 0x1a, 0x18, 
 /*MAC*/0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
 /*Temp*/0xaa, 0xaa, 
 /*Humi*/0xbb, 
 /*BatL*/0xcc, 
 /*BatM*/0xdd, 0xdd, 
 /*Counter*/0x00
};

uint8_t mac_public[6];
	
uint8_t ota_is_working = 0;

void app_enter_ota_mode(void)
{
	custom_bls_ota_clearNewFwDataArea(); //must
	bls_l2cap_requestConnParamUpdate(15, 15, 0, 1000);  //1S
	custom_bls_ota_setTimeout(5 * 1000000);
	ota_is_working = 1;
}

void app_switch_to_indirect_adv(uint8_t e, uint8_t *p, int n)
{
	bls_ll_setAdvParam( ADVERTISING_INTERVAL, ADVERTISING_INTERVAL+50, ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC, 0,  NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
	bls_ll_setAdvEnable(1);
}

void ble_disconnect_callback(uint8_t e,uint8_t *p, int n)
{
	ble_connected = 0;
	printf("BLE disconnected\r\n");
}

_attribute_ram_code_ void user_set_rf_power (uint8_t e, uint8_t *p, int n)
{
	rf_set_power_level_index (RF_POWER_P3p01dBm);
}

void ble_connect_callback(uint8_t e, uint8_t *p, int n)
{
	ble_connected = 1;
	bls_l2cap_requestConnParamUpdate(15, 15, 0, 1000);  //1S
	printf("BLE connected\r\n");
}

extern u32 blt_ota_start_tick;
int otaWritePre(void * p)
{
	blt_ota_start_tick = clock_time()|1;
	custom_otaWrite(p);
	return 0;
}

int RxTxWrite(void * p)
{
	cmd_parser(p);
	return 0;
}

_attribute_ram_code_ void blt_pm_proc(void)
{
	if(ota_is_working){
		custom_bls_ota_procTimeout();
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
		bls_pm_setManualLatency(0);
	}else{
		bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	}
}

void init_ble(){
////////////////// BLE stack initialization ////////////////////////////////////
	uint8_t  mac_random_static[6];
	blc_initMacAddress(CFG_ADR_MAC, mac_public, mac_random_static);
	
	//Set the BLE Name to the last three MACs the first ones are always the same
    const char* hex_ascii = {"0123456789ABCDEF"};	
	ble_name[6] = hex_ascii[mac_public[2]>>4];
	ble_name[7] = hex_ascii[mac_public[2] &0x0f];	
	ble_name[8] = hex_ascii[mac_public[1]>>4];
	ble_name[9] = hex_ascii[mac_public[1] &0x0f];		
	ble_name[10] = hex_ascii[mac_public[0]>>4];
	ble_name[11] = hex_ascii[mac_public[0] &0x0f];
	
	advertising_data[4] = mac_public[5];
	advertising_data[5] = mac_public[4];
	advertising_data[6] = mac_public[3];
	advertising_data[7] = mac_public[2];
	advertising_data[8] = mac_public[1];
	advertising_data[9] = mac_public[0];
	
////// Controller Initialization  //////////
	blc_ll_initBasicMCU();                      //must
	blc_ll_initStandby_module(mac_public);		//must
	blc_ll_initAdvertising_module(mac_public); 	//adv module: 		 must for BLE slave,
	blc_ll_initConnection_module();				//connection module  must for BLE slave/master
	blc_ll_initSlaveRole_module();				//slave module: 	 must for BLE slave,
	blc_ll_initPowerManagement_module();        //pm module:      	 optional
	
////// Host Initialization  //////////
	blc_gap_peripheral_init();
	extern void my_att_init();
	my_att_init (); //gatt initialization
	blc_l2cap_register_handler (blc_l2cap_packet_receive);
	blc_smp_setSecurityLevel(No_Security);
	
///////////////////// USER application initialization ///////////////////
	bls_ll_setScanRspData( (uint8_t *)ble_name, sizeof(ble_name));
	bls_ll_setAdvParam(  ADVERTISING_INTERVAL, ADVERTISING_INTERVAL+50, ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC, 0,  NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
	bls_ll_setAdvEnable(1);
	user_set_rf_power(0, 0, 0);
	bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);
	bls_app_registerEventCallback (BLT_EV_FLAG_CONNECT, &ble_connect_callback);
	bls_app_registerEventCallback (BLT_EV_FLAG_TERMINATE, &ble_disconnect_callback);
	
///////////////////// Power Management initialization///////////////////
	blc_ll_initPowerManagement_module();
	bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	blc_pm_setDeepsleepRetentionThreshold(95, 95);
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(240);
	blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);

	custom_bls_ota_registerStartCmdCb(app_enter_ota_mode);	
}

bool ble_get_connected(){
	return ble_connected;
}

void set_adv_data(int16_t temp, uint16_t humi, uint8_t battery_level, uint16_t battery_mv){
		advertising_data[10] = temp>>8;
		advertising_data[11] = temp&0xff;
		
		advertising_data[12] = humi&0xff;
		
		advertising_data[13] = battery_level;
		
		advertising_data[14] = battery_mv>>8;
		advertising_data[15] = battery_mv&0xff;
		
		advertising_data[16]++;
		
		bls_ll_setAdvData( (uint8_t *)advertising_data, sizeof(advertising_data));	
}

void ble_send_temp(uint16_t temp){
	my_tempVal[0] = temp & 0xFF;
	my_tempVal[1] = temp >> 8;
	bls_att_pushNotifyData(TEMP_LEVEL_INPUT_DP_H, my_tempVal, 2);
}

void ble_send_battery(uint8_t value){
	my_batVal[0] = value;
	bls_att_pushNotifyData(BATT_LEVEL_INPUT_DP_H, (uint8_t *)my_batVal, 1);
}
