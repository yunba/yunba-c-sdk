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
 *    Ian Craggs - fix for bug 413429 - connectionLost not called
 *******************************************************************************/

/*
 
 stdout subscriber for the asynchronous client
 
 compulsory parameters:
 
  --topic topic to subscribe to
 
 defaulted parameters:
 
	--host localhost
	--port 1883
	--qos 2
	--delimiter \n
	--clientid stdout_subscriber
	
	--userid none
	--password none
 
*/

#include "MQTTAsync.h"
#include "MQTTClientPersistence.h"

#include <stdio.h>
#include <signal.h>
#include <memory.h>


#if defined(WIN32)
#include <Windows.h>
#define sleep Sleep
#else
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#include "yunba.h"

#define BUF_LEN 500

volatile int finished = 0;
char* topic = NULL;
int subscribed = 0;
int disconnected = 0;

int toStop = 0;

static int published = 0;

REG_info my_reg_info;

void cfinish(int sig)
{
	signal(SIGINT, NULL);
	finished = 1;
}


struct
{
	char* clientid;
  int nodelimiter;
	char *delimiter;
	int qos;
	char *appkey;
	char *deviceid;
	char *alias;
//	char* username;
//	char* password;
//	char* host;
//	char* port;
  int showtopics;
} opts =
{
	"stdout-subscriber", 1, "\n", 1, NULL, NULL, NULL, 0
};


void usage()
{
	printf("MQTT stdout subscriber\n");
	printf("Usage: stdoutsub topicname <options>, where options are:\n");
//	printf("  --host <hostname> (default is localhost)\n");
//	printf("  --port <port> (default is 1883)\n");
	printf("  --qos <qos> (default is 2)\n");
	printf("  --delimiter <delim> (default is no delimiter)\n");
//	printf("  --clientid <clientid> (default is hostname+timestamp)\n");
//	printf("  --username none\n");
//	printf("  --password none\n");
	printf("  --appkey xxxxxxxxxxxxxxxx\n");
	printf("  --deviceid xxxxxxxxxxxxxxxx\n");
	printf("  --showtopics <on or off> (default is on if the topic has a wildcard, else off)\n");
	exit(-1);
}


void getopts(int argc, char** argv)
{
	int count = 2;
	
	while (count < argc)
	{
		if (strcmp(argv[count], "--qos") == 0)
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
//		else if (strcmp(argv[count], "--host") == 0)
//		{
//			if (++count < argc)
//				opts.host = argv[count];
//			else
//				usage();
//		}
//		else if (strcmp(argv[count], "--port") == 0)
//		{
//			if (++count < argc)
//				opts.port = argv[count];
//			else
//				usage();
//		}
//		else if (strcmp(argv[count], "--clientid") == 0)
//		{
//			if (++count < argc)
//				opts.clientid = argv[count];
//			else
//				usage();
//		}
//		else if (strcmp(argv[count], "--username") == 0)
//		{
//			if (++count < argc)
//				opts.username = argv[count];
//			else
//				usage();
//		}
//		else if (strcmp(argv[count], "--password") == 0)
//		{
//			if (++count < argc)
//				opts.password = argv[count];
//			else
//				usage();
//		}
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
		else if (strcmp(argv[count], "--delimiter") == 0)
		{
			if (++count < argc)
			{
				if (strcmp("newline", argv[count]) == 0)
					opts.delimiter = '\n';
				else
					opts.delimiter = argv[count][0];
				opts.nodelimiter = 0;
			}
			else
				usage();
		}
		else if (strcmp(argv[count], "--showtopics") == 0)
		{
			if (++count < argc)
			{
				if (strcmp(argv[count], "on") == 0)
					opts.showtopics = 1;
				else if (strcmp(argv[count], "off") == 0)
					opts.showtopics = 0;
				else
					usage();
			}
			else
				usage();
		}
		count++;
	}
	
}


int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
//	if (opts.showtopics)
		printf("topic: %s\t", topicName);
	if (opts.nodelimiter)
		printf("msg: %.*s\n", message->payloadlen, (char*)message->payload);
//	else
//		printf("message: %.*s%c", message->payloadlen, (char*)message->payload, opts.delimiter);
	fflush(stdout);
	MQTTAsync_freeMessage(&message);
	MQTTAsync_free(topicName);
  return 1;
}


void onDisconnect(void* context, MQTTAsync_successData* response)
{
	disconnected = 1;
}


void onSubscribe(void* context, MQTTAsync_successData* response)
{
	printf("Subscribe ok\n");
	subscribed = 1;
}


void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
	printf("Subscribe failed, rc %d\n", response->code);
	finished = 1;
}


void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
	printf("Connect failed, rc %d\n", response->code);
	finished = 1;
}


void onConnect(void* context, MQTTAsync_successData* response)
{
	MQTTAsync client = (MQTTAsync)context;
	MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
	MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
	int rc;

	//if (opts.showtopics)
		printf("Subscribing to topic %s with client %s at QoS %d\n", topic, opts.clientid, opts.qos);

	ropts.onSuccess = onSubscribe;
	ropts.onFailure = onSubscribeFailure;
	ropts.context = client;
	if ((rc = MQTTAsync_subscribe(client, topic, opts.qos, &ropts)) != MQTTASYNC_SUCCESS)
	{
		printf("Failed to start subscribe, return code %d\n", rc);
    		finished = 1;	
	}
}


MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;


void connectionLost(void *context, char *cause)
{
	MQTTAsync client = (MQTTAsync)context;
	int rc;

	printf("connectionLost called\n");
	if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
	{
		printf("Failed to start reconnect, return code %d\n", rc);
		finished = 1;
	}
}

void onPublishFailure(void* context, MQTTAsync_failureData* response)
{
	printf("Publish failed, rc %d\n", response ? -1 : response->code);
	published = -1;
}


void onPublish(void* context, MQTTAsync_successData* response)
{
	MQTTAsync client = (MQTTAsync)context;

	published = 1;
	printf("publish ok\n");
}

int extendedCmdArrive(void *context, uint16_t cmd, int status, int ret_string_len, char *ret_string)
{
	char buf[1024];
	memset(buf, 0, 1024);
	memcpy(buf, ret_string, ret_string_len);
	printf("%s:%02x,%02x,%02x, %s\n", "extendedCmdArrive", cmd, status, ret_string_len, buf);

}



int main(int argc, char** argv)
{
	MQTTAsync client;
	MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
	MQTTAsync_responseOptions pub_opts = MQTTAsync_responseOptions_initializer;
	int rc = 0;
	char url[100];
	
	char *buffer = malloc(BUF_LEN);

	if (argc < 2)
		usage();
	
	topic = argv[1];

	if (strchr(topic, '#') || strchr(topic, '+'))
		opts.showtopics = 1;
	if (opts.showtopics)
		printf("topic is %s\n", topic);

	getopts(argc, argv);	
//	sprintf(url, "%s:%s", opts.host, opts.port);

	int res = MQTTClient_setup_with_appkey_and_deviceid_v2(opts.appkey, opts.deviceid, &my_reg_info);
	if (res < 0) {
		printf("can't get reg info\n");
		return 0;
	}

	printf("Get reg info: client_id:%s,username:%s,password:%s, devide_id:%s\n", my_reg_info.client_id, my_reg_info.username, my_reg_info.password, my_reg_info.device_id);

	res = MQTTClient_get_host_v2(opts.appkey, url);
	if (res < 0) {
		printf("can't get host info\n");
		return 0;
	}
	printf("Get url info: %s\n", url);


	rc = MQTTAsync_create(&client, url, my_reg_info.client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);

	MQTTAsync_setCallbacks(client, client, connectionLost, messageArrived, NULL, extendedCmdArrive);

	signal(SIGINT, cfinish);
	signal(SIGTERM, cfinish);

	conn_opts.keepAliveInterval = 300;
	conn_opts.cleansession = 1;
	conn_opts.username = my_reg_info.username;
	conn_opts.password = my_reg_info.password;
	conn_opts.onSuccess = onConnect;
	conn_opts.onFailure = onConnectFailure;
	conn_opts.context = client;
	if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
	{
		printf("Failed to start connect, return code %d\n", rc);
		exit(-1);	
	}

#if 0
	while (!subscribed)
		#if defined(WIN32)
			Sleep(100);
		#else
			usleep(10000L);
		#endif
#endif
printf("now to test publish\n");
		while (!toStop)
		{
			int data_len = 0;
			int delim_len = 0;

			delim_len = strlen(opts.delimiter);
//			printf("------>%d\n", delim_len);
			do
			{
				buffer[data_len++] = getchar();
				if (data_len > delim_len)
				{
			//	printf("comparing %s %s\n", opts.delimiter, &buffer[data_len - delim_len]);
				if (strncmp(opts.delimiter, &buffer[data_len - delim_len], delim_len) == 0)
					break;
				}
			} while (data_len < BUF_LEN);

	//		if (opts.verbose)
					printf("Publishing data of length %d\n", data_len);
			pub_opts.onSuccess = onPublish;
			pub_opts.onFailure = onPublishFailure;
		//	do
			{
				published = 0;
				rc = MQTTAsync_send(client, topic, data_len, buffer, opts.qos, 1, &pub_opts);
				printf("------->publish, %d\n", rc);

				MQTTAsyn_publish2(client,
						topic,
						data_len,
						buffer,
						NULL,
						&pub_opts);
//				while (published == 0)
//					#if defined(WIN32)
//					Sleep(100);
//					#else
//					usleep(1000L);
//					#endif
//				if (published == -1)
//					printf("send fail\n");
					//myconnect(&client);
			}
		//	while (published != 1);
		}

		free(buffer);

	if (finished)
		goto exit;

	while (!finished)
		#if defined(WIN32)
			Sleep(100);
		#else
			usleep(10000L);
		#endif

	disc_opts.onSuccess = onDisconnect;
	if ((rc = MQTTAsync_disconnect(client, &disc_opts)) != MQTTASYNC_SUCCESS)
	{
		printf("Failed to start disconnect, return code %d\n", rc);
    exit(-1);	
	}

  while	(!disconnected)
		#if defined(WIN32)
			Sleep(100);
		#else
			usleep(10000L);
		#endif

exit:
	MQTTAsync_destroy(&client);

	return 0;
}


