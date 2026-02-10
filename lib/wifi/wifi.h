#ifndef WIFI_H
#define WIFI_H

void wifi_init(void);

#endif
#ifndef WIFI_H
#define WIFI_H

/**
 * @file wifi.h
 * @brief Header pour le module Wi-Fi ESP32.
 *        Fournit la fonction d'initialisation du Wi-Fi en mode station
 *        et gère la connexion automatique au réseau configuré.
 *
 * Usage typique :
 * 1. Appeler wifi_init() au démarrage de l'application.
 *    Cette fonction :
 *      - Initialise la pile réseau et la NVS si nécessaire
 *      - Configure le Wi-Fi avec SSID et mot de passe
 *      - Attend la connexion avant de retourner
 */

/**
 * @brief Initialise le Wi-Fi en mode station et attend la connexion.
 *
 * Configure le SSID et le mot de passe définis dans config.h
 * Bloque jusqu'à ce que l'ESP32 obtienne une adresse IP valide.
 */
void wifi_init(void);

#endif // WIFI_H
