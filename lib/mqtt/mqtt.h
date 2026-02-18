#ifndef MQTT_H
#define MQTT_H
#include "config.h"
/**
 * @file mqtt.h
 * @brief Header pour le module MQTT ESP32.
 *
 * Ce module fournit des fonctions pour :
 *  - Initialiser le client MQTT et se connecter au broker
 *  - Publier des messages sur un topic MQTT
 *
 * Usage typique :
 * 1. Appeler mqtt_init() après la connexion Wi-Fi
 * 2. Appeler mqtt_publish(topic, payload) pour envoyer des messages (ex: JSON)
 */

extern char mqtt_names[NB_COUNTERS][32]; // tableau de noms pour MQTT

/**
 * @brief Initialise le client MQTT et démarre la connexion au broker.
 *
 * Configure l'adresse du broker, les identifiants et enregistre le gestionnaire
 * d'événements MQTT.
 */
void mqtt_init(void);

/**
 * @brief Publie un message sur un topic MQTT.
 *
 * @param topic   Topic MQTT sur lequel publier (ex: "energie/compteurs")
 * @param payload Contenu du message (ex: JSON avec valeurs de compteurs)
 *
 * La publication se fait avec QoS = 1 (au moins une fois) et Retain = 0 (pas conservé par le broker)
 */
void mqtt_publish(const char *topic, const char *payload);
void mqtt_publish_config(const char *topic, const char *payload); // Publication de la configuration MQTT (ex: noms des compteurs)   

#endif // MQTT_H
