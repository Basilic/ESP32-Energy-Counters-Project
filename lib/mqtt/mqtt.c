/**
 * @file mqtt.c
 * @brief Module MQTT pour ESP32.
 *
 * Ce module :
 *  - Initialise le client MQTT
 *  - Permet de publier des messages JSON sur un broker
 *  - Gère les événements MQTT (connexion, reconnexion, erreurs)
 *
 * Usage typique :
 * 1. Appeler mqtt_init() au démarrage après la connexion Wi-Fi
 * 2. Appeler mqtt_publish(topic, payload) pour envoyer les données
 */

#include "mqtt_client.h" // ESP-IDF : fonctions MQTT client
#include "mqtt.h"        // Header du module MQTT personnalisé

// Handle global du client MQTT
static esp_mqtt_client_handle_t client;

/**
 * @brief Gestionnaire d'événements MQTT.
 *
 * Cette fonction est appelée automatiquement par l'ESP-IDF
 * pour tout événement lié au client MQTT (connexion, déconnexion, message reçu...).
 *
 * @param handler_args : arguments passés lors de l'enregistrement de l'handler
 * @param base : base de l'événement (ESP_EVENT_BASE)
 * @param event_id : identifiant de l'événement
 * @param event_data : données associées à l'événement
 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    // Optionnel : logs, debug, reconnexion automatique
    // Pour le moment, nous n'utilisons pas ces événements
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
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "MQTT_BROKER_URI",  // Adresse du broker MQTT
        .credentials.username = "MQTT_USERNAME",               // Nom d'utilisateur MQTT
        .credentials.authentication.password = "MQTT_PASSWORD", // Mot de passe MQTT
    };

    // Initialise le client MQTT avec la configuration
    client = esp_mqtt_client_init(&mqtt_cfg);

    // Enregistre le gestionnaire d'événements pour tous les événements MQTT
    esp_mqtt_client_register_event(client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);

    // Démarre le client MQTT (connexion au broker)
    esp_mqtt_client_start(client);
}

/**
 * @brief Publie un message MQTT sur le topic spécifié.
 *
 * @param topic   Topic MQTT (ex: "energie/compteurs")
 * @param payload Contenu du message (ex: JSON avec compteurs)
 *
 * Cette fonction :
 *  - Utilise le client MQTT initialisé
 *  - QoS = 1 (au moins une fois)
 *  - Retain = 0 (le broker ne conserve pas le message)
 */
void mqtt_publish(const char *topic, const char *payload)
{
    esp_mqtt_client_publish(client,
                            topic,
                            payload,
                            0,  // longueur du message, 0 = auto
                            1,  // QoS 1 (assurance livraison)
                            0); // Retain 0 (pas conservé par le broker)
}
