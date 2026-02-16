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


/* ========================= WEB HANDLERS ========================= */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<title>ESP32 Configuration</title>"
        "</head>"
        "<body>"
        "<h2>Configuration ESP32</h2>"
        "<form method='POST' action='/save'>"
        "<h3>WiFi</h3>"
        "SSID:<br>"
        "<input type='text' name='ssid' value='");

    httpd_resp_sendstr_chunk(req, wifi_ssid);

    httpd_resp_sendstr_chunk(req,
        "'><br><br>"
        "Mot de passe:<br>"
        "<input type='password' name='pass' value='");

    httpd_resp_sendstr_chunk(req, wifi_pass);

    httpd_resp_sendstr_chunk(req,
        "'><br><br>"
        "<h3>Compteurs</h3>");

    // ---- Boucle pour les 5 compteurs ----
    for (int i = 0; i < NB_COUNTERS; i++)
    {
        char line[256];

        snprintf(line, sizeof(line),
            "Compteur %d:<br>"
            "<input type='number' name='c%d' value='%lu'> "
            "Nom:<input type='text' name='m%d' value='%s'>"
            "<br><br>",
            i + 1,
            i, (unsigned long)counters[i],
            i, mqtt_names[i]);

        httpd_resp_sendstr_chunk(req, line);
    }

    httpd_resp_sendstr_chunk(req,
        "<button type='submit'>Enregistrer</button>"
        "</form>"
        "</body>"
        "</html>");

    // IMPORTANT : fin de réponse
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[512];
    int total_len = req->content_len;
    int received = 0;
    int ret;

    if (total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return ESP_FAIL;
    }

    while (received < total_len) {
        ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    buf[received] = '\0';

    ESP_LOGI("SAVE", "POST DATA: %s", buf);

    // -------- PARSING ROBUSTE --------
    char *token = strtok(buf, "&");

    while (token != NULL)
    {
        char *eq = strchr(token, '=');
        if (eq)
        {
            *eq = '\0';
            char *key = token;
            char *value = eq + 1;

            // ---- Compteurs ----
            for (int i = 0; i < NB_COUNTERS; i++)
            {
                char expected[8];
                snprintf(expected, sizeof(expected), "c%d", i);

                if (strcmp(key, expected) == 0)
                {
                    counters[i] = strtoul(value, NULL, 10);
                    ESP_LOGI("SAVE", "Counter %d = %lu", i, counters[i]);
                }
            }

            // ---- Noms MQTT ----
            for (int i = 0; i < NB_COUNTERS; i++)
            {
                char expected[8];
                snprintf(expected, sizeof(expected), "m%d", i);

                if (strcmp(key, expected) == 0)
                {
                    strncpy(mqtt_names[i], value, sizeof(mqtt_names[i]) - 1);
                    mqtt_names[i][sizeof(mqtt_names[i]) - 1] = '\0';
                    ESP_LOGI("SAVE", "MQTT name %d = %s", i, mqtt_names[i]);
                }
            }

            // ---- WiFi ----
            if (strcmp(key, "ssid") == 0)
            {
                strncpy(wifi_ssid, value, sizeof(wifi_ssid) - 1);
                wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
                ESP_LOGI("SAVE", "SSID = %s", wifi_ssid);
            }

            if (strcmp(key, "pass") == 0)
            {
                strncpy(wifi_pass, value, sizeof(wifi_pass) - 1);
                wifi_pass[sizeof(wifi_pass) - 1] = '\0';
                ESP_LOGI("SAVE", "PASS updated");
            }
        }

        token = strtok(NULL, "&");
    }

    // -------- SAUVEGARDE NVS --------

    nvs_handle_t handle;
    esp_err_t err;

    // --- Compteurs ---
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
        ESP_LOGE("SAVE", "Failed to open NVS counters");
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
        ESP_LOGE("SAVE", "Failed to open NVS wifi");
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
        .handler = root_get_handler
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
