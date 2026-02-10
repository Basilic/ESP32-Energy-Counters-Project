#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file config.h
 * @brief Fichier de configuration globale pour le projet ESP32.
 *
 * Contient :
 *  - Paramètres Wi-Fi
 *  - Constantes pour MQTT et timing
 *  - Paramètres de debounce pour les entrées de compteur
 *  - Définition des GPIO utilisés pour les compteurs
 *
 * Usage :
 *  - Inclus dans tous les modules qui ont besoin de ces constantes
 *  - Garantit que toutes les tâches et modules utilisent les mêmes valeurs
 */

#include "driver/gpio.h" // Inclusion des fonctions de gestion des GPIO fournies par ESP-IDF

// --------------------- Section Wi-Fi ---------------------
#define WIFI_SSID "TON_SSID"       // Nom du réseau Wi-Fi
#define WIFI_PASS "TON_PASSWORD"   // Mot de passe du réseau Wi-Fi
#define WIFI_CONNECTED_BIT BIT0    // Bit utilisé dans le EventGroup pour signaler la connexion Wi-Fi

// Section MQTT dans config.h
#define MQTT_BROKER_URI  "mqtt://192.168.1.10"  // Adresse du broker MQTT
#define MQTT_USERNAME    "user"                 // Nom d'utilisateur MQTT
#define MQTT_PASSWORD    "pass"                 // Mot de passe MQTT


// --------------------- Section timing et debounce ---------------------
#define DEBOUNCE_US 100000                 // Durée de l'anti-rebond pour les entrées GPIO (100 ms)
#define MQTT_PUBLISH_PERIOD_MS (5 * 60 * 1000)  // Période de publication MQTT en millisecondes (5 minutes)

// --------------------- Section compteurs ---------------------
#define NB_COUNTERS 5   // Nombre de compteurs d'impulsions
// Tableau contenant les GPIO utilisés pour chaque compteur
// Chaque index correspond à un compteur physique
static const gpio_num_t pulse_pins[NB_COUNTERS] = {
    GPIO_NUM_18, // Compteur 0 sur GPIO 18
    GPIO_NUM_19, // Compteur 1 sur GPIO 19
    GPIO_NUM_21, // Compteur 2 sur GPIO 21
    GPIO_NUM_22, // Compteur 3 sur GPIO 22
    GPIO_NUM_23  // Compteur 4 sur GPIO 23
};

#endif // CONFIG_H
