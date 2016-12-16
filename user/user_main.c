/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include "ets_sys.h"
#include "driver/uart.h"
#include "driver/dht22.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "httpclient.h"

MQTT_Client mqttClient;

#define DELAY 30000 /* milliseconds */

LOCAL os_timer_t timer;
LOCAL int relay_state = 0;


#define RELAY_GPIO 0
#define RELAY_GPIO_MUX PERIPHS_IO_MUX_GPIO0_U
#define RELAY_GPIO_FUNC FUNC_GPIO0

// *** CHANGE CONFIG HERE ***
LOCAL char YOUR_THINGSPEAK_API_KEY[]=  "30P3M2A7K7WJKQFK";
LOCAL char YOUR_THINGSPEAK_CHANNEL[]= "63126";
LOCAL bool bRelayInputInverted       = false;


LOCAL int  timer_count = 0;

//
// This function checks the response for last field1 value of Thingspeak
// channel.  If there is a change in the value, send appropriate MQTT
// message to /esp8266/relay topic to turn on/off relay.
//
void http_get_relay_state_callback(char * response, int http_status, char * full_response)
{
	char str[64];
	INFO("In http_get_relay_state_callback... http_status=%d\n", http_status);
//  os_printf("strlen(response)=%d\n", strlen(response));
//	os_printf("strlen(full_response)=%d\n", strlen(full_response));
	os_printf("response===%s===\n", response);

	if (ets_strstr(response,"-1.0"))
	{
		if ((relay_state == 0) || (relay_state == 1))
		{
			relay_state = -1;
			INFO("Sending off to /esp8266/relay...\n");
		    os_sprintf(str,"off");
		    MQTT_Publish(&mqttClient, "/esp8266/relay", str, os_strlen(str), 0, 0);
		}
	}
	else if (ets_strstr(response,"1.0"))
	{
		if ((relay_state == 0) || (relay_state == -1))
		{
			relay_state = 1;
			INFO("Sending on to /esp8266/relay...\n");
			os_sprintf(str,"on");
			MQTT_Publish(&mqttClient, "/esp8266/relay", str, os_strlen(str), 0, 0);
		}
	}
}

void http_post_callback(char * response, int http_status, char * full_response)
{
	INFO("In http_post_callback... http_status=%d\n", http_status);
//	os_printf("strlen(response)=%d\n", strlen(response));
//	os_printf("strlen(full_response)=%d\n", strlen(full_response));
	os_printf("response===%s===\n", response);
}

//
// This function gets called every DELAY milliseconds.  On the 1st, 3rd, 5th, 7th and 9th
// invocations, it will check the relay state from Thingspeak.  That is it does this every
// minute if DELAY is 30000 millseconds.  On the 10th invocation, which is every 5 minutes,
// it will send temperature and humidity data to MQTT and Thingspeak.
//
LOCAL void ICACHE_FLASH_ATTR timer_callback(void *arg)
{
	// send_data_to_mqtt_and_thingspeak();
	struct dht_sensor_data* r = DHTRead();
	float lastTemp = r->temperature;
	float lastHum = r->humidity;
	char str_temp[64],str_hum[64];
	char str_url[256];

	if ((timer_count==1) || (timer_count==3) || (timer_count==5) || (timer_count==7) || (timer_count==9))
	{
		// Get last field1 value of Thingspeak channel that corresponds to the relay state
		os_sprintf(str_url,"http://api.thingspeak.com/channels/%s/fields/field1/last",YOUR_THINGSPEAK_CHANNEL);
		http_get(str_url, http_get_relay_state_callback);

	}
	else if (timer_count==10)
	{
		if(r->success)
		{
			// Send temperature and humidity data to MQTT broker
			INFO("Temperature: %d.%d *C, Humidity: %d.%d %%\r\n", (int)(lastTemp),(int)((lastTemp - (int)lastTemp)*100), (int)(lastHum),(int)((lastHum - (int)lastHum)*100));
			os_sprintf(str_temp,"%d.%d", (int)(lastTemp),(int)((lastTemp - (int)lastTemp)*100));
			MQTT_Publish(&mqttClient, "/esp8266/temperature", str_temp, os_strlen(str_temp), 0, 0);

			os_sprintf(str_hum,"%d.%d", (int)(lastHum),(int)((lastHum - (int)lastHum)*100));
			MQTT_Publish(&mqttClient, "/esp8266/humidity", str_hum, os_strlen(str_hum), 0, 0);

			// Send temperature and humidity data to Thingspeak.com
			os_sprintf(str_url,"http://api.thingspeak.com/update?key=%s&field2=%s&field3=%s",YOUR_THINGSPEAK_API_KEY,str_temp,str_hum);
			http_post(str_url,"",http_post_callback);
		}
		else
		{
			INFO("Error reading temperature and humidity\r\n");
		}
		timer_count = 1;
	}

	timer_count++;
}

void wifiConnectCb(uint8_t status)
{
	if(status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	}
}

void mqttConnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Connected\r\n");
	MQTT_Subscribe(&mqttClient, "/esp8266/temperature", 2);
	MQTT_Subscribe(&mqttClient, "/esp8266/humidity", 2);
    MQTT_Subscribe(&mqttClient, "/esp8266/relay", 2);
}

void mqttDisconnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\r\n");
}

void mqttPublishedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

//
// This function is called whenever a message is received on any of the subscribed topics
// In our case, only messages on the 'relay' topic are processed and commands sent to
// the GPIO0 pin to turn on/off the relay.
//
void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
	char topicBuf[64], dataBuf[64], str_url[256];
	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	INFO("MQTT topic: %s, data: %s \r\n", topicBuf, dataBuf);

	if (os_strcmp(topicBuf,"/esp8266/relay") == 0)
	{
		if((os_strcmp(dataBuf,"on") == 0) || (os_strcmp(dataBuf,"ON") == 0))
		{
			if(bRelayInputInverted)
			{
			    GPIO_OUTPUT_SET(RELAY_GPIO, 0);
			}
			else
			{
			    GPIO_OUTPUT_SET(RELAY_GPIO, 1);
			}
            relay_state = 1;

			// Send relay state to Thingspeak.com
			os_sprintf(str_url,"http://api.thingspeak.com/update?key=%s&field1=1.0", YOUR_THINGSPEAK_API_KEY);
			http_post(str_url,"",http_post_callback);
		}
		else if((os_strcmp(dataBuf,"off") == 0) || (os_strcmp(dataBuf,"OFF") == 0))
		{
			if(bRelayInputInverted)
			{
			    GPIO_OUTPUT_SET(RELAY_GPIO, 1);
			}
			else
			{
			    GPIO_OUTPUT_SET(RELAY_GPIO, 0);
			}
            relay_state = -1;

			// Send relay state to Thingspeak.com
			os_sprintf(str_url,"http://api.thingspeak.com/update?key=%s&field1=-1.0", YOUR_THINGSPEAK_API_KEY);
			http_post(str_url,"",http_post_callback);
		}
	}
}

//
// This is the 'main' function.
//
void user_init(void)
{
	// Initialize GPIO0
	PIN_FUNC_SELECT(RELAY_GPIO_MUX, RELAY_GPIO_FUNC);
	GPIO_OUTPUT_SET(RELAY_GPIO, 1);

	// Initialize DHT11/22 sensor
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	DHTInit(DHT11, 2000);

	// Load the WiFi and MQTT login details from /include/user_config.h
	CFG_Load();

	os_delay_us(3000000);

	// Connect to WiFi.
	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);

	// Initialize MQTT
	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, SEC_NONSSL);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	// Start a timer that calls timer_callback every DELAY milliseconds.
	os_timer_disarm(&timer);
	os_timer_setfn(&timer, (os_timer_func_t *)timer_callback, (void *)0);
	os_timer_arm(&timer, DELAY, 1);

	INFO("\r\nSystem started ...\r\n");
}
