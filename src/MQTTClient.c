/*******************************************************************************
 * Copyright (c) 2009, 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs - bug 384016 - segv setting will message
 *    Ian Craggs - bug 384053 - v1.0.0.7 - stop MQTTClient_receive on socket error 
 *    Ian Craggs, Allan Stockdill-Mander - add ability to connect with SSL
 *    Ian Craggs - multiple server connection support
 *    Ian Craggs - fix for bug 413429 - connectionLost not called
 *    Ian Craggs - fix for bug 421103 - trying to write to same socket, in publish/retries
 *    Ian Craggs - fix for bug 419233 - mutexes not reporting errors
 *    Ian Craggs - fix for bug 420851
 *    Ian Craggs - fix for bug 432903 - queue persistence
 *******************************************************************************/

/**
 * @file
 * \brief Synchronous API implementation
 *
 */

#define _GNU_SOURCE /* for pthread_mutexattr_settype */
#include <stdlib.h>
#if !defined(WIN32) && !defined(WIN64)
	#include <sys/time.h>
#endif

//#include "MQTTClient.h"
#include "yunba.h"
#if !defined(NO_PERSISTENCE)
#include "MQTTPersistence.h"
#endif

#include "utf-8.h"
#include "MQTTProtocol.h"
#include "MQTTProtocolOut.h"
#include "Thread.h"
#include "SocketBuffer.h"
#include "StackTrace.h"
#include "Heap.h"
#include "cJSON.h"

#if defined(OPENSSL)
#include <openssl/ssl.h>
#endif

#define URI_TCP "tcp://"

#define BUILD_TIMESTAMP "##MQTTCLIENT_BUILD_TAG##"
#define CLIENT_VERSION  "##MQTTCLIENT_VERSION_TAG##"

#define DEFAULT_QOS 1
#define DEFAULT_RETAINED 0

char* client_timestamp_eye = "MQTTClientV3_Timestamp " BUILD_TIMESTAMP;
char* client_version_eye = "MQTTClientV3_Version " CLIENT_VERSION;

static ClientStates ClientState =
{
	CLIENT_VERSION, /* version */
	NULL /* client list */
};

ClientStates* bstate = &ClientState;

MQTTProtocol state;

#if defined(WIN32) || defined(WIN64)
static mutex_type mqttclient_mutex = NULL;
extern mutex_type stack_mutex;
extern mutex_type heap_mutex;
extern mutex_type log_mutex;
BOOL APIENTRY DllMain(HANDLE hModule,
					  DWORD  ul_reason_for_call,
					  LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			Log(TRACE_MAX, -1, "DLL process attach");
			if (mqttclient_mutex == NULL)
			{
				mqttclient_mutex = CreateMutex(NULL, 0, NULL);
				stack_mutex = CreateMutex(NULL, 0, NULL);
				heap_mutex = CreateMutex(NULL, 0, NULL);
				log_mutex = CreateMutex(NULL, 0, NULL);
			}
		case DLL_THREAD_ATTACH:
			Log(TRACE_MAX, -1, "DLL thread attach");
		case DLL_THREAD_DETACH:
			Log(TRACE_MAX, -1, "DLL thread detach");
		case DLL_PROCESS_DETACH:
			Log(TRACE_MAX, -1, "DLL process detach");
	}
	return TRUE;
}
#else
static pthread_mutex_t mqttclient_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type mqttclient_mutex = &mqttclient_mutex_store;

void MQTTClient_init()
{
	pthread_mutexattr_t attr;
	int rc;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	if ((rc = pthread_mutex_init(mqttclient_mutex, &attr)) != 0)
		printf("MQTTAsync: error %d initializing client_mutex\n", rc);
}

#define WINAPI
#endif

static volatile int initialized = 0;
static List* handles = NULL;
static time_t last;
static int running = 0;
static int tostop = 0;
static thread_id_type run_id = 0;

MQTTPacket* MQTTClient_waitfor(MQTTClient handle, int packet_type, int* rc, long timeout);
MQTTPacket* MQTTClient_cycle(int* sock, unsigned long timeout, int* rc);
int MQTTClient_cleanSession(Clients* client);
void MQTTClient_stop();
int MQTTClient_disconnect_internal(MQTTClient handle, int timeout);
void MQTTClient_writeComplete(int socket);

typedef struct
{
	MQTTClient_message* msg;
	char* topicName;
	int topicLen;
	unsigned int seqno; /* only used on restore */
} qEntry;


typedef struct
{
	char* serverURI;
#if defined(OPENSSL)
	int ssl;
#endif
	Clients* c;
	MQTTClient_connectionLost* cl;
	MQTTClient_messageArrived* ma;
	MQTTClient_deliveryComplete* dc;
	MQTTClient_extendedCmdArrive *eca;
	void* context;

	sem_type connect_sem;
	int rc; /* getsockopt return code in connect */
	sem_type connack_sem;
	sem_type suback_sem;
	sem_type unsuback_sem;
	MQTTPacket* pack;

} MQTTClients;

void MQTTClient_sleep(long milliseconds)
{
	FUNC_ENTRY;
#if defined(WIN32) || defined(WIN64)
	Sleep(milliseconds);
#else
	usleep(milliseconds*1000);
#endif
	FUNC_EXIT;
}


#if defined(WIN32) || defined(WIN64)
#define START_TIME_TYPE DWORD
START_TIME_TYPE MQTTClient_start_clock(void)
{
	return GetTickCount();
}
#elif defined(AIX)
#define START_TIME_TYPE struct timespec
START_TIME_TYPE MQTTClient_start_clock(void)
{
	static struct timespec start;
	clock_gettime(CLOCK_REALTIME, &start);
	return start;
}
#else
#define START_TIME_TYPE struct timeval
START_TIME_TYPE MQTTClient_start_clock(void)
{
	static struct timeval start;
	gettimeofday(&start, NULL);
	return start;
}
#endif


#if defined(WIN32) || defined(WIN64)
long MQTTClient_elapsed(DWORD milliseconds)
{
	return GetTickCount() - milliseconds;
}
#elif defined(AIX)
#define assert(a)
long MQTTClient_elapsed(struct timespec start)
{
	struct timespec now, res;

	clock_gettime(CLOCK_REALTIME, &now);
	ntimersub(now, start, res);
	return (res.tv_sec)*1000L + (res.tv_nsec)/1000000L;
}
#else
long MQTTClient_elapsed(struct timeval start)
{
	struct timeval now, res;

	gettimeofday(&now, NULL);
	timersub(&now, &start, &res);
	return (res.tv_sec)*1000 + (res.tv_usec)/1000;
}
#endif


int MQTTClient_create(MQTTClient* handle, char* serverURI, char* clientId,
		int persistence_type, void* persistence_context)
{
	int rc = 0;
	MQTTClients *m = NULL;

	FUNC_ENTRY;
	rc = Thread_lock_mutex(mqttclient_mutex);

	if (serverURI == NULL || clientId == NULL)
	{
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	if (!UTF8_validateString(clientId))
	{
		rc = MQTTCLIENT_BAD_UTF8_STRING;
		goto exit;
	}

	if (!initialized)
	{
		#if defined(HEAP_H)
			Heap_initialize();
		#endif
		Log_initialize((Log_nameValue*)MQTTClient_getVersionInfo());
		bstate->clients = ListInitialize();
		Socket_outInitialize();
		Socket_setWriteCompleteCallback(MQTTClient_writeComplete);
		handles = ListInitialize();
#if defined(OPENSSL)
		SSLSocket_initialize();
#endif
		initialized = 1;
	}
	m = malloc(sizeof(MQTTClients));
	*handle = m;
	memset(m, '\0', sizeof(MQTTClients));
	if (strncmp(URI_TCP, serverURI, strlen(URI_TCP)) == 0)
		serverURI += strlen(URI_TCP);
#if defined(OPENSSL)
	else if (strncmp(URI_SSL, serverURI, strlen(URI_SSL)) == 0)
	{
		serverURI += strlen(URI_SSL);
		m->ssl = 1;
	}
#endif
	m->serverURI = malloc(strlen(serverURI)+1);
	strcpy(m->serverURI, serverURI);

//	printf("%s,%s\n", __func__, m->serverURI);
	ListAppend(handles, m, sizeof(MQTTClients));

	m->c = malloc(sizeof(Clients));
	memset(m->c, '\0', sizeof(Clients));
	m->c->context = m;
	m->c->outboundMsgs = ListInitialize();
	m->c->inboundMsgs = ListInitialize();
	m->c->messageQueue = ListInitialize();
	m->c->clientID = malloc(strlen(clientId)+1);
	strcpy(m->c->clientID, clientId);
	m->connect_sem = Thread_create_sem();
	m->connack_sem = Thread_create_sem();
	m->suback_sem = Thread_create_sem();
	m->unsuback_sem = Thread_create_sem();

#if !defined(NO_PERSISTENCE)
	rc = MQTTPersistence_create(&(m->c->persistence), persistence_type, persistence_context);
	if (rc == 0)
	{
		rc = MQTTPersistence_initialize(m->c, m->serverURI);
		if (rc == 0)
			MQTTPersistence_restoreMessageQueue(m->c);
	}
#endif
	ListAppend(bstate->clients, m->c, sizeof(Clients) + 3*sizeof(List));

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


void MQTTClient_terminate(void)
{
	FUNC_ENTRY;
	MQTTClient_stop();
	if (initialized)
	{
		ListFree(bstate->clients);
		ListFree(handles);
		handles = NULL;
		Socket_outTerminate();
#if defined(OPENSSL)
		SSLSocket_terminate();
#endif
		#if defined(HEAP_H)
			Heap_terminate();
		#endif
		Log_terminate();
		initialized = 0;
	}
	FUNC_EXIT;
}


void MQTTClient_emptyMessageQueue(Clients* client)
{
	FUNC_ENTRY;
	/* empty message queue */
	if (client->messageQueue->count > 0)
	{
		ListElement* current = NULL;
		while (ListNextElement(client->messageQueue, &current))
		{
			qEntry* qe = (qEntry*)(current->content);
			free(qe->topicName);
			free(qe->msg->payload);
			free(qe->msg);
		}
		ListEmpty(client->messageQueue);
	}
	FUNC_EXIT;
}


void MQTTClient_destroy(MQTTClient* handle)
{
	MQTTClients* m = *handle;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL)
		goto exit;

	if (m->c)
	{
		int saved_socket = m->c->net.socket;
		char* saved_clientid = malloc(strlen(m->c->clientID)+1);
		strcpy(saved_clientid, m->c->clientID);
#if !defined(NO_PERSISTENCE)
		MQTTPersistence_close(m->c);
#endif
		MQTTClient_emptyMessageQueue(m->c);
		MQTTProtocol_freeClient(m->c);
		if (!ListRemove(bstate->clients, m->c))
			Log(LOG_ERROR, 0, NULL);
		else
			Log(TRACE_MIN, 1, NULL, saved_clientid, saved_socket);
		free(saved_clientid);
	}
	if (m->serverURI)
		free(m->serverURI);
	Thread_destroy_sem(m->connect_sem);
	Thread_destroy_sem(m->connack_sem);
	Thread_destroy_sem(m->suback_sem);
	Thread_destroy_sem(m->unsuback_sem);
	if (!ListRemove(handles, m))
		Log(LOG_ERROR, -1, "free error");
	*handle = NULL;
	if (bstate->clients->count == 0)
		MQTTClient_terminate();

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT;
}


void MQTTClient_freeMessage(MQTTClient_message** message)
{
	FUNC_ENTRY;
	free((*message)->payload);
	free(*message);
	*message = NULL;
	FUNC_EXIT;
}


void MQTTClient_free(void* memory)
{
	FUNC_ENTRY;
	free(memory);
	FUNC_EXIT;
}


int MQTTClient_deliverMessage(int rc, MQTTClients* m, char** topicName, int* topicLen, MQTTClient_message** message)
{
	qEntry* qe = (qEntry*)(m->c->messageQueue->first->content);

	FUNC_ENTRY;
	*message = qe->msg;
	*topicName = qe->topicName;
	*topicLen = qe->topicLen;
	if (strlen(*topicName) != *topicLen)
		rc = MQTTCLIENT_TOPICNAME_TRUNCATED;
	ListRemove(m->c->messageQueue, m->c->messageQueue->first->content);
#if !defined(NO_PERSISTENCE)
	if (m->c->persistence)
		MQTTPersistence_unpersistQueueEntry(m->c, (MQTTPersistence_qEntry*)qe);
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * List callback function for comparing clients by socket
 * @param a first integer value
 * @param b second integer value
 * @return boolean indicating whether a and b are equal
 */
int clientSockCompare(void* a, void* b)
{
	MQTTClients* m = (MQTTClients*)a;
	return m->c->net.socket == *(int*)b;
}


/**
 * Wrapper function to call connection lost on a separate thread.  A separate thread is needed to allow the
 * connectionLost function to make API calls (e.g. connect)
 * @param context a pointer to the relevant client
 * @return thread_return_type standard thread return value - not used here
 */
thread_return_type WINAPI connectionLost_call(void* context)
{
	MQTTClients* m = (MQTTClients*)context;

	(*(m->cl))(m->context, NULL);
	return 0;
}


/* This is the thread function that handles the calling of callback functions if set */
thread_return_type WINAPI MQTTClient_run(void* n)
{
	long timeout = 10L; /* first time in we have a small timeout.  Gets things started more quickly */

	FUNC_ENTRY;
	running = 1;
	run_id = Thread_getid();

	Thread_lock_mutex(mqttclient_mutex);
	while (!tostop)
	{
		int rc = SOCKET_ERROR;
		int sock = -1;
		MQTTClients* m = NULL;
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_cycle(&sock, timeout, &rc);
		Thread_lock_mutex(mqttclient_mutex);
		if (tostop)
			break;
		timeout = 1000L;

		/* find client corresponding to socket */
		if (ListFindItem(handles, &sock, clientSockCompare) == NULL)
		{
			/* assert: should not happen */
			continue;
		}
		m = (MQTTClient)(handles->current->content);
		if (m == NULL)
		{
			/* assert: should not happen */
			continue;
		}
		if (rc == SOCKET_ERROR)
		{
			Thread_unlock_mutex(mqttclient_mutex);
			MQTTClient_disconnect_internal(m, 0);
			Thread_lock_mutex(mqttclient_mutex);
		}
		else
		{
			if (m->c->messageQueue->count > 0)
			{
				qEntry* qe = (qEntry*)(m->c->messageQueue->first->content);
				int topicLen = qe->topicLen;

				if (strlen(qe->topicName) == topicLen)
					topicLen = 0;

				Log(TRACE_MIN, -1, "Calling messageArrived for client %s, queue depth %d",
					m->c->clientID, m->c->messageQueue->count);
				Thread_unlock_mutex(mqttclient_mutex);
				rc = (*(m->ma))(m->context, qe->topicName, topicLen, qe->msg);
				Thread_lock_mutex(mqttclient_mutex);
				/* if 0 (false) is returned by the callback then it failed, so we don't remove the message from
				 * the queue, and it will be retried later.  If 1 is returned then the message data may have been freed,
				 * so we must be careful how we use it.
				 */
				if (rc)
					ListRemove(m->c->messageQueue, qe);
				else
					Log(TRACE_MIN, -1, "False returned from messageArrived for client %s, message remains on queue",
						m->c->clientID);
			}
			if (pack)
			{
				if (pack->header.bits.type == CONNACK)
				{
					Log(TRACE_MIN, -1, "Posting connack semaphore for client %s", m->c->clientID);
					m->pack = pack;
					Thread_post_sem(m->connack_sem);
				}
				else if (pack->header.bits.type == SUBACK)
				{
					Log(TRACE_MIN, -1, "Posting suback semaphore for client %s", m->c->clientID);
					m->pack = pack;
					Thread_post_sem(m->suback_sem);
				}
				else if (pack->header.bits.type == UNSUBACK)
				{
					Log(TRACE_MIN, -1, "Posting unsuback semaphore for client %s", m->c->clientID);
					m->pack = pack;
					Thread_post_sem(m->unsuback_sem);
				}
			}
			else if (m->c->connect_state == 1 && !Thread_check_sem(m->connect_sem))
			{
				int error;
				socklen_t len = sizeof(error);

				if ((m->rc = getsockopt(m->c->net.socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len)) == 0)
					m->rc = error;
				Log(TRACE_MIN, -1, "Posting connect semaphore for client %s rc %d", m->c->clientID, m->rc);
				Thread_post_sem(m->connect_sem);
			}
#if defined(OPENSSL)
			else if (m->c->connect_state == 2 && !Thread_check_sem(m->connect_sem))
			{			
				rc = SSLSocket_connect(m->c->net.ssl, m->c->net.socket);
				if (rc == 1 || rc == SSL_FATAL)
				{
					if (rc == 1 && !m->c->cleansession && m->c->session == NULL)
						m->c->session = SSL_get1_session(m->c->net.ssl);
					m->rc = rc;
					Log(TRACE_MIN, -1, "Posting connect semaphore for SSL client %s rc %d", m->c->clientID, m->rc);
					Thread_post_sem(m->connect_sem);
				}
			}
#endif
		}
	}
	run_id = 0;
	running = tostop = 0;
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT;
	return 0;
}


void MQTTClient_stop()
{
	int rc = 0;

	FUNC_ENTRY;
	if (running == 1 && tostop == 0)
	{
		int conn_count = 0;
		ListElement* current = NULL;

		if (handles != NULL)
		{
			/* find out how many handles are still connected */
			while (ListNextElement(handles, &current))
			{
				if (((MQTTClients*)(current->content))->c->connect_state > 0 ||
						((MQTTClients*)(current->content))->c->connected)
					++conn_count;
			}
		}
		Log(TRACE_MIN, -1, "Conn_count is %d", conn_count);
		/* stop the background thread, if we are the last one to be using it */
		if (conn_count == 0)
		{
			int count = 0;
			tostop = 1;
			if (Thread_getid() != run_id)
			{
				while (running && ++count < 100)
				{
					Thread_unlock_mutex(mqttclient_mutex);
					Log(TRACE_MIN, -1, "sleeping");
					MQTTClient_sleep(100L);
					Thread_lock_mutex(mqttclient_mutex);
				}
			}
			rc = 1;
		}
	}
	FUNC_EXIT_RC(rc);
}


int MQTTClient_setCallbacks(MQTTClient handle, void* context, MQTTClient_connectionLost* cl,
														MQTTClient_messageArrived* ma, MQTTClient_deliveryComplete* dc,
														MQTTClient_extendedCmdArrive *eca)
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || ma == NULL || m->c->connect_state != 0)
		rc = MQTTCLIENT_FAILURE;
	else
	{
		m->context = context;
		m->cl = cl;
		m->ma = ma;
		m->dc = dc;
		m->eca = eca;
	}

	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


void MQTTClient_closeSession(Clients* client)
{
	FUNC_ENTRY;
	client->good = 0;
	client->ping_outstanding = 0;
	if (client->net.socket > 0)
	{
		if (client->connected || client->connect_state)
		{
			MQTTPacket_send_disconnect(&client->net, client->clientID);
			client->connected = 0;
			client->connect_state = 0;
		}
#if defined(OPENSSL)
		SSLSocket_close(&client->net);
#endif
		Socket_close(client->net.socket);
		client->net.socket = 0;
#if defined(OPENSSL)
		client->net.ssl = NULL;
#endif
	}

	if (client->cleansession)
		MQTTClient_cleanSession(client);
	FUNC_EXIT;
}


int MQTTClient_cleanSession(Clients* client)
{
	int rc = 0;

	FUNC_ENTRY;
#if !defined(NO_PERSISTENCE)
	rc = MQTTPersistence_clear(client);
#endif
	MQTTProtocol_emptyMessageList(client->inboundMsgs);
	MQTTProtocol_emptyMessageList(client->outboundMsgs);
	MQTTClient_emptyMessageQueue(client);
	client->msgID = 0;
	FUNC_EXIT_RC(rc);
	return rc;
}


void Protocol_processPublication(Publish* publish, Clients* client)
{
	qEntry* qe = NULL;
	MQTTClient_message* mm = NULL;

	FUNC_ENTRY;
	qe = malloc(sizeof(qEntry));
	mm = malloc(sizeof(MQTTClient_message));

	qe->msg = mm;

	qe->topicName = publish->topic;
	qe->topicLen = publish->topiclen;
	publish->topic = NULL;

	/* If the message is QoS 2, then we have already stored the incoming payload
	 * in an allocated buffer, so we don't need to copy again.
	 */
	if (publish->header.bits.qos == 2)
		mm->payload = publish->payload;
	else
	{
		mm->payload = malloc(publish->payloadlen);
		memcpy(mm->payload, publish->payload, publish->payloadlen);
	}

	mm->payloadlen = publish->payloadlen;
	mm->qos = publish->header.bits.qos;
	mm->retained = publish->header.bits.retain;
	if (publish->header.bits.qos == 2)
		mm->dup = 0;  /* ensure that a QoS2 message is not passed to the application with dup = 1 */
	else
		mm->dup = publish->header.bits.dup;
	mm->msgid = publish->msgId;

	ListAppend(client->messageQueue, qe, sizeof(qe) + sizeof(mm) + mm->payloadlen + strlen(qe->topicName)+1);
#if !defined(NO_PERSISTENCE)
	if (client->persistence)
		MQTTPersistence_persistQueueEntry(client, (MQTTPersistence_qEntry*)qe);
#endif
	FUNC_EXIT;
}


int MQTTClient_connectURI(MQTTClient handle, MQTTClient_connectOptions* options, char* serverURI)
{
	MQTTClients* m = handle;
	START_TIME_TYPE start;
	long millisecsTimeout = 30000L;
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	millisecsTimeout = options->connectTimeout * 1000;
	start = MQTTClient_start_clock();
	if (m->ma && !running)
	{
		Thread_start(MQTTClient_run, handle);
		if (MQTTClient_elapsed(start) >= millisecsTimeout)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
		MQTTClient_sleep(100L);
	}

	m->c->keepAliveInterval = options->keepAliveInterval;
	m->c->cleansession = options->cleansession;
	m->c->maxInflightMessages = (options->reliable) ? 1 : 10;

	if (m->c->will)
	{
		free(m->c->will->msg);
		free(m->c->will->topic);
		free(m->c->will);
		m->c->will = NULL;
	}

	if (options->will && options->will->struct_version == 0)
	{
		m->c->will = malloc(sizeof(willMessages));
		m->c->will->msg = malloc(strlen(options->will->message) + 1); 
		strcpy(m->c->will->msg, options->will->message);
		m->c->will->qos = options->will->qos;
		m->c->will->retained = options->will->retained;
		m->c->will->topic = malloc(strlen(options->will->topicName) + 1);
		strcpy(m->c->will->topic, options->will->topicName);
	}
	
#if defined(OPENSSL)
	if (m->c->sslopts)
	{
		if (m->c->sslopts->trustStore)
			free(m->c->sslopts->trustStore);
		if (m->c->sslopts->keyStore)
			free(m->c->sslopts->keyStore);
		if (m->c->sslopts->privateKey)
			free(m->c->sslopts->privateKey);
		if (m->c->sslopts->privateKeyPassword)
			free(m->c->sslopts->privateKeyPassword);
		if (m->c->sslopts->enabledCipherSuites)
			free(m->c->sslopts->enabledCipherSuites);
		free(m->c->sslopts);
		m->c->sslopts = NULL;
	}

	if (options->struct_version != 0 && options->ssl)
	{
		m->c->sslopts = malloc(sizeof(MQTTClient_SSLOptions));
		memset(m->c->sslopts, '\0', sizeof(MQTTClient_SSLOptions));
		if (options->ssl->trustStore)
		{
			m->c->sslopts->trustStore = malloc(strlen(options->ssl->trustStore) + 1);
			strcpy(m->c->sslopts->trustStore, options->ssl->trustStore); 
		}
		if (options->ssl->keyStore)
		{
			m->c->sslopts->keyStore = malloc(strlen(options->ssl->keyStore) + 1);
			strcpy(m->c->sslopts->keyStore, options->ssl->keyStore);
		}
		if (options->ssl->privateKey)
		{
			m->c->sslopts->privateKey = malloc(strlen(options->ssl->privateKey) + 1);
			strcpy(m->c->sslopts->privateKey, options->ssl->privateKey);
		}
		if (options->ssl->privateKeyPassword)
		{
			m->c->sslopts->privateKeyPassword = malloc(strlen(options->ssl->privateKeyPassword) + 1);
			strcpy(m->c->sslopts->privateKeyPassword, options->ssl->privateKeyPassword);
		}
		if (options->ssl->enabledCipherSuites)
		{
			m->c->sslopts->enabledCipherSuites = malloc(strlen(options->ssl->enabledCipherSuites) + 1);
			strcpy(m->c->sslopts->enabledCipherSuites, options->ssl->enabledCipherSuites);
		}
		m->c->sslopts->enableServerCertAuth = options->ssl->enableServerCertAuth;
	}
#endif

	m->c->username = options->username;
	m->c->password = options->password;
	m->c->retryInterval = options->retryInterval;

	Log(TRACE_MIN, -1, "Connecting to serverURI %s", serverURI);
#if defined(OPENSSL)
	rc = MQTTProtocol_connect(serverURI, m->c, m->ssl);
#else
	rc = MQTTProtocol_connect(serverURI, m->c);
#endif
	if (rc == SOCKET_ERROR)
		goto exit;

	if (m->c->connect_state == 0)
	{
		rc = SOCKET_ERROR;
		goto exit;
	}

	if (m->c->connect_state == 1) /* TCP connect started - wait for completion */
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_waitfor(handle, CONNECT, &rc, millisecsTimeout - MQTTClient_elapsed(start));
		Thread_lock_mutex(mqttclient_mutex);
		if (rc != 0)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
		
#if defined(OPENSSL)
		if (m->ssl)
		{
			if (SSLSocket_setSocketForSSL(&m->c->net, m->c->sslopts) != MQTTCLIENT_SUCCESS)
			{
				if (m->c->session != NULL)
					if ((rc = SSL_set_session(m->c->net.ssl, m->c->session)) != 1)
						Log(TRACE_MIN, -1, "Failed to set SSL session with stored data, non critical");
				rc = SSLSocket_connect(m->c->net.ssl, m->c->net.socket);
				if (rc == -1)
					m->c->connect_state = 2;
				else if (rc == SSL_FATAL)
				{
					rc = SOCKET_ERROR;
					goto exit;
				}
				else if (rc == 1) {
					rc = MQTTCLIENT_SUCCESS;
					m->c->connect_state = 3;
					if (MQTTPacket_send_connect(m->c) == SOCKET_ERROR)
					{
						rc = SOCKET_ERROR;
						goto exit;
					}
					if(!m->c->cleansession && m->c->session == NULL)
						m->c->session = SSL_get1_session(m->c->net.ssl);
				}
			}
			else
			{
				rc = SOCKET_ERROR;
				goto exit;
			}
		}
		else
		{
#endif
			m->c->connect_state = 3; /* TCP connect completed, in which case send the MQTT connect packet */
			if (MQTTPacket_send_connect(m->c) == SOCKET_ERROR)
			{
				rc = SOCKET_ERROR;
				goto exit;
			}
#if defined(OPENSSL)
		}
#endif
	}
	
#if defined(OPENSSL)
	if (m->c->connect_state == 2) /* SSL connect sent - wait for completion */
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_waitfor(handle, CONNECT, &rc, millisecsTimeout - MQTTClient_elapsed(start));
		Thread_lock_mutex(mqttclient_mutex);
		if (rc != 1)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
		if(!m->c->cleansession && m->c->session == NULL)
			m->c->session = SSL_get1_session(m->c->net.ssl);
		m->c->connect_state = 3; /* TCP connect completed, in which case send the MQTT connect packet */
		if (MQTTPacket_send_connect(m->c) == SOCKET_ERROR)
		{
			rc = SOCKET_ERROR;
			goto exit;
		}
	}
#endif

	if (m->c->connect_state == 3) /* MQTT connect sent - wait for CONNACK */
	{
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_waitfor(handle, CONNACK, &rc, millisecsTimeout - MQTTClient_elapsed(start));
		Thread_lock_mutex(mqttclient_mutex);
		if (pack == NULL)
			rc = SOCKET_ERROR;
		else
		{
			Connack* connack = (Connack*)pack;
			Log(LOG_PROTOCOL, 1, NULL, m->c->net.socket, m->c->clientID, connack->rc);
			if ((rc = connack->rc) == MQTTCLIENT_SUCCESS)
			{
				m->c->connected = 1;
				m->c->good = 1;
				m->c->connect_state = 0;
				if (m->c->cleansession)
					rc = MQTTClient_cleanSession(m->c);
				if (m->c->outboundMsgs->count > 0)
				{
					ListElement* outcurrent = NULL;

					while (ListNextElement(m->c->outboundMsgs, &outcurrent))
					{
						Messages* m = (Messages*)(outcurrent->content);
						m->lastTouch = 0;
					}
					MQTTProtocol_retry(m->c->net.lastContact, 1);
					if (m->c->connected != 1)
						rc = MQTTCLIENT_DISCONNECTED;
				}
			}
			free(connack);
			m->pack = NULL;
		}
	}

exit:
	if (rc != MQTTCLIENT_SUCCESS)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect(handle, 0); /* not "internal" because we don't want to call connection lost */
		Thread_lock_mutex(mqttclient_mutex);
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_connect(MQTTClient handle, MQTTClient_connectOptions* options)
{
	MQTTClients* m = handle;
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (options == NULL)
	{
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	if (strncmp(options->struct_id, "MQTC", 4) != 0 || 
		(options->struct_version != 0 && options->struct_version != 1 && options->struct_version != 2))
	{
		rc = MQTTCLIENT_BAD_STRUCTURE;
		goto exit;
	}

	if (options->will) /* check validity of will options structure */
	{
		if (strncmp(options->will->struct_id, "MQTW", 4) != 0 || options->will->struct_version != 0)
		{
			rc = MQTTCLIENT_BAD_STRUCTURE;
			goto exit;
		}
	}
	
#if defined(OPENSSL)
	if (options->struct_version != 0 && options->ssl) /* check validity of SSL options structure */
	{
		if (strncmp(options->ssl->struct_id, "MQTS", 4) != 0 || options->ssl->struct_version != 0)
		{
			rc = MQTTCLIENT_BAD_STRUCTURE;
			goto exit;
		}
	}
#endif

	if ((options->username && !UTF8_validateString(options->username)) ||
		(options->password && !UTF8_validateString(options->password)))
	{
		rc = MQTTCLIENT_BAD_UTF8_STRING;
		goto exit;
	}

	if (options->struct_version < 2 || options->serverURIcount == 0)
		rc = MQTTClient_connectURI(handle, options, m->serverURI);
	else
	{
		int i;
		
		for (i = 0; i < options->serverURIcount; ++i)
		{
			char* serverURI = options->serverURIs[i];
			
			if (strncmp(URI_TCP, serverURI, strlen(URI_TCP)) == 0)
				serverURI += strlen(URI_TCP);
#if defined(OPENSSL)
			else if (strncmp(URI_SSL, serverURI, strlen(URI_SSL)) == 0)
			{
				serverURI += strlen(URI_SSL);
				m->ssl = 1;
			}
#endif
			if ((rc = MQTTClient_connectURI(handle, options, serverURI)) == MQTTCLIENT_SUCCESS)
				break;
		}	
	}

exit:
	if (m->c->will)
	{
		free(m->c->will);
		m->c->will = NULL;
	}
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_disconnect1(MQTTClient handle, int timeout, int internal)
{
	MQTTClients* m = handle;
	START_TIME_TYPE start;
	int rc = MQTTCLIENT_SUCCESS;
	int was_connected = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0 && m->c->connect_state == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}
	was_connected = m->c->connected; /* should be 1 */
	if (m->c->connected != 0)
	{
		start = MQTTClient_start_clock();
		m->c->connect_state = -2; /* indicate disconnecting */
		while (m->c->inboundMsgs->count > 0 || m->c->outboundMsgs->count > 0)
		{ /* wait for all inflight message flows to finish, up to timeout */
			if (MQTTClient_elapsed(start) >= timeout)
				break;
			Thread_unlock_mutex(mqttclient_mutex);
			MQTTClient_yield();
			Thread_lock_mutex(mqttclient_mutex);
		}
	}

	MQTTClient_closeSession(m->c);

	if (Thread_check_sem(m->connect_sem))
		Thread_post_sem(m->connect_sem);
	if (Thread_check_sem(m->connack_sem))
		Thread_post_sem(m->connack_sem);
	if (Thread_check_sem(m->suback_sem))
		Thread_post_sem(m->suback_sem);
	if (Thread_check_sem(m->unsuback_sem))
		Thread_post_sem(m->unsuback_sem);

exit:
	MQTTClient_stop();
	if (internal && m->cl && was_connected)
	{
		Log(TRACE_MIN, -1, "Calling connectionLost for client %s", m->c->clientID);
		Thread_start(connectionLost_call, m);
	}
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_disconnect_internal(MQTTClient handle, int timeout)
{
	return MQTTClient_disconnect1(handle, timeout, 1);
}


void MQTTProtocol_closeSession(Clients* c, int sendwill)
{
	MQTTClient_disconnect_internal((MQTTClient)c->context, 0);
}


int MQTTClient_disconnect(MQTTClient handle, int timeout)
{
	return MQTTClient_disconnect1(handle, timeout, 0);
}


int MQTTClient_isConnected(MQTTClient handle)
{
	MQTTClients* m = handle;
	int rc = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);
	if (m && m->c)
		rc = m->c->connected;
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_subscribeMany(MQTTClient handle, int count, char** topic, int* qos)
{
	MQTTClients* m = handle;
	List* topics = ListInitialize();
	List* qoss = ListInitialize();
	int i = 0;
	int rc = MQTTCLIENT_FAILURE;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}
	for (i = 0; i < count; i++)
	{
		if (!UTF8_validateString(topic[i]))
		{
			rc = MQTTCLIENT_BAD_UTF8_STRING;
			goto exit;
		}
		
		if(qos[i] < 0 || qos[i] > 2)
		{
			rc = MQTTCLIENT_BAD_QOS;
			goto exit;
		}
	}

	for (i = 0; i < count; i++)
	{
		ListAppend(topics, topic[i], strlen(topic[i]));
		ListAppend(qoss, &qos[i], sizeof(int));
	}
	rc = MQTTProtocol_subscribe(m->c, topics, qoss);
	ListFreeNoContent(topics);
	ListFreeNoContent(qoss);

	if (rc == TCPSOCKET_COMPLETE)
	{
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_waitfor(handle, SUBACK, &rc, 10000L);
		Thread_lock_mutex(mqttclient_mutex);
		if (pack != NULL)
		{
			rc = MQTTProtocol_handleSubacks(pack, m->c->net.socket);
			m->pack = NULL;
		}
		else
			rc = SOCKET_ERROR;
	}

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);
		Thread_lock_mutex(mqttclient_mutex);
	}
	else if (rc == TCPSOCKET_COMPLETE)
		rc = MQTTCLIENT_SUCCESS;

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

int MQTTClient_subscribe_many(MQTTClient handle, int count, char** topic)
{
	int rc = 0;
	int *qos = malloc(count * sizeof(int));
	memset(qos, DEFAULT_QOS, count);
	FUNC_ENTRY;
	rc = MQTTClient_subscribeMany(handle, count, topic, qos);
	FUNC_EXIT_RC(rc);
	free(qos);
	return rc;
}


int MQTTClient_subscribe(MQTTClient handle, char* topic)
{
	int rc = 0;
	int qos = DEFAULT_QOS;
	FUNC_ENTRY;
	rc = MQTTClient_subscribeMany(handle, 1, &topic, &qos);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_dosubscribe(MQTTClient handle, char* topic, int qos)
{
        int rc = 0;

        FUNC_ENTRY;
        rc = MQTTClient_subscribeMany(handle, 1, &topic, &qos);
        FUNC_EXIT_RC(rc);
        return rc;
}




int MQTTClient_presence(MQTTClient handle, char* topic)
{
	char temp[100];
	sprintf(temp, "%s/p", topic);
	return MQTTClient_subscribe(handle, temp);
}


int MQTTClient_unsubscribeMany(MQTTClient handle, int count, char** topic)
{
	MQTTClients* m = handle;
	List* topics = ListInitialize();
	int i = 0;
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}

	for (i = 0; i < count; i++)
	{
		if (!UTF8_validateString(topic[i]))
		{
			rc = MQTTCLIENT_BAD_UTF8_STRING;
			goto exit;
		}
	}

	for (i = 0; i < count; i++)
		ListAppend(topics, topic[i], strlen(topic[i]));
	rc = MQTTProtocol_unsubscribe(m->c, topics);
	ListFreeNoContent(topics);

	if (rc == TCPSOCKET_COMPLETE)
	{
		MQTTPacket* pack = NULL;

		Thread_unlock_mutex(mqttclient_mutex);
		pack = MQTTClient_waitfor(handle, UNSUBACK, &rc, 10000L);
		Thread_lock_mutex(mqttclient_mutex);
		if (pack != NULL)
		{
			rc = MQTTProtocol_handleUnsubacks(pack, m->c->net.socket);
			m->pack = NULL;
		}
		else
			rc = SOCKET_ERROR;
	}

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);
		Thread_lock_mutex(mqttclient_mutex);
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

int MQTTClient_unsubscribe_many(MQTTClient handle, int count, char** topic)
{
	return MQTTClient_unsubscribeMany(handle, count, topic);
}


int MQTTClient_unsubscribe(MQTTClient handle, char* topic)
{
	int rc = 0;

	FUNC_ENTRY;
	rc = MQTTClient_unsubscribeMany(handle, 1, &topic);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_unpresence(MQTTClient handle, char* topic)
{
	char temp[100];
	sprintf(temp, "%s/p", topic);
	return MQTTClient_unsubscribe(handle, temp);
}



int MQTTClient_dopublish(MQTTClient handle, char* topicName, int payloadlen, void* payload,
							 int qos, int retained, MQTTClient_deliveryToken* deliveryToken)
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;
	Messages* msg = NULL;
	Publish* p = NULL;
	int blocked = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
		rc = MQTTCLIENT_FAILURE;
	else if (m->c->connected == 0)
		rc = MQTTCLIENT_DISCONNECTED;
	else if (!UTF8_validateString(topicName))
		rc = MQTTCLIENT_BAD_UTF8_STRING;
	if (rc != MQTTCLIENT_SUCCESS)
		goto exit;

	/* If outbound queue is full, block until it is not */
	while (m->c->outboundMsgs->count >= m->c->maxInflightMessages || 
         Socket_noPendingWrites(m->c->net.socket) == 0) /* wait until the socket is free of large packets being written */
	{
		if (blocked == 0)
		{
			blocked = 1;
			Log(TRACE_MIN, -1, "Blocking publish on queue full for client %s", m->c->clientID);
		}
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_yield();
		Thread_lock_mutex(mqttclient_mutex);
		if (m->c->connected == 0)
		{
			rc = MQTTCLIENT_FAILURE;
			goto exit;
		}
	}
	if (blocked == 1)
		Log(TRACE_MIN, -1, "Resuming publish now queue not full for client %s", m->c->clientID);

	p = malloc(sizeof(Publish));

	p->payload = payload;
	p->payloadlen = payloadlen;
	p->topic = topicName;
	p->msgId = -1;

	rc = MQTTProtocol_startPublish(m->c, p, qos, retained, &msg);

	/* If the packet was partially written to the socket, wait for it to complete.
	 * However, if the client is disconnected during this time and qos is not 0, still return success, as
	 * the packet has already been written to persistence and assigned a message id so will
	 * be sent when the client next connects.
	 */
	if (rc == TCPSOCKET_INTERRUPTED)
	{
		while (m->c->connected == 1 && SocketBuffer_getWrite(m->c->net.socket))
		{
			Thread_unlock_mutex(mqttclient_mutex);
			MQTTClient_yield();
			Thread_lock_mutex(mqttclient_mutex);
		}
		rc = (qos > 0 || m->c->connected == 1) ? MQTTCLIENT_SUCCESS : MQTTCLIENT_FAILURE;
	}

	if (deliveryToken && qos > 0)
		*deliveryToken = msg->msgid;

	free(p);

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);
		Thread_lock_mutex(mqttclient_mutex);
		/* Return success for qos > 0 as the send will be retried automatically */
		rc = (qos > 0) ? MQTTCLIENT_SUCCESS : MQTTCLIENT_FAILURE;
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

int MQTTClient_publish(MQTTClient handle, char* topicName, int payloadlen, void* payload)
{
	int qos = DEFAULT_QOS;
	int retained = DEFAULT_RETAINED;
	return MQTTClient_dopublish(handle, topicName, payloadlen, payload, qos, retained, NULL);
}


int MQTTClient_publish_to_alias(MQTTClient handle, char* alias, int payloadlen, void* payload)
{
	int qos = DEFAULT_QOS;
	int retained = DEFAULT_RETAINED;
	char buf[150];

	sprintf(buf, ",yta/%s", alias);
	return MQTTClient_dopublish(handle, buf, payloadlen, payload, qos, retained, NULL);
}

int MQTTClient_publish_json(MQTTClient handle, char* topicName, cJSON *data)
{
	int ret = MQTTCLIENT_FAILURE;
	int qos = DEFAULT_QOS;
	int retained = DEFAULT_RETAINED;
	char *payload = cJSON_PrintUnformatted(data);

	if (payload != NULL) {
		int payloadlen = strlen(payload);
		ret = MQTTClient_dopublish(handle, topicName, payloadlen, payload, qos, retained, NULL);

		free(payload);
	}

	return ret;
}



int MQTTClient_publishMessage(MQTTClient handle, char* topicName, MQTTClient_message* message,
															 MQTTClient_deliveryToken* deliveryToken)
{
	int rc = MQTTCLIENT_SUCCESS;

	FUNC_ENTRY;
	if (message == NULL)
	{
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	if (strncmp(message->struct_id, "MQTM", 4) != 0 || message->struct_version != 0)
	{
		rc = MQTTCLIENT_BAD_STRUCTURE;
		goto exit;
	}

	rc = MQTTClient_dopublish(handle, topicName, message->payloadlen, message->payload,
								message->qos, message->retained, deliveryToken);
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}

int MQTTClient_get(MQTTClient handle, EXTED_CMD cmd, int parameter_len, void* parameter,
							 int qos, int retained, MQTTClient_deliveryToken* deliveryToken)
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;
	Messages* msg = NULL;
	Get* p = NULL;
	int blocked = 0;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
		rc = MQTTCLIENT_FAILURE;
	else if (m->c->connected == 0)
		rc = MQTTCLIENT_DISCONNECTED;
	if (rc != MQTTCLIENT_SUCCESS)
		goto exit;

	/* If outbound queue is full, block until it is not */
	while (m->c->outboundMsgs->count >= m->c->maxInflightMessages ||
         Socket_noPendingWrites(m->c->net.socket) == 0) /* wait until the socket is free of large packets being written */
	{
		if (blocked == 0)
		{
			blocked = 1;
			Log(TRACE_MIN, -1, "Blocking get on queue full for client %s", m->c->clientID);
		}
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_yield();
		Thread_lock_mutex(mqttclient_mutex);
		if (m->c->connected == 0)
		{
			rc = MQTTCLIENT_FAILURE;
			goto exit;
		}
	}
	if (blocked == 1)
		Log(TRACE_MIN, -1, "Resuming get now queue not full for client %s", m->c->clientID);

	p = malloc(sizeof(Get));

	p->ext_payload.ext_buf = parameter;
	p->ext_payload.ext_cmd = cmd;
	p->ext_payload.ext_buf_len = parameter_len;
	p->msgId = -1;

	rc = MQTTProtocol_startGet(m->c, p, qos, retained, &msg);

	/* If the packet was partially written to the socket, wait for it to complete.
	 * However, if the client is disconnected during this time and qos is not 0, still return success, as
	 * the packet has already been written to persistence and assigned a message id so will
	 * be sent when the client next connects.
	 */
	if (rc == TCPSOCKET_INTERRUPTED)
	{
		while (m->c->connected == 1 && SocketBuffer_getWrite(m->c->net.socket))
		{
			Thread_unlock_mutex(mqttclient_mutex);
			MQTTClient_yield();
			Thread_lock_mutex(mqttclient_mutex);
		}
		rc = (qos > 0 || m->c->connected == 1) ? MQTTCLIENT_SUCCESS : MQTTCLIENT_FAILURE;
	}

	if (deliveryToken && qos > 0)
		*deliveryToken = msg->msgid;

	free(p);

	if (rc == SOCKET_ERROR)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_disconnect_internal(handle, 0);
		Thread_lock_mutex(mqttclient_mutex);
		/* Return success for qos > 0 as the send will be retried automatically */
		rc = (qos > 0) ? MQTTCLIENT_SUCCESS : MQTTCLIENT_FAILURE;
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

DLLExport int MQTTClient_report(MQTTClient handle, char* action, char *data)
{
	char topic_name[100];
	sprintf(topic_name, "$$report/%s", action);

	return MQTTClient_dopublish(handle, topic_name, strlen(data), data, 0, DEFAULT_RETAINED, NULL);
}


int MQTTClient_set_alias(MQTTClient handle, char* alias)
{
	char *topic_name=",yali";
	return MQTTClient_dopublish(handle, topic_name, strlen(alias), alias, DEFAULT_QOS, DEFAULT_RETAINED, NULL);
}


int MQTTClient_get_aliaslist(MQTTClient handle, char* parameter)
{
#if 0
	const char *topic_name=",yaliget";
	return MQTTClient_publish(handle, topic_name, strlen(alias), alias, 0, 0, NULL);
#endif
	return MQTTClient_get(handle, GET_ALIAS_LIST, strlen(parameter), parameter, 1, 0, NULL);
}


int MQTTClient_get_alias(MQTTClient handle, char* parameter)
{
	return MQTTClient_get(handle, GET_ALIAS, strlen(parameter), parameter, 1, 0, NULL);
}

int MQTTClient_get_topic(MQTTClient handle, char* parameter)
{
	return MQTTClient_get(handle, GET_TOPIC, strlen(parameter), parameter, 1, 0, NULL);
}


int MQTTClient_get_status(MQTTClient handle, char* parameter)
{
	return MQTTClient_get(handle, GET_STATUS, strlen(parameter), parameter, 1, 0, NULL);
}

int MQTTClient_set_broker(MQTTClient *handle, char* broker)
{
	int rc = 0;
	MQTTClients* m = *handle;
	char *p = NULL;
	char buf[100];
	FUNC_ENTRY;
	rc = Thread_lock_mutex(mqttclient_mutex);

	if (broker == NULL) {
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	p = strtok(m->serverURI, ":");
	if (p != NULL) {
		p = strtok(NULL,":");
		if (p != NULL) {
			if (m->serverURI)
				free(m->serverURI);

			sprintf(buf, "%s:%s", broker, p);
			rc = MQTTCLIENT_SUCCESS;
			m->serverURI = malloc(strlen(buf)+1);
			strcpy(m->serverURI, buf);
		}
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

int MQTTClient_get_broker(MQTTClient *handle, char* broker)
{
	int rc = 0;
	MQTTClients* m = *handle;
	char *p = NULL;
	char buf[100];
	FUNC_ENTRY;
	rc = Thread_lock_mutex(mqttclient_mutex);

	strcpy(buf, m->serverURI);
	if (broker == NULL) {
		rc = MQTTCLIENT_NULL_PARAMETER;
		goto exit;
	}

	p = strtok(buf, ":");
	if (p != NULL) {
		strcpy(broker, p);
		rc = MQTTCLIENT_SUCCESS;
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

REG_info reg_info;

typedef int (*CALLBACK)(char *p);
int http_post_json(char *json_data, char *hostname, uint16_t port, char *path, CALLBACK cb) {
	int ret = -1;
	int sockfd, h;
	socklen_t len;
	fd_set   t_set1;
	struct sockaddr_in servaddr;
	char buf[4096];
	memset(buf, 0, sizeof(buf));

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    struct hostent *host_entry = gethostbyname(hostname);
	if(host_entry == NULL)
		return -1;
    char* p = inet_ntoa(*((struct in_addr *)host_entry->h_addr));
	if (inet_pton(AF_INET, p, &servaddr.sin_addr) <= 0)
		return -1;

	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		return -1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		return -1;

    //TODO: 超时处理.
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		return -1;

    char temp[128];
    sprintf(temp, "POST %s HTTP/1.1", path);
    strcat(buf, temp);
    strcat(buf, "\r\n");
    sprintf(temp, "Host: %s:%d", hostname, port),
    strcat(buf, temp);
    strcat(buf, "\r\n");
    strcat(buf, "Accept: application/json\r\n");
    strcat(buf, "Content-Type: application/json\r\n");
    strcat(buf, "Content-Length: ");
    sprintf(temp, "%d", strlen(json_data)),
    strcat(buf, temp);
    strcat(buf, "\n\n");
    strcat(buf, json_data);

    //TODO:　可能没写完？
    ret = write(sockfd, buf, strlen(buf));
	if (ret < 0) {
		close(sockfd);
		return -1;
	}

    struct timeval  tv;
    FD_ZERO(&t_set1);
    FD_SET(sockfd, &t_set1);
	tv.tv_sec= 6;
	tv.tv_usec= 0;
	h = select(sockfd + 1, &t_set1, NULL, NULL, &tv);
	if (h > 0) {
		memset(buf, 0, sizeof(buf));
		ssize_t  i= read(sockfd, buf, sizeof(buf));
		//取body
		char *temp = strstr(buf, "\r\n\r\n");
		if (temp) {
			temp += 4;
			ret = cb(temp);
		} else
			ret = -1;
	} else
		ret = -1;

    close(sockfd);
    return ret;
}

static int reg_cb(const char *json_data) {
	int ret = 0;
	char buf[500];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, json_data, strlen(json_data));

	cJSON *root = cJSON_Parse(buf);
	if (root) {
		int ret_size = cJSON_GetArraySize(root);
		if (ret_size >= 4) {
			strcpy(reg_info.client_id, cJSON_GetObjectItem(root,"c")->valuestring);
			strcpy(reg_info.username, cJSON_GetObjectItem(root,"u")->valuestring);
			strcpy(reg_info.password, cJSON_GetObjectItem(root,"p")->valuestring);
			strcpy(reg_info.device_id, cJSON_GetObjectItem(root,"d")->valuestring);
		} else
			ret = -1;

		cJSON_Delete(root);
	}
	return ret;
}

int MQTTClient_setup_with_appkey(char* appkey, REG_info *info)
{
	char json_data[1024];
	sprintf(json_data, "{\"a\": \"%s\", \"p\":4}", appkey);

	int ret = http_post_json(json_data, "reg.yunba.io", 8383, "/device/reg/", reg_cb);
	if (ret < 0)
		return -1;

	strcpy(info->client_id, reg_info.client_id);
	strcpy(info->username, reg_info.username);
	strcpy(info->password, reg_info.password);
	strcpy(info->device_id, reg_info.device_id);

	return 0;
}

int MQTTClient_setup_with_appkey_and_deviceid(char* appkey, char *deviceid, REG_info *info)
{
	char json_data[1024];

	if (appkey == NULL)
		return -1;

	if (deviceid == NULL)
		sprintf(json_data, "{\"a\": \"%s\", \"p\":4}", appkey);
	else
		sprintf(json_data, "{\"a\": \"%s\", \"p\":4, \"d\": \"%s\"}", appkey, deviceid);

	int ret = http_post_json(json_data, "reg.yunba.io", 8383, "/device/reg/", reg_cb);
	if (ret < 0)
		return -1;

	strcpy(info->client_id, reg_info.client_id);
	strcpy(info->username, reg_info.username);
	strcpy(info->password, reg_info.password);
	strcpy(info->device_id, reg_info.device_id);
	return 0;
}

static char url_host[200];
static size_t get_broker_cb(const char *json_data)
{
	int ret = 0;
	char buf[500];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, json_data, strlen(json_data));
	cJSON *root = cJSON_Parse(buf);
	if (root) {
		int ret_size = cJSON_GetArraySize(root);
		if (ret_size >= 1)
			strcpy(url_host, cJSON_GetObjectItem(root,"c")->valuestring);
		else
			ret = -1;
		cJSON_Delete(root);
	}
	return ret;
}

int MQTTClient_get_host(char *appkey, char* url)
{
	int ret = -1;
	char json_data[1024];

	sprintf(json_data, "{\"a\":\"%s\",\"n\":\"%s\",\"v\":\"%s\",\"o\":\"%s\"}",
                        appkey, /*${networktype}*/"1", "v1.0.0", /*${NetworkOperator}*/"1");

	ret = http_post_json(json_data, "tick.yunba.io", 9999, "/", get_broker_cb);
	if (ret < 0)
		return -1;

	strcpy(url, url_host);
	return 0;
}

void MQTTClient_retry(void)
{
	time_t now;

	FUNC_ENTRY;
	time(&(now));
	if (difftime(now, last) > 5)
	{
		time(&(last));
		MQTTProtocol_keepalive(now);
		MQTTProtocol_retry(now, 1);
	}
	else
		MQTTProtocol_retry(now, 0);
	FUNC_EXIT;
}

MQTTPacket* MQTTClient_cycle(int* sock, unsigned long timeout, int* rc)
{
	struct timeval tp = {0L, 0L};
	static Ack ack;
	MQTTPacket* pack = NULL;

	FUNC_ENTRY;
	if (timeout > 0L)
	{
		tp.tv_sec = timeout / 1000;
		tp.tv_usec = (timeout % 1000) * 1000; /* this field is microseconds! */
	}

#if defined(OPENSSL)
	if ((*sock = SSLSocket_getPendingRead()) == -1)
	{
		/* 0 from getReadySocket indicates no work to do, -1 == error, but can happen normally */
#endif
		*sock = Socket_getReadySocket(0, &tp);
#if defined(OPENSSL)
	}
#endif
	Thread_lock_mutex(mqttclient_mutex);
	if (*sock > 0)
	{
		MQTTClients* m = NULL;
		if (ListFindItem(handles, sock, clientSockCompare) != NULL)
			m = (MQTTClient)(handles->current->content);
		if (m != NULL)
		{
			if (m->c->connect_state == 1 || m->c->connect_state == 2)
				*rc = 0;  /* waiting for connect state to clear */
			else
			{
				pack = MQTTPacket_Factory(&m->c->net, rc);
				if (*rc == TCPSOCKET_INTERRUPTED)
					*rc = 0;
			}
		}
		if (pack)
		{
			int freed = 1;
			/* Note that these handle... functions free the packet structure that they are dealing with */
			if (pack->header.bits.type == PUBLISH)
				*rc = MQTTProtocol_handlePublishes(pack, *sock);
			else if (pack->header.bits.type == GET) {
				Getack get_ack = *(Getack*)pack;
				*rc = MQTTProtocol_handleGets(pack, *sock);
				if (m && m->eca)
				{
					Log(TRACE_MIN, -1, "Calling extendedCommand ack for client %s", m->c->clientID);
					(*(m->eca))(m->context, get_ack.ack_payload.ext_cmd, get_ack.ack_payload.status,
							get_ack.ack_payload.len, get_ack.ack_payload.ret_string);
				}
			}
			else if (pack->header.bits.type == PUBACK || pack->header.bits.type == PUBCOMP)
			{
				uint64_t msgid;

				ack = (pack->header.bits.type == PUBCOMP) ? *(Pubcomp*)pack : *(Puback*)pack;
				msgid = ack.msgId;
				*rc = (pack->header.bits.type == PUBCOMP) ?
						MQTTProtocol_handlePubcomps(pack, *sock) : MQTTProtocol_handlePubacks(pack, *sock);
				if (m && m->dc)
				{
					Log(TRACE_MIN, -1, "Calling deliveryComplete for client %s, msgid %d", m->c->clientID, msgid);
					(*(m->dc))(m->context, msgid);
				}
			}
			else if (pack->header.bits.type == PUBREC)
				*rc = MQTTProtocol_handlePubrecs(pack, *sock);
			else if (pack->header.bits.type == PUBREL)
				*rc = MQTTProtocol_handlePubrels(pack, *sock);
			else if (pack->header.bits.type == PINGRESP)
				*rc = MQTTProtocol_handlePingresps(pack, *sock);
			else
				freed = 0;
			if (freed)
				pack = NULL;
		}
	}
	MQTTClient_retry();
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(*rc);
	return pack;
}


MQTTPacket* MQTTClient_waitfor(MQTTClient handle, int packet_type, int* rc, long timeout)
{
	MQTTPacket* pack = NULL;
	MQTTClients* m = handle;
	START_TIME_TYPE start = MQTTClient_start_clock();

	FUNC_ENTRY;
	if (((MQTTClients*)handle) == NULL)
	{
		*rc = MQTTCLIENT_FAILURE;
		goto exit;
	}

	if (running)
	{
		if (packet_type == CONNECT)
		{
			if ((*rc = Thread_wait_sem(m->connect_sem, timeout)) == 0)
				*rc = m->rc;
		}
		else if (packet_type == CONNACK)
			*rc = Thread_wait_sem(m->connack_sem, timeout);
		else if (packet_type == SUBACK)
			*rc = Thread_wait_sem(m->suback_sem, timeout);
		else if (packet_type == UNSUBACK)
			*rc = Thread_wait_sem(m->unsuback_sem, timeout);
		if (*rc == 0 && packet_type != CONNECT && m->pack == NULL)
			Log(TRACE_MIN, -1, "waitfor unexpectedly is NULL for client %s, packet_type %d", m->c->clientID, packet_type);
		pack = m->pack;
	}
	else
	{
		*rc = TCPSOCKET_COMPLETE;
		while (1)
		{
			int sock = -1;
			pack = MQTTClient_cycle(&sock, 100L, rc);
			if (sock == m->c->net.socket)
			{
				if (pack && (pack->header.bits.type == packet_type))
					break;
				if (m->c->connect_state == 1)
				{
					int error;
					socklen_t len = sizeof(error);

					if ((*rc = getsockopt(m->c->net.socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len)) == 0)
						*rc = error;
					break;
				}
#if defined(OPENSSL)
				else if (m->c->connect_state == 2)
				{
					*rc = SSLSocket_connect(m->c->net.ssl, sock);
					if (*rc == SSL_FATAL)
						break;
					else if (*rc == 1) /* rc == 1 means SSL connect has finished and succeeded */
					{
						if (!m->c->cleansession && m->c->session == NULL)
							m->c->session = SSL_get1_session(m->c->net.ssl);
						break;
					}
				}
#endif
				else if (m->c->connect_state == 3)
				{
					int error;
					socklen_t len = sizeof(error);

					if (getsockopt(m->c->net.socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0)
					{
						if (error)
						{
							*rc = error;
							break;
						}
					}
				}
			}
			if (MQTTClient_elapsed(start) > timeout)
			{
				pack = NULL;
				break;
			}
		}
	}

exit:
	FUNC_EXIT_RC(*rc);
	return pack;
}


int MQTTClient_receive(MQTTClient handle, char** topicName, int* topicLen, MQTTClient_message** message,
											 unsigned long timeout)
{
	int rc = TCPSOCKET_COMPLETE;
	START_TIME_TYPE start = MQTTClient_start_clock();
	unsigned long elapsed = 0L;
	MQTTClients* m = handle;

	FUNC_ENTRY;
	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}

	*topicName = NULL;
	*message = NULL;

	/* if there is already a message waiting, don't hang around but still do some packet handling */
	if (m->c->messageQueue->count > 0)
		timeout = 0L;

	elapsed = MQTTClient_elapsed(start);
	do
	{
		int sock = 0;
		MQTTClient_cycle(&sock, (timeout > elapsed) ? timeout - elapsed : 0L, &rc);
		
		if (rc == SOCKET_ERROR)
		{
			if (ListFindItem(handles, &sock, clientSockCompare) && 	/* find client corresponding to socket */
			  (MQTTClient)(handles->current->content) == handle)
				break; /* there was an error on the socket we are interested in */
		}
		elapsed = MQTTClient_elapsed(start);
	}
	while (elapsed < timeout && m->c->messageQueue->count == 0);

	if (m->c->messageQueue->count > 0)
		rc = MQTTClient_deliverMessage(rc, m, topicName, topicLen, message);

	if (rc == SOCKET_ERROR)
		MQTTClient_disconnect_internal(handle, 0);

exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


bool endswith(const char* str, const char* tail)
{
  const char* foo = strstr (str, tail);
  if (foo)
  {
     const int strlength = strlen (str);
     const int taillength = strlen (tail);
     return foo == (str + strlength - taillength);
  }
  return 0;
}

int get_present_info(char *topicName, MQTTClient_message* m, Presence_msg *presence_status)
{
	if (endswith(topicName, "/p") != 0) {
		cJSON *root = cJSON_Parse((char *)m->payload);
		if (root != NULL) {
			strcpy(presence_status->action, cJSON_GetObjectItem(root,"action")->valuestring);
			strcpy(presence_status->alias, cJSON_GetObjectItem(root,"alias")->valuestring);
			cJSON_Delete(root);
			return 0;
		}
	}
	return -1;
}



/**/


void MQTTClient_yield(void)
{
	START_TIME_TYPE start = MQTTClient_start_clock();
	unsigned long elapsed = 0L;
	unsigned long timeout = 100L;
	int rc = 0;

	FUNC_ENTRY;
	if (running)
	{
		MQTTClient_sleep(timeout);
		goto exit;
	}

	elapsed = MQTTClient_elapsed(start);
	do
	{
		int sock = -1;
		MQTTClient_cycle(&sock, (timeout > elapsed) ? timeout - elapsed : 0L, &rc);
		if (rc == SOCKET_ERROR && ListFindItem(handles, &sock, clientSockCompare))
		{
			MQTTClients* m = (MQTTClient)(handles->current->content);
			if (m->c->connect_state != -2)
				MQTTClient_disconnect_internal(m, 0);
		}
		elapsed = MQTTClient_elapsed(start);
	}
	while (elapsed < timeout);
exit:
	FUNC_EXIT;
}


int pubCompare(void* a, void* b)
{
	Messages* msg = (Messages*)a;
	return msg->publish == (Publications*)b;
}


int MQTTClient_waitForCompletion(MQTTClient handle, MQTTClient_deliveryToken mdt, unsigned long timeout)
{
	int rc = MQTTCLIENT_FAILURE;
	START_TIME_TYPE start = MQTTClient_start_clock();
	unsigned long elapsed = 0L;
	MQTTClients* m = handle;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL || m->c == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}
	if (m->c->connected == 0)
	{
		rc = MQTTCLIENT_DISCONNECTED;
		goto exit;
	}

	if (ListFindItem(m->c->outboundMsgs, &mdt, messageIDCompare) == NULL)
	{
		rc = MQTTCLIENT_SUCCESS; /* well we couldn't find it */
		goto exit;
	}

	elapsed = MQTTClient_elapsed(start);
	while (elapsed < timeout)
	{
		Thread_unlock_mutex(mqttclient_mutex);
		MQTTClient_yield();
		Thread_lock_mutex(mqttclient_mutex);
		if (ListFindItem(m->c->outboundMsgs, &mdt, messageIDCompare) == NULL)
		{
			rc = MQTTCLIENT_SUCCESS; /* well we couldn't find it */
			goto exit;
		}
		elapsed = MQTTClient_elapsed(start);
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTClient_getPendingDeliveryTokens(MQTTClient handle, MQTTClient_deliveryToken **tokens)
{
	int rc = MQTTCLIENT_SUCCESS;
	MQTTClients* m = handle;
	*tokens = NULL;

	FUNC_ENTRY;
	Thread_lock_mutex(mqttclient_mutex);

	if (m == NULL)
	{
		rc = MQTTCLIENT_FAILURE;
		goto exit;
	}

	if (m->c && m->c->outboundMsgs->count > 0)
	{
		ListElement* current = NULL;
		int count = 0;

		*tokens = malloc(sizeof(MQTTClient_deliveryToken) * (m->c->outboundMsgs->count + 1));
		/*Heap_unlink(__FILE__, __LINE__, *tokens);*/
		while (ListNextElement(m->c->outboundMsgs, &current))
		{
			Messages* m = (Messages*)(current->content);
			(*tokens)[count++] = m->msgid;
		}
		(*tokens)[count] = -1;
	}

exit:
	Thread_unlock_mutex(mqttclient_mutex);
	FUNC_EXIT_RC(rc);
	return rc;
}

MQTTClient_nameValue* MQTTClient_getVersionInfo()
{
#if 0
	#define MAX_INFO_STRINGS 8
	static MQTTClient_nameValue libinfo[MAX_INFO_STRINGS + 1];
	int i = 0;

	libinfo[i].name = "Product name";
	libinfo[i++].value = "Paho Synchronous MQTT C Client Library";

	libinfo[i].name = "Version";
	libinfo[i++].value = CLIENT_VERSION;

	libinfo[i].name = "Build level";
	libinfo[i++].value = BUILD_TIMESTAMP;
#if defined(OPENSSL)
	libinfo[i].name = "OpenSSL version";
	libinfo[i++].value = SSLeay_version(SSLEAY_VERSION);

	libinfo[i].name = "OpenSSL flags";
	libinfo[i++].value = SSLeay_version(SSLEAY_CFLAGS);

	libinfo[i].name = "OpenSSL build timestamp";
	libinfo[i++].value = SSLeay_version(SSLEAY_BUILT_ON);

	libinfo[i].name = "OpenSSL platform";
	libinfo[i++].value = SSLeay_version(SSLEAY_PLATFORM);

	libinfo[i].name = "OpenSSL directory";
	libinfo[i++].value = SSLeay_version(SSLEAY_DIR);
#endif
	libinfo[i].name = NULL;
	libinfo[i].value = NULL;
	return libinfo;
#else
	static MQTTClient_nameValue libinfo;

	libinfo.name = "Yunba SDK";
	libinfo.value = "v1.0.3";
	return &libinfo;

#endif
}


/**
 * See if any pending writes have been completed, and cleanup if so.
 * Cleaning up means removing any publication data that was stored because the write did
 * not originally complete.
 */
void MQTTProtocol_checkPendingWrites()
{
	FUNC_ENTRY;
	if (state.pending_writes.count > 0)
	{
		ListElement* le = state.pending_writes.first;
		while (le)
		{
			if (Socket_noPendingWrites(((pending_write*)(le->content))->socket))
			{
				MQTTProtocol_removePublication(((pending_write*)(le->content))->p);
				state.pending_writes.current = le;
				ListRemove(&(state.pending_writes), le->content); /* does NextElement itself */
				le = state.pending_writes.current;
			}
			else
				ListNextElement(&(state.pending_writes), &le);
		}
	}
	FUNC_EXIT;
}


void MQTTClient_writeComplete(int socket)				
{
	ListElement* found = NULL;
	
	FUNC_ENTRY;
	/* a partial write is now complete for a socket - this will be on a publish*/
	
	MQTTProtocol_checkPendingWrites();
	
	/* find the client using this socket */
	if ((found = ListFindItem(handles, &socket, clientSockCompare)) != NULL)
	{
		MQTTClients* m = (MQTTClients*)(found->content);
		
		time(&(m->c->net.lastContact));			
	}
	FUNC_EXIT;
}
