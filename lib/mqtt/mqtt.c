#include "mqtt_client.h" // ESP-IDF : fonctions MQTT client
#include "mqtt.h"        // Header du module MQTT personnalisé
#include "esp_log.h"        // ESP-IDF : fonctions de logging
#include <string.h>       // Pour les fonctions de manipulation de chaînes (ex: strlen)
#include <stdlib.h>      // Pour les fonctions de conversion (ex: atoi)
#include <stdio.h>     // Pour les fonctions de formatage (ex: sprintf)
#include "gpio_pulse.h"    // Pour accéder au tableau global counters
#include "storage.h"       // Pour les fonctions de stockage NVS (sauvegarde des compteurs)
#include "config.h"
// Handle global du client MQTT
static esp_mqtt_client_handle_t client;

static const char *TAG = "MQTT_HANDLER";  //Identifiant des message log de la lib pour faciliter le debug      


// Définition réelle (allocation mémoire)


/**
 * @brief Traite les événements MQTT reçus par le client.
 *
 * Cette fonction est appelée automatiquement par l'ESP-IDF
 * lorsqu’un événement MQTT survient (connexion, message reçu, erreur, etc.).
 *
 * Ici, seuls les événements contenant des données (MQTT_EVENT_DATA)
 * sont traités pour interpréter des commandes entrantes.
 *
 * @param handler_args Arguments utilisateur (non utilisés ici)
 * @param base         Base de l'événement
 * @param event_id     Identifiant de l'événement MQTT
 * @param event_data   Pointeur vers la structure contenant les données MQTT
 */

static void mqtt_event_handler(void *handler_args,
                                esp_event_base_t base,
                                int32_t event_id,
                                void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;           // Conversion générique vers structure MQTT

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connecté au broker");
            char topic[128];
            char json[512];
            esp_mqtt_client_publish(client, "energie/status", "connected", 0, 1, 0); // Publie un message de statut à la connexion
            for(int i=0; i<NB_COUNTERS; i++){
                snprintf(topic, sizeof(topic),"homeassistant/sensor/energie/%s/config",mqtt_names[i]); // Topic gDiscovery Home Assistant  
                snprintf(json, sizeof(json),
                    "{\"name\": \"%s\","
                    "\"state_topic\": \"energie/%s\","
                    "\"unit_of_measurement\": \"Wh\","
                    "\"device_class\": \"energy\","
                    "\"state_class\": \"total_increasing\","
                    "\"unique_id\": \"%s_%s\","
                    "\"device\": {"
                    "  \"identifiers\": [\"%s_%s\"],"
                    "  \"name\": \"%s%s\","
                    "  \"manufacturer\": \"DIY\","
                    "  \"model\": \"ESP32 Energy\"}}",
                    mqtt_names[i],
                    mqtt_names[i],
                    DEVICE_NAME,mqtt_names[i],
                    DEVICE_NAME,mqtt_names[i],
                    DEVICE_NAME,mqtt_names[i]);
                mqtt_publish_config(topic, json);     
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT déconnecté du broker");
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Erreur MQTT");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Message reçu");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;

        default:
            ESP_LOGI(TAG, "Événement MQTT non traité : %d", event->event_id);
            break;
    }
}





/**
 * @brief Initialise le client MQTT et se connecte au broker.
 *
 * Cette fonction :
 *  - Configure le broker, le nom d'utilisateur et le mot de passe
 *  - Initialise le client MQTT
 *  - Enregistre l'event handler
 *  - Démarre le client
 */

void mqtt_init(void)
{
    char uri_server[256]; 
    snprintf(uri_server, sizeof(uri_server), "mqtt://%s:%s", mqtt_Server, mqtt_port);
    ESP_LOGI(TAG, "Configuration MQTT sans auth : uri=%s user=%s Pass=%s",uri_server, mqtt_user, mqtt_pass); // Log de la configuration utilisée     
    esp_mqtt_client_config_t mqtt_cfg = {                         // Structure de configuration MQTT
        .broker.address.uri = uri_server,                  // URI du broker MQTT
        .credentials.username = NULL ,                      // Authentification par defaut sans User NULL
        .credentials.authentication.password = NULL,        // PASSWORD NULL
    };

   if( (strlen(mqtt_user)>0) && (strlen(mqtt_pass)>0))   // Test si l'user et password du MQTT sont non null
       {  
        ESP_LOGI(TAG, "Configuration MQTT avec auth : uri=%s:%s user=%s Pass=%s",mqtt_Server, mqtt_port, mqtt_user, mqtt_pass); // Log de la configuration utilisée     

        mqtt_cfg.credentials.username = mqtt_user;
        mqtt_cfg.credentials.authentication.password = mqtt_pass;
    }

    client = esp_mqtt_client_init(&mqtt_cfg);                     // Création du client MQTT

    esp_mqtt_client_register_event(client,                        // Enregistrement du handler
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);

    esp_mqtt_client_start(client);                                 // Démarrage de la connexion MQTT
}

/**
 * @brief Publie un message MQTT.
 *
 * Envoie un message vers le broker avec QoS 1.
 *
 * @param topic   Topic MQTT cible
 * @param payload Contenu du message à transmettre
 */
void mqtt_publish(const char *topic, const char *payload)
{
    ESP_LOGI(TAG, "Publication MQTT : topic=%s payload=%s", topic, payload); // Log de la publication MQTT

    esp_mqtt_client_publish(client,   // Client MQTT actif
                            topic,    // Topic de destination
                            payload,  // Message à envoyer
                            0,        // Longueur auto-détectée
                            1,        // QoS 1 (au moins une fois)
                            0);       // 0 Retain désactivé
}

void mqtt_publish_config(const char *topic, const char *payload)
{
    ESP_LOGI(TAG, "Publication MQTT : topic=%s payload=%s", topic, payload); // Log de la publication MQTT

    esp_mqtt_client_publish(client,   // Client MQTT actif
                            topic,    // Topic de destination
                            payload,  // Message à envoyer
                            0,        // Longueur auto-détectée
                            1,        // QoS 1 (au moins une fois)
                            1);       // Retain activé pour les messages de configuration
    }