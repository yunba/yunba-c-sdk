/*******************************************************************************
 * Copyright (c) 2009, 2013 IBM Corp.
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
 *    Ian Craggs - add SSL support
 *******************************************************************************/

/**
 * @file
 * \brief functions which apply to client structures
 * */


#include "Clients.h"

#include <string.h>
#include <stdio.h>

#include "Log.h"

extern ClientStates* bstate;
/**
 * List callback function for comparing clients by clientid
 * @param a first integer value
 * @param b second integer value
 * @return boolean indicating whether a and b are equal
 */
int clientIDCompare(void* a, void* b)
{
	Clients* client = (Clients*)a;
	/*printf("comparing clientdIDs %s with %s\n", client->clientID, (char*)b);*/
	return strcmp(client->clientID, (char*)b) == 0;
}


/**
 * List callback function for comparing clients by socket
 * @param a first integer value
 * @param b second integer value
 * @return boolean indicating whether a and b are equal
 */
int clientSocketCompare(void* a, void* b)
{
	Clients* client = (Clients*)a;
	/*printf("comparing %d with %d\n", (char*)a, (char*)b); */
	return client->net.socket == *(int*)b;
}

int get_client_mqtt_version_from_network_handler(networkHandles* handler)
{
    if(NULL == handler || NULL == bstate )
    {
        return 0x13;
    }
    int sockId = handler->socket;
	Clients* client = (Clients*)(ListFindItem(bstate->clients, &sockId, clientSocketCompare)->content);
    if(NULL == client)
    {
        //use yunba mqtt by default
        return 0x13;
    }else{

        Log(TRACE_MINIMUM, -1, "mqttversion is :%d", client->MQTTVersion);
        if(client->MQTTVersion == 0)
        {
            return 0x13;
        }else{
            return client->MQTTVersion;
        }
    }
}
