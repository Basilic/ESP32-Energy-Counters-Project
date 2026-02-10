/**
 * @file wifi.c
 * @brief Gestion de la connexion Wi-Fi de l'ESP32 en mode station.
 *        Initialise le Wi-Fi, gère les événements de connexion/déconnexion
 *        et attend que l'ESP32 obtienne une IP avant de continuer.
 */

#include "esp_wifi.h"        // Fonctions de configuration et gestion Wi-Fi
#include "esp_event.h"       // Gestion des événements (Wi-Fi, IP, etc.)
#include "nvs_flash.h"       // Pour initialiser la NVS nécessaire au Wi-Fi
#include "freertos/event_groups.h" // Pour utiliser les Event Groups FreeRTOS
#include "config.h"          // Constantes globales (SSID, password, WIFI_CONNECTED_BIT)

/* Exemple mis dans config.h :
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_SSID "TON_SSID"
#define WIFI_PASS "TON_PASSWORD"
*/

static EventGroupHandle_t wifi_event_group; // Groupe d'événements pour signaler l'état Wi-Fi


/**
 * @brief Gestionnaire d'événements Wi-Fi et IP.
 *
 * Cette fonction est appelée automatiquement par l'ESP-IDF
 * lors de changements d'état Wi-Fi ou IP.
 *
 * @param arg : argument passé à l'handler (NULL ici)
 * @param event_base : type d'événement (WIFI_EVENT ou IP_EVENT)
 * @param event_id : identifiant de l'événement spécifique
 * @param event_data : données associées à l'événement
 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    // Si la station démarre, on se connecte au Wi-Fi
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); 
    }
    // Si déconnexion, on tente une reconnexion automatique
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
    // Si l'ESP32 obtient une IP, on signale que la connexion est établie
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT); // Met à 1 le bit "connecté"
    }
}


/**
 * @brief Initialise le Wi-Fi en mode station et attend la connexion.
 *
 * Étapes :
 * 1. Crée un Event Group pour signaler la connexion.
 * 2. Initialise le réseau et l'interface Wi-Fi par défaut.
 * 3. Configure le Wi-Fi (SSID, mot de passe).
 * 4. Démarre le Wi-Fi.
 * 5. Attend que la connexion soit établie avant de continuer.
 */
void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate(); // Crée le groupe d'événements Wi-Fi

    esp_netif_init();                 // Initialise la pile réseau
    esp_event_loop_create_default();  // Crée la boucle d'événements par défaut
    esp_netif_create_default_wifi_sta(); // Crée l'interface Wi-Fi station par défaut

    // Configuration Wi-Fi par défaut
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);              // Initialise le Wi-Fi avec la config par défaut

    // Enregistre le gestionnaire d'événements pour tous les événements Wi-Fi
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    // Enregistre le gestionnaire d'événements pour l'événement IP obtenu
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    // Configuration de la station Wi-Fi (SSID et mot de passe)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);         // Met le Wi-Fi en mode station
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // Applique la configuration Wi-Fi
    esp_wifi_start();                          // Démarre le Wi-Fi

    // Attente bloquante jusqu'à ce que le Wi-Fi soit connecté (bit mis à 1 par l'event handler)
    xEventGroupWaitBits(wifi_event_group,
                        WIFI_CONNECTED_BIT,  // Bit à attendre
                        pdFALSE,             // Ne pas effacer le bit après lecture
                        pdTRUE,              // Attendre tous les bits (ici un seul)
                        portMAX_DELAY);      // Attente infinie
}
