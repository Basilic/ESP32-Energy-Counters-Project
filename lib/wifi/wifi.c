/**
 * @file wifi.c
 * @brief Gestion Wi-Fi STA + Mode AP configuration avec serveur Web.
 */

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_netif.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>


//#define AP_SSID "ESP32_CONFIG"
//#define AP_PASS "12345678"

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_event_group;
static httpd_handle_t server = NULL;

//extern char mqtt_names[5][32];

//char page[2048] = {0};


/* ========================= WIFI STA ========================= */

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, wifi_pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;


    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    xEventGroupWaitBits(wifi_event_group,
                        WIFI_CONNECTED_BIT,
                        pdFALSE,
                        pdTRUE,
                        portMAX_DELAY);
}


//--- Échapper &, <, >, ", ' ---
static void html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t o = 0;
    for (size_t i = 0; src && src[i] != '\0' && o + 1 < dst_size; i++) {
        const char *rep = NULL;
        switch ((unsigned char)src[i]) {
            case '&': rep = "&amp;";  break;
            case '<': rep = "&lt;";   break;
            case '>': rep = "&gt;";   break;
            case '"': rep = "&quot;"; break;
            case '\'':rep = "&#39;";  break;
            default:
                dst[o++] = src[i];
                continue;
        }
        size_t rlen = strlen(rep);
        if (o + rlen >= dst_size) break;
        memcpy(dst + o, rep, rlen);
        o += rlen;
    }
    dst[o] = '\0';
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // Buffers échappés (tailles confortables)
    char esc_ssid[128], esc_pass[128];
    char esc_mqserv[256], esc_mquser[128], esc_mqpass[128];
    char esc_name[128];

    html_escape(wifi_ssid,   esc_ssid,  sizeof esc_ssid);
    html_escape(wifi_pass,   esc_pass,  sizeof esc_pass);
    html_escape(mqtt_Server, esc_mqserv,sizeof esc_mqserv);
    html_escape(mqtt_user,   esc_mquser,sizeof esc_mquser);
    html_escape(mqtt_pass,   esc_mqpass,sizeof esc_mqpass);

    // Macro pour checker les envois
    #define SEND(S) do {                              \
        esp_err_t __e = httpd_resp_sendstr_chunk(req, (S)); \
        if (__e != ESP_OK) {                          \
            ESP_LOGE("HTTP", "send failed (%d) at [%s]", __e, (S)); \
            httpd_resp_sendstr_chunk(req, NULL);      \
            return __e;                               \
        }                                             \
    } while(0)

    // Buffer pour composer les lignes (jamais vide)
    char line[512];

    // --- Envoi du header/page ---
    SEND("<!DOCTYPE html>"
         "<html><head>"
         "<meta charset=\"UTF-8\">"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
         "<title>ESP32 Configuration</title>"
         "<style>"
         "body{font-family:sans-serif;margin:16px;max-width:860px}"
         "label{display:block;margin-top:10px}"
         "input{width:100%;max-width:520px;padding:6px;margin:4px 0}"
         "h2{margin-bottom:6px} h3{margin-top:18px}"
         "button{padding:8px 14px;margin-top:12px}"
         "</style>"
         "</head><body>"
         "<h2>Configuration ESP32</h2>"
         "<form method=\"POST\" action=\"/save\">"

         "<h3>Wi‑Fi</h3>"
         "<label>SSID</label>"
    );

    // SSID
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"ssid\" value=\"%s\"><br><br>",
             esc_ssid);
    SEND(line);

    // Pass Wi-Fi
    SEND("<label>Mot de passe</label>");
    snprintf(line, sizeof line,
             "<input type=\"password\" name=\"pass\" value=\"%s\"><br><br>",
             esc_pass);
    SEND(line);

    // --- MQTT ---
    SEND("<h3>MQTT</h3>"
         "<label>Serveur MQTT</label>");
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"mqtt_server\" "
             "placeholder=\"mqtt://192.168.1.1:1883\" value=\"%s\"><br><br>",
             esc_mqserv);
    SEND(line);

    SEND("<label>Utilisateur MQTT</label>");
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"mqtt_user\" value=\"%s\"><br><br>",
             esc_mquser);
    SEND(line);

    SEND("<label>Mot de passe MQTT</label>");
    snprintf(line, sizeof line,
             "<input type=\"password\" name=\"mqtt_pass\" value=\"%s\"><br><br>",
             esc_mqpass);
    SEND(line);

    // --- Compteurs ---
    SEND("<h3>Compteurs</h3>");
    for (int i = 0; i < NB_COUNTERS; i++) {
        html_escape(mqtt_names[i], esc_name, sizeof esc_name);
        snprintf(line, sizeof line,
                 "Compteur %d:<br>"
                 "<input type=\"number\" name=\"c%d\" value=\"%lu\"><br>"
                 "Nom:<br>"
                 "<input type=\"text\" name=\"m%d\" value=\"%s\"><br><br>",
                 i + 1,
                 i, (unsigned long)counters[i],
                 i, esc_name);
        SEND(line);
    }

    // Bouton + fin
    SEND("<button type=\"submit\">Enregistrer</button>"
         "</form></body></html>");

    // Fin de la réponse chunked
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;

    #undef SEND
}

// Décode application/x-www-form-urlencoded (%, +)
static void url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t si = 0, di = 0;
    if (!src || !dst || dst_size == 0) return;

    while (src[si] != '\0' && di + 1 < dst_size)
    {
        if (src[si] == '%' &&
            src[si+1] && src[si+2] &&
            isxdigit((unsigned char)src[si+1]) &&
            isxdigit((unsigned char)src[si+2]))
        {
            char hex[3] = { src[si+1], src[si+2], 0 };
            dst[di++] = (char) strtol(hex, NULL, 16);
            si += 3;
        }
        else if (src[si] == '+')
        {
            dst[di++] = ' ';
            si++;
        }
        else
        {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[512];
    int total_len = req->content_len;
    int received = 0;
    int ret;

    if (total_len >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return ESP_FAIL;
    }

    while (received < total_len) {
        ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            // Optionnel: si ret == HTTPD_SOCK_ERR_TIMEOUT, on peut continuer
            return ESP_FAIL;
        }
        received += ret;
    }

    buf[received] = '\0';
    ESP_LOGI("SAVE", "POST RAW: %s", buf);

    // -------- PARSING ROBUSTE --------
    // IMPORTANT: séparer sur "&" (et non sur "&amp;")
    char *saveptr = NULL;
    char *token = strtok_r(buf, "&", &saveptr);

    while (token != NULL)
    {
        char *eq = strchr(token, '=');
        if (eq)
        {
            *eq = '\0';
            char *key   = token;
            char *value = eq + 1;

            // Décoder la valeur encodée en x-www-form-urlencoded
            char decoded[256];
            url_decode(value, decoded, sizeof(decoded));

            // ---- Compteurs ----
            for (int i = 0; i < NB_COUNTERS; i++)
            {
                char expected[8];
                snprintf(expected, sizeof(expected), "c%d", i);

                if (strcmp(key, expected) == 0)
                {
                    // Si vide -> 0
                    if (decoded[0] == '\0') {
                        counters[i] = 0;
                    } else {
                        counters[i] = strtoul(decoded, NULL, 10);
                    }
                    ESP_LOGI("SAVE", "Counter %d = %lu", i, (unsigned long)counters[i]);
                }
            }

            // ---- Noms MQTT ----
            for (int i = 0; i < NB_COUNTERS; i++)
            {
                char expected[8];
                snprintf(expected, sizeof(expected), "m%d", i);

                if (strcmp(key, expected) == 0)
                {
                    strncpy(mqtt_names[i], decoded, sizeof(mqtt_names[i]) - 1);
                    mqtt_names[i][sizeof(mqtt_names[i]) - 1] = '\0';
                    ESP_LOGI("SAVE", "MQTT name %d = %s", i, mqtt_names[i]);
                }
            }

            // ---- WiFi ----
            if (strcmp(key, "ssid") == 0)
            {
                strncpy(wifi_ssid, decoded, sizeof(wifi_ssid) - 1);
                wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
                ESP_LOGI("SAVE", "SSID = %s", wifi_ssid);
            }
            else if (strcmp(key, "pass") == 0)
            {
                strncpy(wifi_pass, decoded, sizeof(wifi_pass) - 1);
                wifi_pass[sizeof(wifi_pass) - 1] = '\0';
                ESP_LOGI("SAVE", "PASS updated (len=%u)", (unsigned)strlen(wifi_pass));
            }
            // ---- MQTT Server ----
            else if (strcmp(key, "mqtt_server") == 0)
            {
                // Ici decoded contient déjà: "mqtt://192.168.1.1:1883"
                strncpy(mqtt_Server, decoded, sizeof(mqtt_Server) - 1);
                mqtt_Server[sizeof(mqtt_Server) - 1] = '\0';
                ESP_LOGI("SAVE", "MQTT_SERVER = %s", mqtt_Server);
            }
            // ---- MQTT User ----
            else if (strcmp(key, "mqtt_user") == 0)
            {
                strncpy(mqtt_user, decoded, sizeof(mqtt_user) - 1);
                mqtt_user[sizeof(mqtt_user) - 1] = '\0';
                ESP_LOGI("SAVE", "MQTT_USER = %s", mqtt_user);
            }
            // ---- MQTT Password ----
            else if (strcmp(key, "mqtt_pass") == 0)
            {
                strncpy(mqtt_pass, decoded, sizeof(mqtt_pass) - 1);
                mqtt_pass[sizeof(mqtt_pass) - 1] = '\0';
                ESP_LOGI("SAVE", "MQTT_PASS updated (len=%u)", (unsigned)strlen(mqtt_pass));
            }
        }

        token = strtok_r(NULL, "&", &saveptr);
    }

    // -------- SAUVEGARDE NVS --------
    nvs_handle_t handle;
    esp_err_t err;

    // --- Compteurs + noms ---
    err = nvs_open("counters", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        for (int i = 0; i < NB_COUNTERS; i++)
        {
            char key[8];

            snprintf(key, sizeof(key), "c%d", i);
            nvs_set_u32(handle, key, counters[i]);

            snprintf(key, sizeof(key), "m%d", i);
            nvs_set_str(handle, key, mqtt_names[i]);
        }

        nvs_commit(handle);
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE("SAVE", "Failed to open NVS counters: %s", esp_err_to_name(err));
    }

    // --- WiFi ---
    err = nvs_open("wifi", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        nvs_set_str(handle, "ssid", wifi_ssid);
        nvs_set_str(handle, "pass", wifi_pass);
        nvs_commit(handle);
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE("SAVE", "Failed to open NVS wifi: %s", esp_err_to_name(err));
    }

    // --- MQTT ---
    err = nvs_open("mqtt", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        nvs_set_str(handle, "mqtt_server", mqtt_Server);
        nvs_set_str(handle, "mqtt_user",   mqtt_user);
        nvs_set_str(handle, "mqtt_pass",   mqtt_pass);
        nvs_commit(handle);
        nvs_close(handle);
    }
    else
    {
        ESP_LOGE("SAVE", "Failed to open NVS mqtt: %s", esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h2>Configuration saved</h2>"
        "<p>Rebooting...</p></body></html>");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &config);

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = config_get_handler
    };

    httpd_register_uri_handler(server, &root);

     httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &save);
}

/* ========================= MODE AP ========================= */

void start_config_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode (open)...");
    // --- INITIALISATION REQUISE ---
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    // Configuration AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = "",             // open
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));          // Met en mode AP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config)); // Applique la config
    ESP_ERROR_CHECK(esp_wifi_start());                          // Démarre l’AP
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "AP Started. SSID: %s (open)", AP_SSID);

    start_webserver(); // Lance le serveur web de configuration
}
