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
 *    Ian Craggs, Allan Stockdill-Mander - SSL updates
 *    Ian Craggs - MQTT 3.1.1 support
 *******************************************************************************/

#if !defined(MQTTPACKET_H)
#define MQTTPACKET_H

#include "Socket.h"
#if defined(OPENSSL)
#include "SSLSocket.h"
#endif
#include "LinkedList.h"
#include "Clients.h"

/*BE
include "Socket"
include "LinkedList"
include "Clients"
BE*/

typedef unsigned int bool;
typedef void* (*pf)(unsigned char, char*, int);

#define BAD_MQTT_PACKET -4

enum msgTypes
{
	CONNECT = 1, CONNACK, PUBLISH, PUBACK, PUBREC, PUBREL,
	PUBCOMP, SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK,
	PINGREQ, PINGRESP, DISCONNECT,GET
};


/**
 * Bitfields for the MQTT header byte.
 */
typedef union
{
	/*unsigned*/ char byte;	/**< the whole byte */
#if defined(REVERSED)
	struct
	{
		unsigned int type : 4;	/**< message type nibble */
		bool dup : 1;			/**< DUP flag bit */
		unsigned int qos : 2;	/**< QoS value, 0, 1 or 2 */
		bool retain : 1;		/**< retained flag bit */
	} bits;
#else
	struct
	{
		bool retain : 1;		/**< retained flag bit */
		unsigned int qos : 2;	/**< QoS value, 0, 1 or 2 */
		bool dup : 1;			/**< DUP flag bit */
		unsigned int type : 4;	/**< message type nibble */
	} bits;
#endif
} Header;


/**
 * Data for a connect packet.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	union
	{
		unsigned char all;	/**< all connect flags */
#if defined(REVERSED)
		struct
		{
			bool username : 1;			/**< 3.1 user name */
			bool password : 1; 			/**< 3.1 password */
			bool willRetain : 1;		/**< will retain setting */
			unsigned int willQoS : 2;	/**< will QoS value */
			bool will : 1;			/**< will flag */
			bool cleanstart : 1;	/**< cleansession flag */
			int : 1;	/**< unused */
		} bits;
#else
		struct
		{
			int : 1;	/**< unused */
			bool cleanstart : 1;	/**< cleansession flag */
			bool will : 1;			/**< will flag */
			unsigned int willQoS : 2;	/**< will QoS value */
			bool willRetain : 1;		/**< will retain setting */
			bool password : 1; 			/**< 3.1 password */
			bool username : 1;			/**< 3.1 user name */
		} bits;
#endif
	} flags;	/**< connect flags byte */

	char *Protocol, /**< MQTT protocol name */
		*clientID,	/**< string client id */
        *willTopic,	/**< will topic */
        *willMsg;	/**< will payload */

	int keepAliveTimer;		/**< keepalive timeout value in seconds */
	unsigned char version;	/**< MQTT version number */
} Connect;


/**
 * Data for a connack packet.
 */
typedef struct
{
	Header header; /**< MQTT header byte */
	union
	{
		unsigned char all;	/**< all connack flags */
#if defined(REVERSED)
		struct
		{
			unsigned int reserved : 7;	/**< message type nibble */
			bool sessionPresent : 1;    /**< was a session found on the server? */
		} bits;
#else
		struct
		{
			bool sessionPresent : 1;    /**< was a session found on the server? */
			unsigned int reserved : 7;	/**< message type nibble */
		} bits;
#endif
	} flags;	 /**< connack flags byte */
	char rc; /**< connack return code */
} Connack;


/**
 * Data for a packet with header only.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
} MQTTPacket;


/**
 * Data for a subscribe packet.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	uint64_t msgId;		/**< MQTT message id */
	List* topics;	/**< list of topic strings */
	List* qoss;		/**< list of corresponding QoSs */
	int noTopics;	/**< topic and qos count */
} Subscribe;


/**
 * Data for a suback packet.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	uint64_t msgId;		/**< MQTT message id */
	List* qoss;		/**< list of granted QoSs */
} Suback;


/**
 * Data for an unsubscribe packet.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	uint64_t msgId;		/**< MQTT message id */
	List* topics;	/**< list of topic strings */
	int noTopics;	/**< topic count */
} Unsubscribe;


/**
 * Data for a publish packet.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	char* topic;	/**< topic string */
	int topiclen;
	uint64_t msgId;		/**< MQTT message id */
	char* payload;	/**< binary payload, length delimited */
	int payloadlen;	/**< payload length */
} Publish;


/**
 * Data for a payload in get-packet.
 */
typedef struct {
	uint8_t ext_cmd; /* extended command */
	uint8_t ext_buf_len; /* the length of parameters*/
	uint8_t* ext_buf;  /* the parameters */
} Ext_payload;


/**
 * Data for a get packet.
 */
typedef struct {
	Header header;	/**< MQTT header byte */
	uint64_t msgId;		/**< MQTT message id */
	Ext_payload ext_payload;
	int ext_payloadlen;	/**< payload length */
} Get;


/**
 * Data for.
 */
typedef struct {
	uint8_t ext_cmd;
	uint8_t status;
	uint8_t len;
	char *ret_string;
} Ext_ack_payload;


/**
 * Data for.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	uint64_t msgId;		/**< MQTT message id */
	Ext_ack_payload ack_payload;
} Ext_ack;



/**
 * Data for one of the ack packets.
 */
typedef struct
{
	Header header;	/**< MQTT header byte */
	uint64_t msgId;		/**< MQTT message id */
} Ack;

typedef Ack Puback;
typedef Ack Pubrec;
typedef Ack Pubrel;
typedef Ack Pubcomp;
typedef Ack Unsuback;
typedef Ext_ack Getack;

int MQTTPacket_encode(char* buf, int length);
int MQTTPacket_decode(networkHandles* net, int* value);
int readInt(char** pptr);
uint64_t readInt64(char** pptr);
char* readUTF(char** pptr, char* enddata);
unsigned char readChar(char** pptr);
void writeChar(char** pptr, char c);
void writeInt(char** pptr, int anInt);
void writeInt64(char** pptr, uint64_t anInt);
void writeUTF(char** pptr, char* string);

char* MQTTPacket_name(int ptype);

void* MQTTPacket_Factory(networkHandles* net, int* error);
int MQTTPacket_send(networkHandles* net, Header header, char* buffer, int buflen);
int MQTTPacket_sends(networkHandles* net, Header header, int count, char** buffers, int* buflens);

void* MQTTPacket_header_only(unsigned char aHeader, char* data, int datalen);
int MQTTPacket_send_disconnect(networkHandles* net, char* clientID);

void* MQTTPacket_get(unsigned char aHeader, char* data, int datalen);
void MQTTPacket_freeGet(Getack* pack);
void* MQTTPacket_publish(unsigned char aHeader, char* data, int datalen);
void MQTTPacket_freePublish(Publish* pack);
int MQTTPacket_send_get(Get* pack, int dup, int qos, int retained, networkHandles* net, char* clientID);
int MQTTPacket_send_publish(Publish* pack, int dup, int qos, int retained, networkHandles* net, char* clientID);
int MQTTPacket_send_puback(uint64_t msgid, networkHandles* net, char* clientID);
void* MQTTPacket_ack(unsigned char aHeader, char* data, int datalen);

void MQTTPacket_freeSuback(Suback* pack);
int MQTTPacket_send_pubrec(uint64_t msgid, networkHandles* net, char* clientID);
int MQTTPacket_send_pubrel(uint64_t msgid, int dup, networkHandles* net, char* clientID);
int MQTTPacket_send_pubcomp(uint64_t msgid, networkHandles* net, char* clientID);

void MQTTPacket_free_packet(MQTTPacket* pack);

#if !defined(NO_BRIDGE)
	#include "MQTTPacketOut.h"
#endif

#endif /* MQTTPACKET_H */
