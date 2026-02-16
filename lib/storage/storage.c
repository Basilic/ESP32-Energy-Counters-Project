#include "storage.h"         // Header du module storage pour les prototypes
#include "nvs_flash.h"       // Fonctions NVS pour initialiser la mémoire flash
#include "nvs.h"             // Fonctions NVS pour lire/écrire des valeurs
#include "esp_log.h"         // Fonctions ESP_LOG pour debug
#include "gpio_pulse.h"      // Pour accéder au tableau global counters
#include "config.h"          // Pour NB_COUNTERS et global_mode_config  

static const char *TAG = "STORAGE";

char wifi_ssid[32] = {0};
char  wifi_pass[64] = {0};
char mqtt_names[NB_COUNTERS][32] = {
    "compteur0",
    "compteur1",
    "compteur2",
    "compteur3",
    "compteur4"
};
uint8_t global_mode_config = 1; // Mode de configuration (0 = normal, 1 = AP)    


void nvs_init_and_load(void)
{
    // --- Initialisation NVS ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- Chargement des compteurs ---
    nvs_handle_t counters_handle;
    ret = nvs_open("counters", NVS_READWRITE, &counters_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS counters");
    } else {
        for (int i = 0; i < NB_COUNTERS; i++) {
            char key[8];
            snprintf(key, sizeof(key), "c%d", i);

            uint32_t value = 0;
            ret = nvs_get_u32(counters_handle, key, &value);
            if (ret == ESP_OK) {
                counters[i] = value;
            } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
                counters[i] = 0;
            } else {
                ESP_LOGW(TAG, "Erreur lecture NVS compteur %d", i);
                counters[i] = 0;
            }

            // --- Lecture des noms MQTT ---
            char mqtt_key[8];
            snprintf(mqtt_key, sizeof(mqtt_key), "m%d", i);
            size_t len = sizeof(mqtt_names[i]);
            ret = nvs_get_str(counters_handle, mqtt_key, mqtt_names[i], &len);
            if (ret == ESP_ERR_NVS_NOT_FOUND) {
                mqtt_names[i][0] = ' '; // Vide si non trouvé
            } else if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Erreur lecture NVS nom MQTT %d", i);
                mqtt_names[i][0] = ' ';
            }
        }
        nvs_close(counters_handle);
    }

    // --- Chargement du Wi-Fi ---
    nvs_handle_t wifi_handle;
    ret = nvs_open("wifi", NVS_READWRITE, &wifi_handle);
    if (ret == ESP_OK) {
        size_t len_ssid = sizeof(wifi_ssid);
        size_t len_pass = sizeof(wifi_pass);

        ret = nvs_get_str(wifi_handle, "ssid", wifi_ssid, &len_ssid);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            wifi_ssid[0] = 'TEST_Wifi';
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Erreur lecture NVS SSID");
            wifi_ssid[0] = 'TEST_Wifi';
        }

        ret = nvs_get_str(wifi_handle, "pass", wifi_pass, &len_pass);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            wifi_pass[0] = 'TEST_Wifi';
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Erreur lecture NVS password");
            wifi_pass[0] = 'Password';
        }

        nvs_close(wifi_handle);
    } else {
        ESP_LOGW(TAG, "Impossible d'ouvrir NVS pour Wi-Fi");
        wifi_ssid[0] = 'TEST_Wifi';
        wifi_pass[0] = 'Password';
    }

    // --- Chargement du mode configuration ---
    nvs_handle_t config_handle;
    ret = nvs_open("config", NVS_READWRITE, &config_handle);
    if (ret == ESP_OK) {
        ret = nvs_get_u8(config_handle, "config_mode", &global_mode_config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Mode configuration récupéré depuis NVS : %d", global_mode_config);
        } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "Mode configuration non trouvé dans NVS -> mode normal");
            global_mode_config = 0;
        } else {
            ESP_LOGW(TAG, "Erreur lecture NVS mode_config, mode normal par défaut");
            global_mode_config = 0;
        }
        nvs_close(config_handle);
    } else {
        ESP_LOGE(TAG, "Impossible d'ouvrir NVS pour lire mode config");
        global_mode_config = 0;
    }

    ESP_LOGI(TAG, "Compteurs, noms MQTT et configuration Wi-Fi chargés depuis NVS");
}


void save_counter_to_nvs(int idx, uint32_t value)
{
    nvs_handle_t handle;     
    esp_err_t ret = nvs_open("counters", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS pour écriture compteur %d", idx);
        return;
    }

    char key[8];
    snprintf(key, sizeof(key), "c%d", idx);

    ret = nvs_set_u32(handle, key, value); 
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'écrire compteur %d", idx);
        nvs_close(handle);
        return;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur commit NVS compteur %d", idx);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Compteur %d sauvegardé : %lu", idx, value);
}
