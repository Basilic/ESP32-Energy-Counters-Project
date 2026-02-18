/**
 * @file lib/mqtt/mqtt.c
 *
 * Ce fichier gère les communications MQTT pour l'application.
 * Il inclut la configuration du client MQTT, le traitement des événements et la publication de messages.
 *
 * Fonctions principales :
 * - mqtt_event_handler : Traite les événements MQTT.
 * - mqtt_init : Initialise le client MQTT.
 * - mqtt_publish : Publie un message MQTT.
 * - mqtt_publish_config : Publie un message de configuration MQTT.
 */

#include "mqtt_client.h" // ESP-IDF : fonctions MQTT client
#include "mqtt.h"        // Header du module MQTT personnalisé
#include "esp_log.h"        // ESP-IDF : fonctions de logging
#include <string.h>       // Pour les fonctions de manipulation de chaînes (ex: strlen)
#include <stdlib.h>      // Pour les fonctions de conversion (ex: atoi)
#include <stdio.h>     // Pour les fonctions de formatage (ex: sprintf)
#include "gpio_pulse.h"    // Pour accéder au tableau global counters
#include "storage.h"       // Pour les fonctions de stockage NVS (sauvegarde des compteurs)
#include "config.h"     // Pour les constantes de configuration (ex: NB_COUNTERS, mqtt_names, etc.)

static esp_mqtt_client_handle_t client; // Handle global du client MQTT

static const char *TAG = "MQTT_HANDLER"; //Identifiant des message log de la lib pour faciliter le debug      

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
    esp_mqtt_event_handle_t event = event_data;  // Conversion générique vers structure MQTT

    switch (event->event_id) {  // Traitement en fonction du type d'événement MQTT

        case MQTT_EVENT_CONNECTED: //    Traite la connexion réussie au broker
            ESP_LOGI(TAG, "MQTT connecté au broker"); // Log de la connexion pour le debug
            char topic[128]; // Buffer pour construire les topics MQTT à publier    
            char json[512]; // Buffer pour construire les messages JSON à publier (ex: configuration Home Assistant)
            esp_mqtt_client_publish(client, "energie/status", "connected", 0, 1, 0); // Publie un message de statut à la connexion
            for(int i=0; i<NB_COUNTERS; i++){
                snprintf(topic, sizeof(topic),"homeassistant/sensor/energie/%s/config",mqtt_names[i]); // Topic Discovery Home Assistant  
                snprintf(json, sizeof(json),
                    "{\"name\": \"%s\","
                    "\"state_topic\": \"energie/%s\","
                    "\"unit_of_measurement\" : \"Wh\","
                    "\"device_class\": \"energy\","
                    "\"state_class\": \"total_increasing\","
                    "\"unique_id\": \"%s_%s\","
                    "\"device\": {"
                    "  \"identifiers\": [\"%s_%s\"],"
                    "  \"name\": \"%s_%s\","
                    "  \"manufacturer\": \"DIY\","
                    "  \"model\": \"ESP32 Energy\"}}",
                    mqtt_names[i],
                    mqtt_names[i],
                    DEVICE_NAME,mqtt_names[i],
                    DEVICE_NAME,mqtt_names[i],
                    DEVICE_NAME,mqtt_names[i]);
                mqtt_publish_config(topic, json);   // Publie la configuration de chaque compteur pour Home Assistant (MQTT Discovery)  
            }
            break; //   Important : ne pas oublier le break pour éviter de traiter les autres cas après une connexion réussie

        case MQTT_EVENT_DISCONNECTED: //    Traite la déconnexion du broker
            ESP_LOGW(TAG, "MQTT déconnecté du broker"); // Log de la déconnexion pour le debug
            break; //   Important : ne pas oublier le break pour éviter de traiter les autres cas après une déconnexion

        case MQTT_EVENT_ERROR: //    Traite les erreurs MQTT
            ESP_LOGE(TAG, "Erreur MQTT"); // Log de l'erreur pour le debug
            break; //   Important : ne pas oublier le break pour éviter de traiter les autres cas après une erreur  

        case MQTT_EVENT_DATA: //    Traite les messages reçus sur les topics MQTT
            ESP_LOGI(TAG, "Message reçu"); // Log de la réception d'un message pour le debug
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic); // Affiche le topic du message reçu pour le debug
            printf("DATA=%.*s\r\n", event->data_len, event->data); // Affiche le contenu du message reçu pour le debug
            break; //   Important : ne pas oublier le break pour éviter de traiter les autres cas après la réception d'un message

        default: //    Cas par défaut pour les événements non traités
            ESP_LOGI(TAG, "Événement MQTT non traité : %d", event->event_id); //    Log des événements MQTT non traités pour le debug
            break; //   Important : ne pas oublier le break pour éviter de traiter les autres cas après un événement non traité
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
    char uri_server[256]; // Buffer pour construire l'URI du broker MQTT (ex: "mqtt://
    snprintf(uri_server, sizeof(uri_server), "mqtt://%s:%s", mqtt_Server, mqtt_port); // Construction de l'URI du broker MQTT à partir des paramètres de configuration
    ESP_LOGI(TAG, "Configuration MQTT sans auth : uri=%s user=%s Pass=%s",uri_server, mqtt_user, mqtt_pass); // Log de la configuration utilisée     
    esp_mqtt_client_config_t mqtt_cfg = {                         // Structure de configuration MQTT
        .broker.address.uri = uri_server,                  // URI du broker MQTT
        .credentials.username = NULL ,                      // Authentification par defaut sans User NULL
        .credentials.authentication.password = NULL,        // PASSWORD NULL
    };

   if( (strlen(mqtt_user)>2) && (strlen(mqtt_pass)>2))   // Test si l'user et password du MQTT sont non null ou juste un espace, si oui on configure le client MQTT avec auth sinon sans auth
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


/**
 * @brief Publie un message de configuration MQTT.
 *
 * Envoie un message de configuration vers le broker avec QoS 1 et retain activé.
 *
 * @param topic   Topic MQTT cible
 * @param payload Contenu du message à transmettre
 */
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