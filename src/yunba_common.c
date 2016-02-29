/*
 * yunba_common.c
 *
 *  Created on: Nov 16, 2015
 *      Author: yunba
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

#include "yunba_common.h"

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
    if (host_entry == NULL) return -1;

    char* p = inet_ntoa(*((struct in_addr *)host_entry->h_addr));
    if (p == NULL) return -1;
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

int tcp_post_json(char *json_data, char *hostname, uint16_t port, char *path, CALLBACK cb) {
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
    if (host_entry == NULL) return -1;

    char* p = inet_ntoa(*((struct in_addr *)host_entry->h_addr));
    if (p == NULL) return -1;
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

	uint16_t json_len = strlen(json_data);
    buf[0] = 1; //version
    buf[1] = (uint8_t)((json_len >> 8) & 0xff);
    buf[2] = (uint8_t)(json_len & 0xff);
    memcpy(buf + 3, json_data, strlen(json_data));

    //TODO:　可能没写完？
    ret = write(sockfd, buf, json_len + 3);
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
		if (i > 3) {
			uint16_t len = (uint16_t)(((uint8_t)buf[1] << 8) | (uint8_t)buf[2]);
			ret = (i == (len + 3)) ? cb(buf + 3) : -1;
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

int MQTTClient_setup_with_appkey_v2(char* appkey, REG_info *info)
{
	char json_data[1024];
	sprintf(json_data, "{\"a\": \"%s\", \"p\":4}", appkey);

	int ret = tcp_post_json(json_data, "reg-t.yunba.io", 9944, "/device/reg/", reg_cb);
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

int MQTTClient_setup_with_appkey_and_deviceid_v2(char* appkey, char *deviceid, REG_info *info)
{
	char json_data[1024];

	if (appkey == NULL)
		return -1;

	if (deviceid == NULL)
		sprintf(json_data, "{\"a\": \"%s\", \"p\":4}", appkey);
	else
		sprintf(json_data, "{\"a\": \"%s\", \"p\":4, \"d\": \"%s\"}", appkey, deviceid);

	int ret = tcp_post_json(json_data, "reg-t.yunba.io", 9944, "/device/reg/", reg_cb);
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

int MQTTClient_get_host_v2(char *appkey, char* url)
{
	int ret = -1;
	char json_data[1024];

	sprintf(json_data, "{\"a\":\"%s\",\"n\":\"%s\",\"v\":\"%s\",\"o\":\"%s\"}",
			appkey, /*${networktype}*/"1", "v1.0.0", /*${NetworkOperator}*/"1");

	ret = tcp_post_json(json_data, "tick-t.yunba.io", 9977, "/", get_broker_cb);
	if (ret < 0)
		return -1;

	strcpy(url, url_host);
	return 0;
}
