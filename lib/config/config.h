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

#define DEVICE_NAME "ESP32_Counter" // Nom de l'appareil pour identification MQTT ou logs
#include "driver/gpio.h" // Inclusion des fonctions de gestion des GPIO fournies par ESP-IDF

// --------------------- Section Wi-Fi ---------------------
#define WIFI_CONNECTED_BIT BIT0    // Bit utilisé dans le EventGroup pour signaler la connexion Wi-Fi

// --------------------- Section Wi-Fi CONFIG ---------------------
#define AP_SSID "COUNTER_CONFIG"
//#define AP_PASS "123456789"

// Section MQTT dans config.h
//#define MQTT_BROKER_URI  "mqtt://192.168.1.10"  // Adresse du broker MQTT
//#define MQTT_USERNAME    "user"                 // Nom d'utilisateur MQTT
//#define MQTT_PASSWORD    "pass"                 // Mot de passe MQTT

// Durée appui long bouton BOOT en millisecondes
#define BOOT_LONG_PRESS_TIME_MS 3000

// GPIO du bouton BOOT (ESP32 DevKit = GPIO0)
#define BOOT_BUTTON_GPIO GPIO_NUM_0
// --------------------- Section timing et debounce ---------------------
#define DEBOUNCE_US 20000               // Durée de l'anti-rebond pour les entrées GPIO (20 ms)
#define MQTT_PUBLISH_PERIOD_MS (5 * 60 * 1000)  // Période de publication MQTT en millisecondes (5 minutes)

// --------------------- Section compteurs ---------------------
#define NB_COUNTERS 5   // Nombre de compteurs d'impulsions
// Tableau contenant les GPIO utilisés pour chaque compteur
// Chaque index correspond à un compteur physique
static const gpio_num_t pulse_pins[NB_COUNTERS] = {
    GPIO_NUM_18, // Compteur 0 sur GPIO 18
    GPIO_NUM_19, // Compteur 1 sur GPIO 19
    GPIO_NUM_23, // Compteur 2 sur GPIO 23
    GPIO_NUM_21, // Compteur 3 sur GPIO 21
    GPIO_NUM_22  // Compteur 4 sur GPIO 22
};

extern uint8_t global_mode_config; // Mode de configuration (0 = normal, 1 = AP)
extern uint32_t counters[NB_COUNTERS]; // Tableau global des compteurs d'impulsions, accessible depuis tous les modules pour lecture/écriture des valeurs de comptage
extern char mqtt_names[NB_COUNTERS][32]; // taille adaptée à tes noms
extern char wifi_ssid[32]; // SSID Wi-Fi, accessible globalement pour la configuration et la connexion
extern char wifi_pass[64]; // Mot de passe Wi-Fi, accessible globalement pour la configuration et la connexion
extern char mqtt_Server[64]; // URI du broker MQTT, accessible globalement pour la configuration et la connexion
extern char mqtt_user[32]; // Nom d'utilisateur MQTT, accessible globalement pour la configuration et la connexion
extern char mqtt_pass[32]; // Mot de passe MQTT, accessible globalement pour la configuration et la connexion
extern char mqtt_port[8]; // Port du server MQTT, accessible globalement pour la configuration et la connexion
#endif // CONFIG_H
