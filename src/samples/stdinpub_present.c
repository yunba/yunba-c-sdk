/*******************************************************************************
 * Copyright (c) 2012, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *   http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial contribution
 *******************************************************************************/
 
 /*
 stdin publisher
 
 compulsory parameters:
 
  --topic topic to publish on
 
 defaulted parameters:
 
	--host localhost
	--port 1883
	--qos 0
	--delimiters \n
	--clientid stdin_publisher
	--maxdatalen 100
	
	--userid none
	--password none
 
*/
#include "yunba.h"
//#include "MQTTClient.h"
#include "MQTTClientPersistence.h"

#include <stdio.h>
#include <signal.h>
#include <memory.h>


#if defined(WIN32)
#include <Windows.h>
#define sleep Sleep
#else
#include <time.h>
#include <stdlib.h>
#endif


REG_info my_reg_info;


volatile int toStop = 0;


void usage()
{
	printf("MQTT stdin publisher\n");
	printf("Usage: stdinpub topicname <options>, where options are:\n");
	printf("  --qos <qos> (default is 0)\n");
	printf("  --retained (default is off)\n");
	printf("  --delimiter <delim> (default is \\n)");
	printf("  --maxdatalen 100\n");
	printf("  --appkey xxxxxxxxxxxxxxxx\n");
	printf("  --deviceid xxxxxxxxxxxxxxxx\n");
	exit(-1);
}


void myconnect(MQTTClient* client, MQTTClient_connectOptions* opts)
{
	printf("Connecting\n");
	if (MQTTClient_connect(*client, opts) != 0)
	{
		printf("Failed to connect\n");
		exit(-1);	
	}
}


void cfinish(int sig)
{
	signal(SIGINT, NULL);
	toStop = 1;
}


struct
{
//	char* clientid;
	char* delimiter;
	int maxdatalen;
	int qos;
	int retained;
	char *appkey;
	char *deviceid;
	char *alias;
//	char* username;
//	char* password;
//	char* host;
//	char* port;
  int verbose;
} opts =
{
	"\n", 100, 0, 0, NULL, 0
};

Presence_msg my_present;
	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

void getopts(int argc, char** argv);


int extendedCmdArrive(void *context, EXTED_CMD cmd, int status, int ret_string_len, char *ret_string)
{
	char buf[1024];
	memset(buf, 0, 1024);
	memcpy(buf, ret_string, ret_string_len);
	printf("%s:%02x,%02x,%02x, %s\n", __func__, cmd, status, ret_string_len, buf);

}

int messageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* m)
{
	char action[30];
	char alias[60];
	int ret = -1;
	int i;
	char* payloadptr;

	my_present.action = action;
	my_present.alias = alias;
	ret = get_present_info(topicName, m, &my_present);
	if (ret == 0)
		printf("action:%s alias:%s\n", my_present.action, my_present.alias);
	time_t t;
	time(&t);
	printf("Message arrived, date:%s", ctime(&t));
	printf("     qos: %i\n", m->qos);
	printf("     messageid: %"PRIu64"\n", m->msgid);
	printf("     topic: %s\n", topicName);
	printf("   message: ");

	payloadptr = m->payload;
	for(i = 0; i < m->payloadlen; i++)
	{
		putchar(*payloadptr++);
	}
	putchar('\n');
	MQTTClient_freeMessage(&m);
	MQTTClient_free(topicName);

	/* not expecting any messages */
	return 1;
}

void connectionLost(void *context, char *cause)
{
	myconnect(&client, &conn_opts);
	printf("%s, %s, %s\r\n", __func__, context, cause);

}

int main(int argc, char** argv)
{
	//MQTTClient client;
//	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	char* topic = NULL;
	char* buffer = NULL;
	int rc = 0;
	char url[100];
	char broker[100];

	if (argc < 2)
		usage();
	
	getopts(argc, argv);
	
//	sprintf(url, "%s:%s", opts.host, opts.port);
//  if (opts.verbose)
//		printf("URL is %s\n", url);
	
	topic = argv[1];
	printf("Using topic %s\n", topic);

	int res = MQTTClient_setup_with_appkey_and_deviceid(opts.appkey, opts.deviceid, &my_reg_info);
	if (res < 0) {
		printf("can't get reg info\n");
		return 0;
	}

	printf("Get reg info: client_id:%s,username:%s,password:%s, devide_id:%s\n", my_reg_info.client_id, my_reg_info.username, my_reg_info.password, my_reg_info.device_id);

	res = MQTTClient_get_host(opts.appkey, url);
	if (res < 0) {
		printf("can't get host info\n");
		return 0;
	}
	printf("Get url info: %s\n", url);

	rc = MQTTClient_create(&client, url, my_reg_info.client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTClient_get_broker(&client, broker);
	printf("get broker:%s\n", broker);

//	MQTTClient_set_broker(&client, "localhost");

	MQTTClient_get_broker(&client, broker);
	printf("get broker:%s\n", broker);

	signal(SIGINT, cfinish);
	signal(SIGTERM, cfinish);

	rc = MQTTClient_setCallbacks(client, NULL, connectionLost, messageArrived, NULL, extendedCmdArrive);

	conn_opts.keepAliveInterval = 300;
	conn_opts.reliable = 0;
	conn_opts.cleansession = 1;
	conn_opts.username = my_reg_info.username;
	conn_opts.password = my_reg_info.password;
	
	myconnect(&client, &conn_opts);

	buffer = malloc(opts.maxdatalen);

	rc = MQTTClient_subscribe(client, topic, 1);
	printf("subscribe topic:%s, %i\n", topic, rc);

	if (opts.alias != NULL) {
		printf("set alias: %s\n", opts.alias);
		MQTTClient_set_alias(client, opts.alias);
	}
	//MQTTClient_presence(client, topic);
	int ret;
	ret = MQTTClient_get_aliaslist(client, topic);
	printf("get aliaslist:%i, topic:%s\n", ret, topic);
	ret = MQTTClient_get_topic(client, "band1111");
	printf("get topic:%i\n", ret);
	ret = MQTTClient_get_status(client, "band1111");
	printf("get status:%i\n", ret);

	ret = MQTTClient_report(client, "domytest", "abc");
	printf("report status:%i\n", ret);

	MQTTClient_get_status2(client, "baidu");
	MQTTClient_get_topiclist2(client, topic);
	MQTTClient_get_aliaslist2(client, topic);

	sleep(7);
	cJSON *apn_json, *aps;
	cJSON *Opt = cJSON_CreateObject();
	cJSON_AddStringToObject(Opt,"time_to_live",  "120");
	cJSON_AddStringToObject(Opt,"time_delay",  "1100");
	cJSON_AddStringToObject(Opt,"apn_json",  "{\"aps\":{\"alert\":\"FENCE alarm\", \"sound\":\"alarm.mp3\"}}");
	ret = MQTTClient_publish2(client, topic, strlen("test"), "test", Opt);
	cJSON_Delete(Opt);
	printf("publish2 status:%i\n", ret);
	
	while (!toStop)
	{
		int data_len = 0;
		int delim_len = 0;
		
		delim_len = strlen(opts.delimiter);
		do
		{
			buffer[data_len++] = getchar();
			if (data_len > delim_len)
			{
			//printf("comparing %s %s\n", opts.delimiter, &buffer[data_len - delim_len]);
			if (strncmp(opts.delimiter, &buffer[data_len - delim_len], delim_len) == 0)
				break;
			}
		} while (data_len < opts.maxdatalen);
				
		if (opts.verbose)
				printf("Publishing data of length %d\n", data_len);
		rc = MQTTClient_publish(client, topic, data_len, buffer);
		if (rc != 0)
		{
			myconnect(&client, &conn_opts);
			rc = MQTTClient_publish(client, topic, data_len, buffer);
			printf("reconnect %i\n", rc);
		}
		if (opts.qos > 0)
			MQTTClient_yield();
	}
	
	printf("Stopping\n");
	
	free(buffer);

	MQTTClient_disconnect(client, 0);

 	MQTTClient_destroy(&client);

	return 0;
}

void getopts(int argc, char** argv)
{
	int count = 2;
	
	while (count < argc)
	{
		if (strcmp(argv[count], "--retained") == 0)
			opts.retained = 1;
		if (strcmp(argv[count], "--verbose") == 0)
			opts.verbose = 1;
		else if (strcmp(argv[count], "--qos") == 0)
		{
			if (++count < argc)
			{
				if (strcmp(argv[count], "0") == 0)
					opts.qos = 0;
				else if (strcmp(argv[count], "1") == 0)
					opts.qos = 1;
				else if (strcmp(argv[count], "2") == 0)
					opts.qos = 2;
				else
					usage();
			}
			else
				usage();
		}
#if 0
		else if (strcmp(argv[count], "--host") == 0)
		{
			if (++count < argc)
				opts.host = argv[count];
			else
				usage();
		}
		else if (strcmp(argv[count], "--port") == 0)
		{
			if (++count < argc)
				opts.port = argv[count];
			else
				usage();
		}
#endif
		else if (strcmp(argv[count], "--appkey") == 0)
		{
			if (++count < argc)
				opts.appkey = argv[count];
			else
				usage();
		}

		else if (strcmp(argv[count], "--deviceid") == 0)
		{
			if (++count < argc)
				opts.deviceid = argv[count];
		}
		else if (strcmp(argv[count], "--alias") == 0)
		{
			if (++count < argc)
				opts.alias = argv[count];
			else
				usage();
		}
/*
		else if (strcmp(argv[count], "--clientid") == 0)
		{
			if (++count < argc)
				opts.clientid = argv[count];
			else
				usage();
		}
		else if (strcmp(argv[count], "--username") == 0)
		{
			if (++count < argc)
				opts.username = argv[count];
			else
				usage();
		}
		else if (strcmp(argv[count], "--password") == 0)
		{
			if (++count < argc)
				opts.password = argv[count];
			else
				usage();
		}
*/
		else if (strcmp(argv[count], "--maxdatalen") == 0)
		{
			if (++count < argc)
				opts.maxdatalen = atoi(argv[count]);
			else
				usage();
		}
		else if (strcmp(argv[count], "--delimiter") == 0)
		{
			if (++count < argc)
				opts.delimiter = argv[count];
			else
				usage();
		}
		count++;
	}
	
}

