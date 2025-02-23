#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <curl/curl.h>

#include <mosquitto.h>
#include <mosquitto_plugin.h>
#include <mosquitto_broker.h>

#define DEFAULT_USER_URI "http://localhost:5555/api/v1/validUser"
#define DEFAULT_ACL_URI "http://localhost:5555/api/v1/validACL"

static char *http_user_uri = NULL;
static char *http_acl_uri = NULL;

char *gen_uuid();

int mosquitto_auth_plugin_version(void)
{
    return MOSQ_AUTH_PLUGIN_VERSION;
}

int mosquitto_auth_plugin_init(void **user_data, struct mosquitto_opt *auth_opts, int auth_opt_count)
{
    int i = 0;
    for (i = 0; i < auth_opt_count; i++)
    {
#ifdef MQAP_DEBUG
        fprintf(stderr, "AuthOptions: key=%s, val=%s\n", auth_opts[i].key, auth_opts[i].value);
#endif
        if (strncmp(auth_opts[i].key, "http_user_uri", 13) == 0)
        {
            http_user_uri = auth_opts[i].value;
        }
        if (strncmp(auth_opts[i].key, "http_acl_uri", 12) == 0)
        {
            http_acl_uri = auth_opts[i].value;
        }
    }

    if (http_user_uri == NULL)
    {
        http_user_uri = DEFAULT_USER_URI;
    }

    if (http_acl_uri == NULL)
    {
        http_acl_uri = DEFAULT_ACL_URI;
    }
    mosquitto_log_printf(MOSQ_LOG_INFO, "http_user_uri = %s, http_acl_uri = %s", http_user_uri, http_acl_uri);
#ifdef MQAP_DEBUG
    fprintf(stderr, "http_user_uri = %s, http_acl_uri = %s\n", http_user_uri, http_acl_uri);
#endif
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_plugin_cleanup(void *user_data, struct mosquitto_opt *auth_opts, int auth_opt_count)
{
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_security_init(void *user_data, struct mosquitto_opt *auth_opts, int auth_opt_count, bool reload)
{
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_security_cleanup(void *user_data, struct mosquitto_opt *auth_opts, int auth_opt_count, bool reload)
{
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_unpwd_check(void *user_data, struct mosquitto *client, const char *username, const char *password)
{
    if (username == NULL || password == NULL)
    {
        return MOSQ_ERR_AUTH;
    }
#ifdef MQAP_DEBUG
    fprintf(stderr, "mosquitto_auth_unpwd_check: username=%s, password=%s\n", username, password);
#endif
    mosquitto_log_printf(MOSQ_LOG_DEBUG, "mosquitto_auth_unpwd_check: username=%s, password=%s", username, password);

    int rc;
    int rv;
    CURL *ch;

    if ((ch = curl_easy_init()) == NULL)
    {
        mosquitto_log_printf(MOSQ_LOG_WARNING, "failed to initialize curl (curl_easy_init AUTH): %s", strerror(errno));
#ifdef MQAP_DEBUG
        fprintf(stderr, "malloc(): %s [%s, %d]\n", strerror(errno), __FILE__, __LINE__);
#endif
        return MOSQ_ERR_AUTH;
    }

    char *escaped_username;
    char *escaped_password;
    char *request_template = "{\"data\":{\"userName\":\"\",\"token\":\"\"},\"deviceInfo\":{\"osVersion\":\"\",\"os\":\"\",\"deviceName\":\"\",\"deviceId\":\"\"},\"language\":\"vi\",\"ipRequest\":\"\",\"channel\":\"MQTT_NOTIFY\",\"requestId\":\"\"}";
    char *requestId = gen_uuid();
    escaped_username = curl_easy_escape(ch, username, 0);
    escaped_password = curl_easy_escape(ch, password, 0);

    size_t data_len = strlen(request_template) + strlen(escaped_username) + strlen(escaped_password) + 1 + 36;
    char *data = NULL;
    if ((data = malloc(data_len)) == NULL)
    {
        mosquitto_log_printf(MOSQ_LOG_WARNING, "failed allocate data memory (%u): %s", data_len, strerror(errno));
#ifdef MQAP_DEBUG
        fprintf(stderr, "malloc(): %s [%s, %d]\n", strerror(errno), __FILE__, __LINE__);
#endif
        rv = -1;
    }
    else
    {
        memset(data, 0, data_len);
        snprintf(data, data_len, "{\"data\":{\"userName\":\"%s\",\"token\":\"%s\"},\"deviceInfo\":{\"osVersion\":\"\",\"os\":\"\",\"deviceName\":\"\",\"deviceId\":\"\"},\"language\":\"vi\",\"ipRequest\":\"\",\"channel\":\"MQTT_NOTIFY\",\"requestId\":\"%s\"}", escaped_username, escaped_password, requestId);
        mosquitto_log_printf(MOSQ_LOG_DEBUG, data);

        struct curl_slist *headers = NULL;

        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "charset: utf-8");

        curl_easy_setopt(ch, CURLOPT_POST, 1L);
        curl_easy_setopt(ch, CURLOPT_URL, http_user_uri);
        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, strlen(data));

        if ((rv = curl_easy_perform(ch)) == CURLE_OK)
        {
            curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &rc);
            rv = rc;
        }
        else
        {
#ifdef MQAP_DEBUG
            fprintf(stderr, "%s\n", curl_easy_strerror(rv));
#endif
            rv = -1;
        }
    }
    curl_free(escaped_username);
    curl_free(escaped_password);
    curl_easy_cleanup(ch);
    free(data);
    data = NULL;
    if (rv == -1)
    {
        return MOSQ_ERR_AUTH;
    }
#ifdef MQAP_DEBUG
    if (rc != 200)
    {
        fprintf(stderr, "HTTP response code = %d\n", rc);
    }
#endif
    mosquitto_log_printf(MOSQ_LOG_DEBUG, "HTTP response code = %d", rc);



    return (rc == 200 ? MOSQ_ERR_SUCCESS : MOSQ_ERR_AUTH);
}

int mosquitto_auth_acl_check(void *user_data, int access, struct mosquitto *client, const struct mosquitto_acl_msg *msg)
{
    const char *username = mosquitto_client_username(client);
    const char *clientid = mosquitto_client_id(client);
    const char *topic = msg->topic;
    if (username == NULL)
    {
        // If the username is NULL then it's an anonymous user, currently we let
        // this pass assuming the admin will disable anonymous users if required.
        return MOSQ_ERR_SUCCESS;
    }

    char access_name[6];
    if (access == MOSQ_ACL_READ)
    {
        sprintf(access_name, "read");
    }
    else if (access == MOSQ_ACL_WRITE)
    {
        sprintf(access_name, "write");
    }
    else if (access == MOSQ_ACL_SUBSCRIBE)
    {
        sprintf(access_name, "sub");
    }

#ifdef MQAP_DEBUG
    fprintf(stderr, "mosquitto_auth_acl_check: clientid=%s, username=%s, topic=%s, access=%s\n",
                    clientid, username, topic, access_name);
#endif
    mosquitto_log_printf(MOSQ_LOG_DEBUG, "mosquitto_auth_acl_check: clientid=%s, username=%s, topic=%s, access=%s",
                                             clientid, username, topic, access_name);

    int rc;
    int rv;
    CURL *ch;

    if ((ch = curl_easy_init()) == NULL)
    {
        mosquitto_log_printf(MOSQ_LOG_WARNING, "failed to initialize curl (curl_easy_init ACL): %s", strerror(errno));
#ifdef MQAP_DEBUG
        fprintf(stderr, "malloc(): %s [%s, %d]\n", strerror(errno), __FILE__, __LINE__);
#endif
        return MOSQ_ERR_ACL_DENIED;
    }

    char *escaped_clientid;
    char *escaped_username;
    char *escaped_topic;
    char *request_template = "{\"data\":{\"clientId\":\"\",\"userName\":\"\",\"topic\":\"\",\"access\":\"\"},\"deviceInfo\":{\"osVersion\":\"\",\"os\":\"\",\"deviceName\":\"\",\"deviceId\":\"\"},\"language\":\"vi\",\"ipRequest\":\"\",\"channel\":\"MQTT_NOTIFY\",\"requestId\":\"\"}";
    char *requestId = gen_uuid();
    escaped_clientid = curl_easy_escape(ch, clientid, 0);
    escaped_username = curl_easy_escape(ch, username, 0);
    escaped_topic = curl_easy_escape(ch, topic, 0);
    size_t data_len = strlen(request_template) + strlen(escaped_clientid) + strlen(escaped_username) + strlen(escaped_topic) + strlen(access_name) + 1 + 36;
    char *data = NULL;
    if ((data = malloc(data_len)) == NULL)
    {
        mosquitto_log_printf(MOSQ_LOG_WARNING, "failed allocate data memory (%u): %s", data_len, strerror(errno));
#ifdef MQAP_DEBUG
        fprintf(stderr, "malloc(): %s [%s, %d]\n", strerror(errno), __FILE__, __LINE__);
#endif
        rv = -1;
    }
    else
    {
        memset(data, 0, data_len);
        snprintf(data, data_len, "{\"data\":{\"clientId\":\"%s\",\"userName\":\"%s\",\"topic\":\"%s\",\"access\":\"%s\"},\"deviceInfo\":{\"osVersion\":\"\",\"os\":\"\",\"deviceName\":\"\",\"deviceId\":\"\"},\"language\":\"vi\",\"ipRequest\":\"\",\"channel\":\"MQTT_NOTIFY\",\"requestId\":\"%s\"}",
                         escaped_clientid, escaped_username, escaped_topic, access_name, requestId);

        mosquitto_log_printf(MOSQ_LOG_DEBUG, data);

        struct curl_slist *headers = NULL;

        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "charset: utf-8");

        curl_easy_setopt(ch, CURLOPT_POST, 1L);
        curl_easy_setopt(ch, CURLOPT_URL, http_acl_uri);
        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, strlen(data));

        if ((rv = curl_easy_perform(ch)) == CURLE_OK)
        {
            curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &rc);
            rv = rc;
        }
        else
        {
#ifdef MQAP_DEBUG
            fprintf(stderr, "%s\n", curl_easy_strerror(rv));
#endif
            rv = -1;
        }
    }
    curl_free(escaped_clientid);
    curl_free(escaped_username);
    curl_free(escaped_topic);
    curl_easy_cleanup(ch);
    free(data);
    data = NULL;
    if (rv == -1)
    {
        return MOSQ_ERR_ACL_DENIED;
    }
#ifdef MQAP_DEBUG
    if (rc != 200)
    {
        fprintf(stderr, "HTTP response code = %d\n", rc);
    }
#endif
    mosquitto_log_printf(MOSQ_LOG_DEBUG, "HTTP response code = %d", rc);

    return (rc == 200 ? MOSQ_ERR_SUCCESS : MOSQ_ERR_ACL_DENIED);
}

int mosquitto_auth_psk_key_get(void *user_data, struct mosquitto *client, const char *hint, const char *identity, char *key, int max_key_len)
{
    return MOSQ_ERR_AUTH;
}

char* gen_uuid() {
    char v[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    //3fb17ebc-bc38-4939-bc8b-74f2443281d4
    //8 dash 4 dash 4 dash 4 dash 12
    static char buf[37] = {0};

    //gen random for all spaces because lazy
    for(int i = 0; i < 36; ++i) {
        buf[i] = v[rand()%16];
    }

    //put dashes in place
    buf[8] = '-';
    buf[13] = '-';
    buf[18] = '-';
    buf[23] = '-';

    //needs end byte
    buf[36] = '\0';

    return buf;
}
