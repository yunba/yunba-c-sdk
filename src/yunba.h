#if !defined(MQTTCLIENT_H)
#define MQTTCLIENT_H

#if defined(WIN32) || defined(WIN64)
  #define DLLImport __declspec(dllimport)
  #define DLLExport __declspec(dllexport)
#else
  #define DLLImport extern
  #define DLLExport __attribute__ ((visibility ("default")))
#endif

#include <stdio.h>
#include <inttypes.h>
#include "cJSON.h"

/// @endcond


typedef enum {
	GET_ALIAS =1,
	GET_ALIAS_ACK,
	GET_TOPIC,
	GET_TOPIC_ACK,
	GET_ALIAS_LIST,
	GET_ALIAS_LIST_ACK,
	GET_STATUS = 9,
	GET_STATUS_ACK
} EXTED_CMD;

/**
 * Return code: No error. Indicates successful completion of an MQTT client
 * operation.
 */
#define MQTTCLIENT_SUCCESS 0
/**
 * Return code: A generic error code indicating the failure of an MQTT client
 * operation.
 */
#define MQTTCLIENT_FAILURE -1

/* error code -2 is MQTTCLIENT_PERSISTENCE_ERROR */

/**
 * Return code: The client is disconnected.
 */
#define MQTTCLIENT_DISCONNECTED -3
/**
 * Return code: The maximum number of messages allowed to be simultaneously 
 * in-flight has been reached.
 */
#define MQTTCLIENT_MAX_MESSAGES_INFLIGHT -4
/**
 * Return code: An invalid UTF-8 string has been detected.
 */
#define MQTTCLIENT_BAD_UTF8_STRING -5
/**
 * Return code: A NULL parameter has been supplied when this is invalid.
 */
#define MQTTCLIENT_NULL_PARAMETER -6
/**
 * Return code: The topic has been truncated (the topic string includes
 * embedded NULL characters). String functions will not access the full topic.
 * Use the topic length value to access the full topic.
 */
#define MQTTCLIENT_TOPICNAME_TRUNCATED -7
/**
 * Return code: A structure parameter does not have the correct eyecatcher
 * and version number.
 */
#define MQTTCLIENT_BAD_STRUCTURE -8
/**
 * Return code: A QoS value that falls outside of the acceptable range (0,1,2)
 */
#define MQTTCLIENT_BAD_QOS -9



#define MQTTCLIENT_PERSISTENCE_DEFAULT 0
/**
  * This <i>persistence_type</i> value specifies a memory-based 
  * persistence mechanism (see MQTTClient_create()).
  */
#define MQTTCLIENT_PERSISTENCE_NONE 1
/**
  * This <i>persistence_type</i> value specifies an application-specific 
  * persistence mechanism (see MQTTClient_create()).
  */
#define MQTTCLIENT_PERSISTENCE_USER 2

/** 
  * Application-specific persistence functions must return this error code if 
  * there is a problem executing the function. 
  */
#define MQTTCLIENT_PERSISTENCE_ERROR -2



typedef struct {
	char *action;
	char *alias;
	uint32_t timestamp;
	uint16_t occupancy_num;
} Presence_msg;


/**
 * A handle representing an MQTT client. A valid client handle is available
 * following a successful call to MQTTClient_create().
 */
typedef void* MQTTClient;
/**
 * A value representing an MQTT message. A delivery token is returned to the
 * client application when a message is published. The token can then be used to
 * check that the message was successfully delivered to its destination (see
 * MQTTClient_publish(), 
 * MQTTClient_publishMessage(), 
 * MQTTClient_deliveryComplete(), 
 * MQTTClient_waitForCompletion() and
 * MQTTClient_getPendingDeliveryTokens()).
 */
typedef uint64_t MQTTClient_deliveryToken;
typedef int MQTTClient_token;

/**
 * A structure representing the payload and attributes of an MQTT message. The
 * message topic is not part of this structure (see MQTTClient_publishMessage(),
 * MQTTClient_publish(), MQTTClient_receive(), MQTTClient_freeMessage()
 * and MQTTClient_messageArrived()).
 */
typedef struct
{
	/** The eyecatcher for this structure.  must be MQTM. */
	char struct_id[4];
	/** The version number of this structure.  Must be 0 */
	int struct_version;
	/** The length of the MQTT message payload in bytes. */
	int payloadlen;
	/** A pointer to the payload of the MQTT message. */
	void* payload;
	/** 
     * The quality of service (QoS) assigned to the message. 
     * There are three levels of QoS:
     * <DL>
     * <DT><B>QoS0</B></DT>
     * <DD>Fire and forget - the message may not be delivered</DD>
     * <DT><B>QoS1</B></DT>
     * <DD>At least once - the message will be delivered, but may be 
     * delivered more than once in some circumstances.</DD>
     * <DT><B>QoS2</B></DT>
     * <DD>Once and one only - the message will be delivered exactly once.</DD>
     * </DL>
     */
	int qos;
	/** 
     * The retained flag serves two purposes depending on whether the message
     * it is associated with is being published or received. 
     * 
     * <b>retained = true</b><br>
     * For messages being published, a true setting indicates that the MQTT 
     * server should retain a copy of the message. The message will then be 
     * transmitted to new subscribers to a topic that matches the message topic.
     * For subscribers registering a new subscription, the flag being true
     * indicates that the received message is not a new one, but one that has
     * been retained by the MQTT server.
     *
     * <b>retained = false</b> <br>
     * For publishers, this ndicates that this message should not be retained 
     * by the MQTT server. For subscribers, a false setting indicates this is 
     * a normal message, received as a result of it being published to the 
     * server.
     */
	int retained;
	/** 
      * The dup flag indicates whether or not this message is a duplicate. 
      * It is only meaningful when receiving QoS1 messages. When true, the
      * client application should take appropriate action to deal with the
      * duplicate message.
      */
	int dup;
	/** The message identifier is normally reserved for internal use by the
      * MQTT client and server. 
      */
	uint64_t msgid;
} MQTTClient_message;

#define MQTTClient_message_initializer { {'M', 'Q', 'T', 'M'}, 0, 0, NULL, 0, 0, 0, 0 }

/**
 * This is a callback function. The client application
 * must provide an implementation of this function to enable asynchronous 
 * receipt of messages. The function is registered with the client library by
 * passing it as an argument to MQTTClient_setCallbacks(). It is
 * called by the client library when a new message that matches a client
 * subscription has been received from the server. This function is executed on
 * a separate thread to the one on which the client application is running.
 * @param context A pointer to the <i>context</i> value originally passed to 
 * MQTTClient_setCallbacks(), which contains any application-specific context.
 * @param topicName The topic associated with the received message.
 * @param topicLen The length of the topic if there are one
 * more NULL characters embedded in <i>topicName</i>, otherwise <i>topicLen</i>
 * is 0. If <i>topicLen</i> is 0, the value returned by <i>strlen(topicName)</i>
 * can be trusted. If <i>topicLen</i> is greater than 0, the full topic name
 * can be retrieved by accessing <i>topicName</i> as a byte array of length 
 * <i>topicLen</i>. 
 * @param message The MQTTClient_message structure for the received message. 
 * This structure contains the message payload and attributes.
 * @return This function must return a boolean value indicating whether or not
 * the message has been safely received by the client application. Returning 
 * true indicates that the message has been successfully handled.  
 * Returning false indicates that there was a problem. In this 
 * case, the client library will reinvoke MQTTClient_messageArrived() to 
 * attempt to deliver the message to the application again.
 */
typedef int MQTTClient_messageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message);

typedef int MQTTClient_extendedCmdArrive(void *context, EXTED_CMD cmd, int status, int ret_string_len, char *ret_string);

/**
 * This is a callback function. The client application
 * must provide an implementation of this function to enable asynchronous 
 * notification of delivery of messages. The function is registered with the 
 * client library by passing it as an argument to MQTTClient_setCallbacks(). 
 * It is called by the client library after the client application has 
 * published a message to the server. It indicates that the necessary 
 * handshaking and acknowledgements for the requested quality of service (see 
 * MQTTClient_message.qos) have been completed. This function is executed on a
 * separate thread to the one on which the client application is running.
 * <b>Note:</b>MQTTClient_deliveryComplete() is not called when messages are 
 * published at QoS0.
 * @param context A pointer to the <i>context</i> value originally passed to 
 * MQTTClient_setCallbacks(), which contains any application-specific context.
 * @param dt The ::MQTTClient_deliveryToken associated with
 * the published message. Applications can check that all messages have been 
 * correctly published by matching the delivery tokens returned from calls to
 * MQTTClient_publish() and MQTTClient_publishMessage() with the tokens passed
 * to this callback. 
 */
typedef void MQTTClient_deliveryComplete(void* context, MQTTClient_deliveryToken dt);

/**
 * This is a callback function. The client application
 * must provide an implementation of this function to enable asynchronous 
 * notification of the loss of connection to the server. The function is 
 * registered with the client library by passing it as an argument to  
 * MQTTClient_setCallbacks(). It is called by the client library if the client
 * loses its connection to the server. The client application must take 
 * appropriate action, such as trying to reconnect or reporting the problem. 
 * This function is executed on a separate thread to the one on which the 
 * client application is running.
 * @param context A pointer to the <i>context</i> value originally passed to 
 * MQTTClient_setCallbacks(), which contains any application-specific context.
 * @param cause The reason for the disconnection.
 * Currently, <i>cause</i> is always set to NULL.
 */
typedef void MQTTClient_connectionLost(void* context, char* cause);

/**
 * This function sets the callback functions for a specific client.
 * If your client application doesn't use a particular callback, set the 
 * relevant parameter to NULL. Calling MQTTClient_setCallbacks() puts the
 * client into multi-threaded mode. Any necessary message acknowledgements and
 * status communications are handled in the background without any intervention
 * from the client application. See @ref async for more information.
 * 
 * <b>Note:</b> The MQTT client must be disconnected when this function is 
 * called. 
 * @param handle A valid client handle from a successful call to
 * MQTTClient_create(). 
 * @param context A pointer to any application-specific context. The
 * the <i>context</i> pointer is passed to each of the callback functions to
 * provide access to the context information in the callback.
 * @param cl A pointer to an MQTTClient_connectionLost() callback
 * function. You can set this to NULL if your application doesn't handle
 * disconnections.
 * @param ma A pointer to an MQTTClient_messageArrived() callback
 * function. This callback function must be specified when you call
 * MQTTClient_setCallbacks().
 * @param dc A pointer to an MQTTClient_deliveryComplete() callback
 * function. You can set this to NULL if your application publishes 
 * synchronously or if you do not want to check for successful delivery.
 * @return ::MQTTCLIENT_SUCCESS if the callbacks were correctly set,
 * ::MQTTCLIENT_FAILURE if an error occurred.
 */
DLLExport int MQTTClient_setCallbacks(MQTTClient handle, void* context, MQTTClient_connectionLost* cl,
									MQTTClient_messageArrived* ma, MQTTClient_deliveryComplete* dc,
									MQTTClient_extendedCmdArrive *eca);
		

/**
 * This function creates an MQTT client ready for connection to the 
 * specified server and using the specified persistent storage (see 
 * MQTTClient_persistence). See also MQTTClient_destroy().
 * @param handle A pointer to an ::MQTTClient handle. The handle is
 * populated with a valid client reference following a successful return from
 * this function.  
 * @param serverURI A null-terminated string specifying the server to
 * which the client will connect. It takes the form <i>protocol://host:port</i>.
 * Currently, <i>protocol</i> must be <i>tcp</i>. For <i>host</i>, you can 
 * specify either an IP address or a domain name. For instance, to connect to
 * a server running on the local machines with the default MQTT port, specify
 * <i>tcp://localhost:1883</i>.
 * @param clientId The client identifier passed to the server when the
 * client connects to it. It is a null-terminated UTF-8 encoded string. 
 * ClientIDs must be no longer than 23 characters according to the MQTT 
 * specification.
 * @param persistence_type The type of persistence to be used by the client:
 * <br>
 * ::MQTTCLIENT_PERSISTENCE_NONE: Use in-memory persistence. If the device or 
 * system on which the client is running fails or is switched off, the current
 * state of any in-flight messages is lost and some messages may not be 
 * delivered even at QoS1 and QoS2.
 * <br>
 * ::MQTTCLIENT_PERSISTENCE_DEFAULT: Use the default (file system-based) 
 * persistence mechanism. Status about in-flight messages is held in persistent
 * storage and provides some protection against message loss in the case of 
 * unexpected failure.
 * <br>
 * ::MQTTCLIENT_PERSISTENCE_USER: Use an application-specific persistence
 * implementation. Using this type of persistence gives control of the 
 * persistence mechanism to the application. The application has to implement
 * the MQTTClient_persistence interface.
 * @param persistence_context If the application uses 
 * ::MQTTCLIENT_PERSISTENCE_NONE persistence, this argument is unused and should
 * be set to NULL. For ::MQTTCLIENT_PERSISTENCE_DEFAULT persistence, it
 * should be set to the location of the persistence directory (if set 
 * to NULL, the persistence directory used is the working directory).
 * Applications that use ::MQTTCLIENT_PERSISTENCE_USER persistence set this
 * argument to point to a valid MQTTClient_persistence structure.
 * @return ::MQTTCLIENT_SUCCESS if the client is successfully created, otherwise
 * an error code is returned.
 */
DLLExport int MQTTClient_create(MQTTClient* handle, char* serverURI, char* clientId,
		int persistence_type, void* persistence_context);

/**
 * MQTTClient_willOptions defines the MQTT "Last Will and Testament" (LWT) settings for
 * the client. In the event that a client unexpectedly loses its connection to
 * the server, the server publishes the LWT message to the LWT topic on
 * behalf of the client. This allows other clients (subscribed to the LWT topic)
 * to be made aware that the client has disconnected. To enable the LWT
 * function for a specific client, a valid pointer to an MQTTClient_willOptions 
 * structure is passed in the MQTTClient_connectOptions structure used in the
 * MQTTClient_connect() call that connects the client to the server. The pointer
 * to MQTTClient_willOptions can be set to NULL if the LWT function is not 
 * required.
 */
typedef struct
{
	/** The eyecatcher for this structure.  must be MQTW. */
	char struct_id[4];
	/** The version number of this structure.  Must be 0 */
	int struct_version;
	/** The LWT topic to which the LWT message will be published. */
	char* topicName;
	/** The LWT payload. */
	char* message;
	/**
      * The retained flag for the LWT message (see MQTTClient_message.retained).
      */
	int retained;
	/** 
      * The quality of service setting for the LWT message (see 
      * MQTTClient_message.qos and @ref qos).
      */
	int qos;
} MQTTClient_willOptions;

#define MQTTClient_willOptions_initializer { {'M', 'Q', 'T', 'W'}, 0, NULL, NULL, 0, 0 }

/**
* MQTTClient_sslProperties defines the settings to establish an SSL/TLS connection using the 
* OpenSSL library. It covers the following scenarios:
* - Server authentication: The client needs the digital certificate of the server. It is included
*   in a store containting trusted material (also known as "trust store").
* - Mutual authentication: Both client and server are authenticated during the SSL handshake. In 
*   addition to the digital certificate of the server in a trust store, the client will need its own 
*   digital certificate and the private key used to sign its digital certificate stored in a "key store".
* - Anonymous connection: Both client and server do not get authenticated and no credentials are needed 
*   to establish an SSL connection. Note that this scenario is not fully secure since it is subject to
*   man-in-the-middle attacks.
*/
typedef struct 
{
	/** The eyecatcher for this structure.  Must be MQTS */
	char struct_id[4];
	/** The version number of this structure.  Must be 0 */
	int struct_version;	
	
	/** The file in PEM format containing the public digital certificates trusted by the client. */
	char* trustStore;

	/** The file in PEM format containing the public certificate chain of the client. It may also include
	* the client's private key. 
	*/
	char* keyStore;
	
	/** If not included in the sslKeyStore, this setting points to the file in PEM format containing
	* the client's private key.
	*/
	char* privateKey;
	/** The password to load the client's privateKey if encrypted. */
	char* privateKeyPassword;
 
	/**
	* The list of cipher suites that the client will present to the server during the SSL handshake. For a 
	* full explanation of the cipher list format, please see the OpenSSL on-line documentation:
	* http://www.openssl.org/docs/apps/ciphers.html#CIPHER_LIST_FORMAT
	* If this setting is ommitted, its default value will be "ALL", that is, all the cipher suites -excluding
	* those offering no encryption- will be considered.
	* This setting can be used to set an SSL anonymous connection ("aNULL" string value, for instance).
	*/
	char* enabledCipherSuites;    

    /** True/False option to enable verification of the server certificate **/
    int enableServerCertAuth;
  
} MQTTClient_SSLOptions;

#define MQTTClient_SSLOptions_initializer { {'M', 'Q', 'T', 'S'}, 0, NULL, NULL, NULL, NULL, NULL, 1 }

/**
 * MQTTClient_connectOptions defines several settings that control the way the
 * client connects to an MQTT server. 
 *
 * <b>Note:</b> Default values are not defined for members of 
 * MQTTClient_connectOptions so it is good practice to specify all settings.
 * If the MQTTClient_connectOptions structure is defined as an automatic
 * variable, all members are set to random values and thus must be set by the
 * client application. If the MQTTClient_connectOptions structure is defined
 * as a static variable, initialization (in compliant compilers) sets all 
 * values to 0 (NULL for pointers). A #keepAliveInterval setting of 0 prevents
 * correct operation of the client and so you <b>must</b> at least set a value
 * for #keepAliveInterval.
 */
typedef struct
{
	/** The eyecatcher for this structure.  must be MQTC. */
	char struct_id[4];
	/** The version number of this structure.  Must be 0, 1 or 2.  
	  * 0 signifies no SSL options and no serverURIs
	  * 1 signifies no serverURIs 
	  */
	int struct_version;
	/** The "keep alive" interval, measured in seconds, defines the maximum time
   * that should pass without communication between the client and the server
   * The client will ensure that at least one message travels across the
   * network within each keep alive period.  In the absence of a data-related 
	 * message during the time period, the client sends a very small MQTT 
   * "ping" message, which the server will acknowledge. The keep alive 
   * interval enables the client to detect when the server is no longer 
	 * available without having to wait for the long TCP/IP timeout.
	 */
	int keepAliveInterval;
	/** 
   * This is a boolean value. The cleansession setting controls the behaviour
   * of both the client and the server at connection and disconnection time.
   * The client and server both maintain session state information. This
   * information is used to ensure "at least once" and "exactly once"
   * delivery, and "exactly once" receipt of messages. Session state also
   * includes subscriptions created by an MQTT client. You can choose to
   * maintain or discard state information between sessions. 
   *
   * When cleansession is true, the state information is discarded at 
   * connect and disconnect. Setting cleansession to false keeps the state 
   * information. When you connect an MQTT client application with 
   * MQTTClient_connect(), the client identifies the connection using the 
   * client identifier and the address of the server. The server checks 
   * whether session information for this client
   * has been saved from a previous connection to the server. If a previous 
   * session still exists, and cleansession=true, then the previous session 
   * information at the client and server is cleared. If cleansession=false,
   * the previous session is resumed. If no previous session exists, a new
   * session is started.
	 */
	int cleansession;
	/** 
   * This is a boolean value that controls how many messages can be in-flight
   * simultaneously. Setting <i>reliable</i> to true means that a published 
   * message must be completed (acknowledgements received) before another
   * can be sent. Attempts to publish additional messages receive an
   * ::MQTTCLIENT_MAX_MESSAGES_INFLIGHT return code. Setting this flag to
	 * false allows up to 10 messages to be in-flight. This can increase 
   * overall throughput in some circumstances.
	 */
	int reliable;		
	/** 
   * This is a pointer to an MQTTClient_willOptions structure. If your 
   * application does not make use of the Last Will and Testament feature, 
   * set this pointer to NULL.
   */
	MQTTClient_willOptions* will;
	/** 
   * MQTT servers that support the MQTT v3.1 protocol provide authentication
   * and authorisation by user name and password. This is the user name 
   * parameter. 
   */
	char* username;	
	/** 
   * MQTT servers that support the MQTT v3.1 protocol provide authentication
   * and authorisation by user name and password. This is the password 
   * parameter.
   */
	char* password;
	/**
   * The time interval in seconds to allow a connect to complete.
   */
	int connectTimeout;
	/**
	 * The time interval in seconds
	 */
	int retryInterval;
	/** 
   * This is a pointer to an MQTTClient_SSLOptions structure. If your 
   * application does not make use of SSL, set this pointer to NULL.
   */
	MQTTClient_SSLOptions* ssl;
	/**
	 * The number of entries in the optional serverURIs array. Defaults to 0.
	 */
	int serverURIcount;
	/**
   * An optional array of null-terminated strings specifying the servers to
   * which the client will connect. Each string takes the form <i>protocol://host:port</i>.
   * <i>protocol</i> must be <i>tcp</i> or <i>ssl</i>. For <i>host</i>, you can 
   * specify either an IP address or a host name. For instance, to connect to
   * a server running on the local machines with the default MQTT port, specify
   * <i>tcp://localhost:1883</i>.
   * If this list is empty (the default), the server URI specified on MQTTClient_create()
   * is used.
   */    
	char** serverURIs;
} MQTTClient_connectOptions;

#define MQTTClient_connectOptions_initializer { {'M', 'Q', 'T', 'C'}, 2, 60, 1, 1, NULL, NULL, NULL, 30, 20, NULL, 0, NULL }


typedef struct
{
	const char* name;
	const char* value;
} MQTTClient_nameValue;

DLLExport MQTTClient_nameValue* MQTTClient_getVersionInfo(void);

/**
  * This function attempts to connect a previously-created client (see
  * MQTTClient_create()) to an MQTT server using the specified options. If you
  * want to enable asynchronous message and status notifications, you must call
  * MQTTClient_setCallbacks() prior to MQTTClient_connect().
  * @param handle A valid client handle from a successful call to 
  * MQTTClient_create(). 
  * @param options A pointer to a valid MQTTClient_connectOptions
  * structure.
  * @return ::MQTTCLIENT_SUCCESS if the client successfully connects to the
  * server. An error code is returned if the client was unable to connect to
  * the server. 
  * Error codes greater than 0 are returned by the MQTT protocol:<br><br>
  * <b>1</b>: Connection refused: Unacceptable protocol version<br>
  * <b>2</b>: Connection refused: Identifier rejected<br>
  * <b>3</b>: Connection refused: Server unavailable<br>
  * <b>4</b>: Connection refused: Bad user name or password<br>
  * <b>5</b>: Connection refused: Not authorized<br>
  * <b>6-255</b>: Reserved for future use<br>
  */
DLLExport int MQTTClient_connect(MQTTClient handle, MQTTClient_connectOptions* options);

/**
  * This function attempts to disconnect the client from the MQTT
  * server. In order to allow the client time to complete handling of messages
  * that are in-flight when this function is called, a timeout period is 
  * specified. When the timeout period has expired, the client disconnects even
  * if there are still outstanding message acknowledgements.
  * The next time the client connects to the same server, any QoS 1 or 2 
  * messages which have not completed will be retried depending on the 
  * cleansession settings for both the previous and the new connection (see 
  * MQTTClient_connectOptions.cleansession and MQTTClient_connect()).
  * @param handle A valid client handle from a successful call to 
  * MQTTClient_create(). 
  * @param timeout The client delays disconnection for up to this time (in 
  * milliseconds) in order to allow in-flight message transfers to complete.
  * @return ::MQTTCLIENT_SUCCESS if the client successfully disconnects from 
  * the server. An error code is returned if the client was unable to disconnect
  * from the server
  */
DLLExport int MQTTClient_disconnect(MQTTClient handle, int timeout);

/**
  * This function allows the client application to test whether or not a
  * client is currently connected to the MQTT server.
  * @param handle A valid client handle from a successful call to 
  * MQTTClient_create(). 
  * @return Boolean true if the client is connected, otherwise false.
  */
DLLExport int MQTTClient_isConnected(MQTTClient handle);


/* Subscribe is synchronous.  QoS list parameter is changed on return to granted QoSs.
   Returns return code, MQTTCLIENT_SUCCESS == success, non-zero some sort of error (TBD) */

/**
  * This function attempts to subscribe a client to a single topic, which may 
  * contain wildcards (see @ref wildcard). This call also specifies the 
  * @ref qos requested for the subscription
  * (see also MQTTClient_subscribeMany()).
  * @param handle A valid client handle from a successful call to 
  * MQTTClient_create(). 
  * @param topic The subscription topic, which may include wildcards.
  * @param qos The requested quality of service for the subscription.
  * @return ::MQTTCLIENT_SUCCESS if the subscription request is successful. 
  * An error code is returned if there was a problem registering the 
  * subscription. 
  */
DLLExport int MQTTClient_subscribe(MQTTClient handle, char* topic);

DLLExport int MQTTClient_subscribe_many(MQTTClient handle, int count, char** topic);

DLLExport int MQTTClient_presence(MQTTClient handle, char* topic);

DLLExport int MQTTClient_unpresence(MQTTClient handle, char* topic);

DLLExport int get_present_info(char *topicName, MQTTClient_message* m, Presence_msg *presence_status);

DLLExport int MQTTClient_unsubscribe(MQTTClient handle, char* topic);

DLLExport int MQTTClient_unsubscribe_many(MQTTClient handle, int count, char** topic);

DLLExport int MQTTClient_publish(MQTTClient handle, char* topicName, int data_len, void* data);

DLLExport int MQTTClient_publish_json(MQTTClient handle, char* topicName, cJSON *data);

DLLExport int MQTTClient_publish_to_alias(MQTTClient handle, char* alias, int data_len, void* data);

DLLExport int MQTTClient_report(MQTTClient handle, char* action, char *data);

DLLExport int MQTTClient_set_alias(MQTTClient handle, char* alias);

DLLExport int MQTTClient_get_alias(MQTTClient handle, char* parameter);

DLLExport int MQTTClient_get_aliaslist(MQTTClient handle, char* parameter);

DLLExport int MQTTClient_get_topic(MQTTClient handle, char* parameter);

DLLExport int MQTTClient_get_status(MQTTClient handle, char* parameter);

DLLExport int MQTTClient_set_broker(MQTTClient *handle, char* broker);

DLLExport int MQTTClient_get_broker(MQTTClient *handle, char* broker);

DLLExport int MQTTClient_waitForCompletion(MQTTClient handle, MQTTClient_deliveryToken dt, unsigned long timeout);


/**
  * When implementing a single-threaded client, call this function periodically
  * to allow processing of message retries and to send MQTT keepalive pings.
  * If the application is calling MQTTClient_receive() regularly, then it is 
  * not necessary to call this function.
  */
DLLExport void MQTTClient_yield(void);

/**
  * This function performs a synchronous receive of incoming messages. It should
  * be used only when the client application has not set callback methods to
  * support asynchronous receipt of messages (see @ref async and 
  * MQTTClient_setCallbacks()). Using this function allows a single-threaded
  * client subscriber application to be written. When called, this function 
  * blocks until the next message arrives or the specified timeout expires 
  *(see also MQTTClient_yield()).
  *
  * <b>Important note:</b> The application must free() the memory allocated
  * to the topic and the message when processing is complete (see
  * MQTTClient_freeMessage()).
  * @param handle A valid client handle from a successful call to 
  * MQTTClient_create().
  * @param topicName The address of a pointer to a topic. This function 
  * allocates the memory for the topic and returns it to the application
  * by setting <i>topicName</i> to point to the topic.
  * @param topicLen The length of the topic. If the return code from this 
  * function is ::MQTTCLIENT_TOPICNAME_TRUNCATED, the topic contains embedded 
  * NULL characters and the full topic should be retrieved by using
  * <i>topicLen</i>.
  * @param message The address of a pointer to the received message. This
  * function allocates the memory for the message and returns it to the
  * application by setting <i>message</i> to point to the received message.
  * The pointer is set to NULL if the timeout expires.
  * @param timeout The length of time to wait for a message in milliseconds. 
  * @return ::MQTTCLIENT_SUCCESS or ::MQTTCLIENT_TOPICNAME_TRUNCATED if a 
  * message is received. ::MQTTCLIENT_SUCCESS can also indicate that the 
  * timeout expired, in which case <i>message</i> is NULL. An error code is 
  * returned if there was a problem trying to receive a message.
  */
DLLExport int MQTTClient_receive(MQTTClient handle, char** topicName, int* topicLen, MQTTClient_message** message,
		unsigned long timeout);

/**
  * This function frees memory allocated to an MQTT message, including the 
  * additional memory allocated to the message payload. The client application
  * calls this function when the message has been fully processed. <b>Important 
  * note:</b> This function does not free the memory allocated to a message 
  * topic string. It is the responsibility of the client application to free 
  * this memory using the MQTTClient_free() library function.
  * @param msg The address of a pointer to the ::MQTTClient_message structure 
  * to be freed.
  */
DLLExport void MQTTClient_freeMessage(MQTTClient_message** msg);

/**
  * This function frees memory allocated by the MQTT C client library, especially the
  * topic name. This is needed on Windows when the client libary and application
  * program have been compiled with different versions of the C compiler.  It is
  * thus good policy to always use this function when freeing any MQTT C client-
  * allocated memory.
  * @param ptr The pointer to the client library storage to be freed.
  */
DLLExport void MQTTClient_free(void* ptr);

/** 
  * This function frees the memory allocated to an MQTT client (see
  * MQTTClient_create()). It should be called when the client is no longer 
  * required.
  * @param handle A pointer to the handle referring to the ::MQTTClient 
  * structure to be freed.
  */
DLLExport void MQTTClient_destroy(MQTTClient* handle);

#endif
