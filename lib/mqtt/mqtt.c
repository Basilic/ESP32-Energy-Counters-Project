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

    if (event_id != MQTT_EVENT_DATA)                      // Ignore tous les événements sauf réception de données
        return;                                           // Quitte si ce n’est pas un message entrant

    char data[event->data_len + 1];                       // Buffer local pour stocker la donnée reçue (+1 pour '\0')
    memcpy(data, event->data, event->data_len);           // Copie des données MQTT dans le buffer local
    data[event->data_len] = '\0';                         // Ajout du caractère de fin de chaîne

    ESP_LOGI(TAG, "Commande reçue : %s", data);           // Affiche la commande reçue en log

    if (strncmp(data, "Force_Compteur[", 15) == 0)        // Vérifie si la commande est de type Force_Compteur
    {
        int index;                                        // Index du compteur à modifier
        uint32_t value;                                   // Nouvelle valeur du compteur

        if (sscanf(data, "Force_Compteur[%d]=%lu", &index, &value) == 2) // Extraction index + valeur
        {
            counters[index] = value;                       // Mise à jour du compteur en RAM
            save_counter_to_nvs(index, value);             // Sauvegarde persistante en NVS

            ESP_LOGI(TAG, "Compteur %d forcé à %lu", index, value); // Confirmation en log
        }
        else
        {
            ESP_LOGW(TAG, "Format invalide Force_Compteur"); // Log si la commande est mal formée
        }
    }
    else if (strncmp(data, "Read_Compteur[", 14) == 0)     // Vérifie si la commande est de type lecture compteur
    {
        int index;                                         // Index du compteur demandé

        if (sscanf(data, "Read_Compteur[%d]", &index) == 1) // Extraction de l’index
        {
            char topic[32];                                 // Buffer pour le topic MQTT
            char payload[32];                               // Buffer pour la valeur envoyée

            snprintf(topic, sizeof(topic), "compteur/%d", index); // Génère le topic dynamique
            snprintf(payload, sizeof(payload), "%lu", counters[index]); // Convertit la valeur en texte

            esp_mqtt_client_publish(client, topic, payload, 0, 1, 0); // Publication MQTT QoS 1

            ESP_LOGI(TAG, "Compteur %d envoyé : %lu", index, counters[index]); // Confirmation en log
        }
        else
        {
            ESP_LOGW(TAG, "Format invalide Read_Compteur"); // Log si format incorrect
        }
    }
    else if (strcmp(data, "Init_All") == 0)                // Vérifie si la commande est une réinitialisation globale
    {
        for (int i = 0; i < NB_COUNTERS; i++)              // Parcourt tous les compteurs
        {
            counters[i] = 0;                               // Remet le compteur à zéro en RAM
            save_counter_to_nvs(i, 0);                      // Sauvegarde la valeur 0 en NVS
        }

        ESP_LOGI(TAG, "Tous les compteurs réinitialisés à 0"); // Confirmation en log
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
    esp_mqtt_client_config_t mqtt_cfg = {                         // Structure de configuration MQTT
        .broker.address.uri = uri_server,                  // URI du broker MQTT
        .credentials.username = NULL ,                      // Authentification par defaut sans User NULL
        .credentials.authentication.password = NULL,        // PASSWORD NULL
    };
    
   if( (strlen(mqtt_user)>0) && (strlen(mqtt_pass)>0))   // Test si l'user et password du MQTT sont non null
       {  
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
    esp_mqtt_client_publish(client,   // Client MQTT actif
                            topic,    // Topic de destination
                            payload,  // Message à envoyer
                            0,        // Longueur auto-détectée
                            1,        // QoS 1 (au moins une fois)
                            0);       // Retain désactivé
}