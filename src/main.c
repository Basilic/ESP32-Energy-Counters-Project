/**
 * @file main.c
 * @brief Programme principal pour ESP32 :
 *        - Tâche de comptage d'impulsions avec anti-rebond et sauvegarde NVS
 *        - Tâche Wi-Fi + MQTT pour publication périodique des compteurs
 *        - Initialisation des périphériques et des modules
 */

#include <stdio.h>                  // Pour fonctions standard comme snprintf
#include "freertos/FreeRTOS.h"      // Pour types et fonctions FreeRTOS de base
#include "freertos/task.h"          // Pour xTaskCreate, vTaskDelay, etc.
#include "esp_system.h"             // Pour fonctions système ESP (reset, reboot)
#include "driver/gpio.h"            // Pour configurer et lire/écrire les GPIO
#include "esp_timer.h"              // Pour obtenir le temps en microsecondes
#include "nvs_flash.h"              // Pour initialiser la NVS (stockage persistant)
#include "nvs.h"                    // Pour lire/écrire des valeurs dans la NVS
#include "esp_task_wdt.h"           // Pour le Task Watchdog (WDT)
#include "wifi.h"                   // Module Wi-Fi personnalisé (wifi_init, etc.)
#include "mqtt.h"                   // Module MQTT personnalisé (mqtt_init, mqtt_publish)
#include "gpio_pulse.h"             // Module de comptage d'impulsions et ISR
#include "storage.h"                // Module de stockage NVS pour les compteurs
#include "config.h"// Inclusion du header global de configuration (ex : DEBOUNCE_US, NB_COUNTERS)

#include "esp_log.h"

static const char *TAG = "APP_MAIN";

//uint32_t counters[NB_COUNTERS] = {0};

// Mutex pour protéger l'accès aux compteurs partagés
SemaphoreHandle_t counter_mutex; 

// ----------------------------------------------------------------------
// ------------------- Tâche de comptage des impulsions -----------------
// ----------------------------------------------------------------------
/**
 * @brief Tâche qui compte les impulsions sur chaque compteur,
 *        applique un anti-rebond, et sauvegarde toutes les 100 impulsions dans la NVS.
 *
 * @param pv : argument passé à la tâche (non utilisé ici)
 */
void task_counter(void *pv)
{
    esp_task_wdt_add(NULL);                 // Ajoute cette tâche au WDT pour surveillance
    uint32_t last_saved[NB_COUNTERS] = {0}; // Stocke la dernière valeur sauvegardée pour chaque compteur

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));    // Délai 500 ms pour limiter la fréquence de vérification

        xSemaphoreTake(counter_mutex, portMAX_DELAY); // Accès exclusif aux compteurs
        for (int i = 0; i < NB_COUNTERS; i++) {
            // Si 100 impulsions ou plus depuis la dernière sauvegarde
            if ((counters[i] - last_saved[i]) >= 100) {
                save_counter_to_nvs(i, counters[i]); // Sauvegarde dans la NVS
                last_saved[i] = counters[i];         // Met à jour la dernière valeur sauvegardée
            }
        }
        xSemaphoreGive(counter_mutex);        // Libère le mutex
        esp_task_wdt_reset();                 // Reset WDT pour indiquer que la tâche fonctionne
    }
}

static void task_config_ap(void *pvParameters)
{
    ESP_LOGW(TAG, "Starting CONFIG AP task...");
global_mode_config = 0; // reset le flag pour ne pas relancer à chaque reboot
    nvs_handle_t handle;
    if (nvs_open("config", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "config_mode", 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
    start_config_ap();

    /* 
       On ne quitte pas la task.
       Le serveur HTTP tourne en arrière-plan.
       On peut juste laisser la task vivante.
    */
   
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}



// ----------------------------------------------------------------------
// ------------------- Tâche Wi-Fi et publication MQTT -----------------
// ----------------------------------------------------------------------
/**
 * @brief Tâche qui initialise le Wi-Fi et MQTT,
 *        puis publie périodiquement les compteurs sur le broker MQTT.
 *
 * @param pv : argument passé à la tâche (non utilisé ici)
 */
void task_mqtt(void *pv)
{
    wifi_init();  // Initialise le Wi-Fi et attend la connexion
    mqtt_init();  // Initialise le client MQTT

    char payload[128];           // Buffer pour le message JSON

    esp_task_wdt_add(NULL);      // Ajoute cette tâche au WDT

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_PERIOD_MS)); // Attente de 5 minutes

        xSemaphoreTake(counter_mutex, portMAX_DELAY);     // Accès exclusif aux compteurs

        // Prépare la payload JSON avec les valeurs actuelles
        snprintf(payload, sizeof(payload),
            "{\"c0\":%lu,\"c1\":%lu,\"c2\":%lu,\"c3\":%lu,\"c4\":%lu}",
            counters[0], counters[1], counters[2], counters[3], counters[4]);

        xSemaphoreGive(counter_mutex);                     // Libère le mutex

        mqtt_publish("energie/compteurs", payload);       // Publie sur le topic MQTT
        esp_task_wdt_reset();                              // Reset WDT pour indiquer que la tâche fonctionne
    }
}

// ----------------------------------------------------------------------
// ------------------- Fonction principale -------------------------------
// ----------------------------------------------------------------------
/**
 * @brief Point d'entrée principal du programme.
 *        Initialise la NVS, les GPIO, crée le mutex et lance les tâches sur les cœurs ESP32.
 */
void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Main_APP start");

    counter_mutex = xSemaphoreCreateMutex(); // Crée le mutex pour protéger les compteurs
    ESP_LOGI(TAG, "global_mode_config = %d", global_mode_config);
    nvs_init_and_load();                     // Initialise la NVS et charge les compteurs
    ESP_LOGI(TAG, "NVS_Init Done");
    ESP_LOGI(TAG, "global_mode_config = %d", global_mode_config);

    gpio_init_pulses();                      // Configure les GPIO pour les impulsions
    ESP_LOGI(TAG, "GPIO_Init Done");

   // Crée la tâche de comptage sur le Core 1
    xTaskCreatePinnedToCore(
        task_counter,
        "task_counter",
        4096,
        NULL,
        10,
        NULL,
        1);               // Core 1
    // Crée la tâche boot/config sur le Core 0  
    xTaskCreatePinnedToCore(
        task_boot_button,
        "task_boot_button",
        2048,
        NULL,
        4,
        NULL,
        0);
    // Crée la tâche MQTT sur le Core 0
    if(global_mode_config == 0) {
        ESP_LOGI(TAG, "Mode normal : lancement tâche MQTT"); // Log mode normal 
        xTaskCreatePinnedToCore(
            task_mqtt,        // Fonction de la tâche
            "task_mqtt",      // Nom de la tâche
            8192,             // Taille de la stack
            NULL,             // Paramètre passé à la tâche
            5,                // Priorité
            NULL,             // Handle de tâche (pas utilisé)
            0);               // Core 0
    }else{
        ESP_LOGI(TAG, "Mode AP : lancement tâche CONFIG AP"); // Log mode AP 
        xTaskCreatePinnedToCore(
            task_config_ap,        // Fonction de la tâche
            "task_config_ap",      // Nom de la tâche
            8192,             // Taille de la stack
            NULL,             // Paramètre passé à la tâche
            5,                // Priorité
            NULL,             // Handle de tâche (pas utilisé)
            0);               // Core 0
    }
}
