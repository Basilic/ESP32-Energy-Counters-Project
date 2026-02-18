/**
 * @file storage.c
 * @brief Module de gestion de la mémoire non-volatile (NVS) pour stocker les paramètres de configuration.
 *
 * Ce module utilise la mémoire non-volatile (NVS) pour sauvegarder et charger les paramètres de configuration du système, notamment :
 * - Les compteurs utilisés par le système
 * - La configuration Wi-Fi (SSID et mot de passe)
 * - La configuration MQTT (URI du broker, port, nom d'utilisateur et mot de passe)
 * - Le mode de configuration actuel (normal ou AP)
 *
 * Il fournit des fonctions pour initialiser la mémoire NVS, charger les paramètres à partir de celle-ci et sauvegarder les modifications.
 */

#include "storage.h"         // Header du module storage pour les prototypes
#include "nvs_flash.h"       // Fonctions NVS pour initialiser la mémoire flash
#include "nvs.h"             // Fonctions NVS pour lire/écrire des valeurs
#include "esp_log.h"         // Fonctions ESP_LOG pour debug
#include "gpio_pulse.h"      // Pour accéder au tableau global counters
#include "config.h"          // Pour NB_COUNTERS et global_mode_config  

static const char *TAG = "STORAGE"; // Tag pour les logs du module storage

char wifi_ssid[32] = {0}; // SSID Wi-Fi
char wifi_pass[64] = {0}; // MQTT configuration
char mqtt_names[NB_COUNTERS][32] = { 
    "compteur0",
    "compteur1",
    "compteur2",
    "compteur3",
    "compteur4"
}; // Noms des compteurs pour MQTT, chargés depuis NVS ou par défaut
uint8_t global_mode_config = 1; // Mode de configuration (0 = normal, 1 = AP)    

char mqtt_Server[64]= {"192.168.1.1"} ;   // URI du broker MQTT
char mqtt_user[32]= {0}  ;               // Nom d'utilisateur MQTT
char mqtt_pass[32] = {0};                // Password MQTT
char mqtt_port[8] = {"1883"}; // Port du server MQTT

/**
 * @brief Initialise la mémoire NVS et charge les paramètres de configuration.
 *
 * Cette fonction effectue plusieurs tâches :
 * 1. Initialise la mémoire NVS en gérant les erreurs courantes comme l'absence de pages libres ou une nouvelle version incompatibilité.
 * 2. Charge les compteurs depuis la mémoire NVS, initialisant ceux qui ne sont pas trouvés à zéro.
 * 3. Charge le SSID et le mot de passe Wi-Fi depuis la mémoire NVS, utilisant des valeurs par défaut si ces paramètres ne sont pas trouvés.
 * 4. Charge les paramètres MQTT (URI du broker, port, nom d'utilisateur et mot de passe) depuis la mémoire NVS, utilisant des valeurs par défaut si nécessaire.
 * 5. Charge le mode de configuration actuel (normal ou AP) depuis la mémoire NVS, initialisant à 0 (mode normal) si ce paramètre n'est pas trouvé.
 *
 * Cette fonction est appelée au démarrage du système pour s'assurer que tous les paramètres sont correctement chargés et disponibles.
 */
void nvs_init_and_load(void)
{
    // --- Initialisation NVS ---
    esp_err_t ret = nvs_flash_init(); // Initialise la NVS
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) // Si la NVS est corrompue ou incompatible, on efface et réinitialise
    {
        ESP_ERROR_CHECK(nvs_flash_erase()); // Efface la NVS
        ret = nvs_flash_init(); // Réinitialise la NVS après effacement
    }
    ESP_ERROR_CHECK(ret); // Vérifie que l'initialisation a réussi

    // --- Chargement des compteurs ---
    nvs_handle_t counters_handle; //    Handle pour accéder à la NVS des compteurs
    ret = nvs_open("counters", NVS_READWRITE, &counters_handle); // Ouvre la NVS "counters" en mode lecture/écriture
    if (ret != ESP_OK)  // Si l'ouverture échoue, on log une erreur et on initialise les compteurs à 0
    {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS counters"); // Log d'erreur
    } 
    else // Si l'ouverture réussit, on lit les compteurs et les noms MQTT
    {
        for (int i = 0; i < NB_COUNTERS; i++) { // Pour chaque compteur
            char key[8]; // Clé pour lire le compteur (ex : "c0", "c1", etc.)
            snprintf(key, sizeof(key), "c%d", i); // Formate la clé pour le compteur i
            uint32_t value = 0; // Variable pour stocker la valeur lue de la NVS
            ret = nvs_get_u32(counters_handle, key, &value); // Tente de lire la valeur du compteur i depuis la NVS
            if (ret == ESP_OK) // Si la lecture réussit, on stocke la valeur dans le tableau global counters
            { 
                counters[i] = value;// Stocke la valeur lue dans le tableau global counters
            } 
            else if (ret == ESP_ERR_NVS_NOT_FOUND) // Si la clé n'est pas trouvée dans la NVS, on initialise le compteur à 0
            {
                counters[i] = 0; // Initialise à 0 si non trouvé
            } 
            else // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le compteur à 0
            {
                ESP_LOGW(TAG, "Erreur lecture NVS compteur %d", i);// Log d'avertissement
                counters[i] = 0; // Initialise à 0 en cas d'erreur
            }

            // --- Lecture des noms MQTT ---
            char mqtt_key[8]; // Clé pour lire le nom MQTT (ex : "m0", "m1", etc.)
            snprintf(mqtt_key, sizeof(mqtt_key), "m%d", i); // Formate la clé pour le nom MQTT du compteur i
            size_t len = sizeof(mqtt_names[i]); // Taille du buffer pour lire le nom MQTT
            ret = nvs_get_str(counters_handle, mqtt_key, mqtt_names[i], &len); // Tente de lire le nom MQTT depuis la NVS
            if (ret == ESP_ERR_NVS_NOT_FOUND) // Si la clé n'est pas trouvée dans la NVS, on initialise le nom MQTT à une valeur par défaut (ex : "compteur0", "compteur1", etc.)
            { 
                mqtt_names[i][0] = ' '; // Vide si non trouvé
            }
            else if (ret != ESP_OK)  // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le nom MQTT à une valeur par défaut
            {
                ESP_LOGW(TAG, "Erreur lecture NVS nom MQTT %d", i); // Log d'avertissement
                mqtt_names[i][0] = ' '; // Vide en cas d'erreur
            }
        }
        nvs_close(counters_handle); // Ferme la NVS après lecture
    }

    // --- Chargement du Wi-Fi ---
    nvs_handle_t wifi_handle; // Handle pour accéder à la NVS du Wi-Fi
    ret = nvs_open("wifi", NVS_READWRITE, &wifi_handle); // Ouvre la NVS "wifi" en mode lecture/écriture
    if (ret == ESP_OK) // Si l'ouverture réussit, on lit le SSID et le mot de passe Wi-Fi
    {  
        size_t len_ssid = sizeof(wifi_ssid); // Taille du buffer pour le SSID
        size_t len_pass = sizeof(wifi_pass); // Taille du buffer pour le mot de passe

        ret = nvs_get_str(wifi_handle, "ssid", wifi_ssid, &len_ssid); // Tente de lire le SSID depuis la NVS
        if (ret == ESP_ERR_NVS_NOT_FOUND)  // Si la clé n'est pas trouvée dans la NVS, on initialise le SSID à une valeur par défaut
        {  
            strcpy(wifi_ssid, "TEST_Wifi"); // Valeur par défaut si non trouvé
        } 
        else if (ret != ESP_OK) //  Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le SSID à une valeur par défaut
        {
            ESP_LOGW(TAG, "Erreur lecture NVS SSID"); // Log d'avertissement
            strcpy(wifi_ssid, "TEST_Wifi"); // Valeur par défaut en cas d'erreur
        }

        ret = nvs_get_str(wifi_handle, "pass", wifi_pass, &len_pass);
        if (ret == ESP_ERR_NVS_NOT_FOUND)  // Si la clé n'est pas trouvée dans la NVS, on initialise le mot de passe à une valeur par défaut
        {
            strcpy(wifi_pass, "TEST_Wifi"); // Valeur par défaut si non trouvé
        }
        else if (ret != ESP_OK) // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le mot de passe à une valeur par défaut
        {
            ESP_LOGW(TAG, "Erreur lecture NVS password"); // Log d'avertissement
            strcpy(wifi_pass, "TEST_Wifi"); // Valeur par défaut en cas d'erreur
        }

        nvs_close(wifi_handle);// Ferme la NVS après lecture
    } 
    else // Si l'ouverture échoue, on log une erreur et on initialise le SSID et le mot de passe à des valeurs par défaut
    {
        ESP_LOGW(TAG, "Impossible d'ouvrir NVS pour Wi-Fi"); // Log d'avertissement
        strcpy(wifi_ssid, "TEST_Wifi"); // Valeur par défaut pour le SSID
        strcpy(wifi_pass, "TEST_Wifi"); // Valeur par défaut pour le mot de passe
    }

    // --- Chargement du MQTT ---
    nvs_handle_t mqtt_handle; // Handle pour accéder à la NVS du MQTT
    ret = nvs_open("mqtt", NVS_READWRITE, &mqtt_handle); // Ouvre la NVS "mqtt" en mode lecture/écriture
    if (ret == ESP_OK) // Si l'ouverture réussit, on lit la configuration MQTT (URI du broker, port, nom d'utilisateur, mot de passe)
    { 
        size_t len_mqtt_serv = sizeof(mqtt_Server); // Taille du buffer pour l'URI du broker
        size_t len_mqtt_user = sizeof(mqtt_user); // Taille du buffer pour le nom d'utilisateur
        size_t len_mqtt_pass = sizeof(mqtt_pass); // Taille du buffer pour le mot de passe
        size_t len_mqtt_port = sizeof(mqtt_port); // Taille du buffer pour le port

        ret = nvs_get_str(mqtt_handle, "mqtt_server", mqtt_Server, &len_mqtt_serv); // Tente de lire l'URI du broker depuis la NVS
        if (ret == ESP_ERR_NVS_NOT_FOUND) // Si la clé n'est pas trouvée dans la NVS, on initialise l'URI du broker à une valeur par défaut
        {
            strcpy(mqtt_Server, "192.168.1.1");//   Valeur par défaut si non trouvé
        } 
        else if (ret != ESP_OK)  // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise l'URI du broker à une valeur par défaut
        {
            ESP_LOGW(TAG, "Erreur lecture NVS mqtt_server"); // Log d'avertissement
            strcpy(mqtt_Server, "192.168.1.1"); // Valeur par défaut en cas d'erreur
        }

        ret = nvs_get_str(mqtt_handle, "mqtt_port", mqtt_port, &len_mqtt_port);// Tente de lire le port MQTT depuis la NVS
        if (ret == ESP_ERR_NVS_NOT_FOUND) // Si la clé n'est pas trouvée dans la NVS, on initialise le port MQTT à une valeur par défaut
        {
            strcpy(mqtt_port, "1883");// Valeur par défaut si non trouvé
        }
        else if (ret != ESP_OK)  // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le port MQTT à une valeur par défaut
        {
            ESP_LOGW(TAG, "Erreur lecture NVS mqtt_port"); // Log d'avertissement
            strcpy(mqtt_port, "1883"); // Valeur par défaut en cas d'erreur
        }

        ret = nvs_get_str(mqtt_handle, "mqtt_user", mqtt_user, &len_mqtt_user); // Tente de lire le nom d'utilisateur MQTT depuis la NVS
        if (ret == ESP_ERR_NVS_NOT_FOUND) // Si la clé n'est pas trouvée dans la NVS, on initialise le nom d'utilisateur MQTT à une valeur par défaut
        { 
            strcpy(mqtt_user, " "); // Valeur par défaut si non trouvé
        }
        else if (ret != ESP_OK) // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le nom d'utilisateur MQTT à une valeur par défaut
        { 
            ESP_LOGW(TAG, "Erreur lecture NVS mqtt_user"); // Log d'avertissement
            strcpy(mqtt_user, " "); // Valeur par défaut en cas d'erreur
        }
        
        ret = nvs_get_str(mqtt_handle, "mqtt_pass", mqtt_pass, &len_mqtt_pass);// Tente de lire le mot de passe MQTT depuis la NVS
        if (ret == ESP_ERR_NVS_NOT_FOUND) // Si la clé n'est pas trouvée dans la NVS, on initialise le mot de passe MQTT à une valeur par défaut
        {
            strcpy(mqtt_pass, " "); // Valeur par défaut si non trouvé
        } 
        else if (ret != ESP_OK) // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le mot de passe MQTT à une valeur par défaut
        {
            ESP_LOGW(TAG, "Erreur lecture NVS password"); // Log d'avertissement
            strcpy(mqtt_pass, " "); //  Valeur par défaut en cas d'erreur
        }
        nvs_close(mqtt_handle);// Ferme la NVS après lecture

    }
    else // Si l'ouverture échoue, on log une erreur et on initialise la configuration MQTT à des valeurs par défaut
    {
        ESP_LOGW(TAG, "Impossible d'ouvrir NVS pour MQTT"); // Log d'avertissement
        strcpy(mqtt_Server,"192.168.1.1"); // Valeur par défaut pour l'URI du broker MQTT
        strcpy(mqtt_port,"1883"); //
        strcpy(mqtt_user, " "); // Valeur par défaut pour le nom d'utilisateur MQTT
        strcpy(mqtt_pass, " "); // Valeur par défaut pour le mot de passe MQTT
    }

    // --- Chargement du mode configuration ---
    nvs_handle_t config_handle; // Handle pour accéder à la NVS de configuration
    ret = nvs_open("config", NVS_READWRITE, &config_handle); // Ouvre la NVS "config" en mode lecture/écriture
    if (ret == ESP_OK) //   Si l'ouverture réussit, on lit le mode de configuration (normal ou AP)
    { 
        ret = nvs_get_u8(config_handle, "config_mode", &global_mode_config); // Tente de lire le mode de configuration depuis la NVS
        if (ret == ESP_OK)  // Si la lecture réussit, on log le mode de configuration récupéré
        {
            ESP_LOGI(TAG, "Mode configuration récupéré depuis NVS : %d", global_mode_config);// Log du mode de configuration
        } 
        else if (ret == ESP_ERR_NVS_NOT_FOUND)  // Si la clé n'est pas trouvée dans la NVS, on initialise le mode de configuration à 0 (normal) et on log une info
        {
            ESP_LOGI(TAG, "Mode configuration non trouvé dans NVS -> mode normal"); // Log d'information
            global_mode_config = 0; // Initialise à 0 (mode normal) si non trouvé
        } 
        else  // Si une autre erreur survient lors de la lecture, on log une erreur et on initialise le mode de configuration à 0 (normal)
        {
            ESP_LOGW(TAG, "Erreur lecture NVS mode_config, mode normal par défaut"); // Log d'avertissement
            global_mode_config = 0; // Initialise à 0 (mode normal) en cas d'erreur
        }
        nvs_close(config_handle); // Ferme la NVS après lecture
    }
    else // Si l'ouverture échoue, on log une erreur et on initialise le mode de configuration à 0 (normal)
    {
        ESP_LOGE(TAG, "Impossible d'ouvrir NVS pour lire mode config"); // Log d'erreur
        global_mode_config = 0; // Initialise à 0 (mode normal) par défaut en cas d'erreur d'ouverture
    }

    ESP_LOGI(TAG, "Compteurs, noms MQTT et configuration Wi-Fi chargés depuis NVS");// Log de fin de chargement
}

/**
 * @brief Sauvegarde la valeur d'un compteur dans la mémoire NVS.
 *
 * Cette fonction permet de sauvegarder une nouvelle valeur pour un compteur spécifique dans la mémoire NVS. Elle effectue les étapes suivantes :
 * 1. Ouvre la mémoire NVS en mode lecture/écriture pour le groupe "counters".
 * 2. Formate une clé unique pour ce compteur (par exemple, "c0", "c1", etc.) en utilisant l'index du compteur.
 * 3. Écrit la nouvelle valeur dans la mémoire NVS sous cette clé.
 * 4. Commite les modifications pour s'assurer que la valeur est bien sauvegardée.
 * 5. Ferme la mémoire NVS après l'écriture.
 *
 * Cette fonction permet de mettre à jour dynamiquement les compteurs utilisés par le système sans avoir à redémarrer le système.
 *
 * @param idx Index du compteur à sauvegarder (doit être compris entre 0 et NB_COUNTERS-1).
 * @param value Nouvelle valeur du compteur à sauvegarder.
 */
void save_counter_to_nvs(int idx, uint32_t value)
{
    nvs_handle_t handle;   // Handle pour accéder à la NVS des compteurs   
    esp_err_t ret = nvs_open("counters", NVS_READWRITE, &handle); // Ouvre la NVS "counters" en mode lecture/écriture
    if (ret != ESP_OK)  // Si l'ouverture échoue, on log une erreur et on retourne
    { 
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS pour écriture compteur %d", idx);// Log d'erreur
        return; // Retourne en cas d'erreur d'ouverture
    }

    char key[8]; // Clé pour le compteur (ex : "c0", "c1", etc.)
    snprintf(key, sizeof(key), "c%d", idx); // Formate la clé pour le compteur idx

    ret = nvs_set_u32(handle, key, value);  // Tente d'écrire la nouvelle valeur du compteur dans la NVS
    if (ret != ESP_OK)  // Si l'écriture échoue, on log une erreur, on ferme la NVS et on retourne
    {
        ESP_LOGE(TAG, "Impossible d'écrire compteur %d", idx); // Log d'erreur
        nvs_close(handle); // Ferme la NVS avant de retourner
        return; // Retourne en cas d'erreur d'écriture
    }

    ret = nvs_commit(handle); // Commite les modifications pour s'assurer que la valeur est bien sauvegardée
    if (ret != ESP_OK)  // Si le commit échoue, on log une erreur
    {
        ESP_LOGE(TAG, "Erreur commit NVS compteur %d", idx); // Log d'erreur
    }

    nvs_close(handle); // Ferme la NVS après l'écriture
    ESP_LOGI(TAG, "Compteur %d sauvegardé : %lu", idx, value);// Log de succès de sauvegarde
}
