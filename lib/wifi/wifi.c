/**
 * @file wifi.c
 * @brief Gestion Wi-Fi STA + Mode AP configuration avec serveur Web.
 *
 * Ce fichier gère la configuration et le fonctionnement du Wi-Fi sur l'ESP32 en mode station (STA) et point d'accès (AP).
 * Il inclut également un serveur web pour configurer les paramètres de connexion Wi-Fi, MQTT et compteurs.
 */

#include "esp_wifi.h"   // API Wi-Fi
#include "esp_event.h"  // Gestion des événements   
#include "nvs_flash.h"      // NVS pour stockage des configs
#include "freertos/event_groups.h"  // Groupes d'événements FreeRTOS
#include "config.h" // Configuration globale (SSID, pass, MQTT, etc.)
#include "esp_log.h"    // Logging ESP-IDF
#include "esp_http_server.h"    // Serveur HTTP pour la configuration
#include "esp_system.h"   // Pour esp_restart()
#include "esp_netif.h"  // Pour esp_netif_init() et esp_netif_create_default_wifi_sta()
#include <string.h>   // Pour memset, memcpy, etc.
#include <stdio.h>  // Pour snprintf
#include <ctype.h>  // Pour isprint
#include <stdlib.h>// Pour malloc, free

static const char *TAG = "WIFI"; // Tag pour les logs

static EventGroupHandle_t wifi_event_group; // Groupe d'événements pour la connexion Wi-Fi
static httpd_handle_t server = NULL; // Handle du serveur HTTP

/* ========================= WIFI STA ========================= */

/**
 * @brief Gère les événements Wi-Fi pour le mode station.
 *
 * Cette fonction est appelée automatiquement par l'ESP-IDF lorsqu'un événement Wi-Fi survient.
 * Elle gère la connexion au réseau, la déconnexion et la réception d'une adresse IP.
 *
 * @param arg Arguments utilisateur (non utilisés ici).
 * @param event_base Base de l'événement.
 * @param event_id Identifiant de l'événement Wi-Fi.
 * @param event_data Pointeur vers les données de l'événement.
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { // Lorsque le Wi-Fi démarre, tente de se connecter au réseau configuré
        esp_wifi_connect(); // Tente de se connecter au réseau Wi-Fi configuré
    }
    else if (event_base == WIFI_EVENT && 
             event_id == WIFI_EVENT_STA_DISCONNECTED) { // Lorsque la station est déconnectée, tente de se reconnecter
        esp_wifi_connect(); // Tente de se reconnecter au réseau Wi-Fi configuré
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) { // Lorsque la station obtient une adresse IP, signale que la connexion est établie
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT); // Signale que la connexion Wi-Fi est établie en définissant le bit WIFI_CONNECTED_BIT
    }
}

/**
 * @brief Initialise le client Wi-Fi en mode station et se connecte au réseau.
 *
 * Cette fonction configure le client Wi-Fi, enregistre les gestionnaires d'événements et démarre la connexion au réseau spécifié.
 */
void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate(); // Crée un groupe d'événements pour gérer la connexion Wi-Fi
    ESP_LOGI(TAG, "Initialisation du Wi-Fi en mode station..."); // Log d'initialisation
    esp_netif_init(); // Initialise la pile réseau (obligatoire avant d'utiliser le Wi-Fi)
    esp_event_loop_create_default(); // Crée une boucle d'événements par défaut pour gérer les événements système
    esp_netif_create_default_wifi_sta(); // Crée une interface réseau par défaut pour le mode station Wi-Fi

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Utilise la configuration par défaut pour l'initialisation du Wi-Fi
    esp_wifi_init(&cfg); // Initialise le Wi-Fi avec la configuration spécifiée

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL); // Enregistre le gestionnaire d'événements pour les événements Wi-Fi
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL); //    Enregistre le gestionnaire d'événements pour les événements IP liés à l'obtention d'une adresse IP

    wifi_config_t wifi_config = {0}; // Initialise la structure de configuration Wi-Fi à zéro
    strncpy((char *)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid)); // Copie le SSID configuré dans la structure de configuration Wi-Fi
    strncpy((char *)wifi_config.sta.password, wifi_pass, sizeof(wifi_config.sta.password)); // Copie le mot de passe configuré dans la structure de configuration Wi-Fi
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // Définit le mode d'authentification minimum pour la connexion (WPA2-PSK)
    ESP_LOGI(TAG, "Configuration Wi-Fi : SSID=%s Pass=%s", wifi_ssid, wifi_pass); // Log de la configuration utilisée

    esp_wifi_set_mode(WIFI_MODE_STA); // Configure le Wi-Fi en mode station
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // Applique la configuration Wi-Fi pour l'interface station
    ESP_LOGI(TAG, "Démarrage du Wi-Fi..."); // Log de démarrage du Wi-Fi
    esp_wifi_start(); // Démarre le Wi-Fi, ce qui déclenchera les événements de connexion
    ESP_LOGI(TAG, "Attente de la connexion Wi-Fi..."); // Log d'attente de la connexion Wi-Fi

    xEventGroupWaitBits(wifi_event_group, //   
                        WIFI_CONNECTED_BIT, // Bit à attendre pour indiquer que la connexion Wi-Fi est établie
                        pdFALSE, // Ne pas effacer les bits après les avoir reçus
                        pdTRUE, // Attendre que tous les bits spécifiés soient définis (dans ce cas, il n'y en a qu'un)
                        portMAX_DELAY); // Attendre indéfiniment jusqu'à ce que la connexion Wi-Fi soit établie
    ESP_LOGI(TAG, "Wi-Fi connecté avec succès !"); // Log de succès de la connexion Wi-Fi
}

/* ========================= MODE AP ========================= */
 
/**
 * @brief Échappe les caractères spéciaux HTML dans une chaîne.
 *
 * Cette fonction remplace certains caractères spéciaux (&, <, >, ", ') par leurs équivalents HTML pour éviter des injections XSS.
 *
 * @param src Chaîne source à échapper.
 * @param dst Buffer de destination pour la chaîne échappée.
 * @param dst_size Taille du buffer de destination.
 */
static void html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t o = 0; // Index de sortie dans dst
    for (size_t i = 0; src && src[i] != '\0' && o + 1 < dst_size; i++) { // Parcourt la chaîne source tant que le caractère n'est pas nul et que le buffer de destination n'est pas plein
        const char *rep = NULL; // Pointeur pour la chaîne de remplacement si un caractère spécial est trouvé
        switch ((unsigned char)src[i]) {
            case '&': rep = "&amp;";  break;
            case '<': rep = "&lt;";   break;
            case '>': rep = "&gt;";   break;
            case '"': rep = "&quot;"; break;
            case '\'':rep = "&#39;";  break;
            default:
                dst[o++] = src[i];
                continue;
        } // Si un caractère spécial est trouvé, rep pointe vers la chaîne de remplacement correspondante
        size_t rlen = strlen(rep); // Longueur de la chaîne de remplacement
        if (o + rlen >= dst_size) break; // Si la chaîne de remplacement ne rentre pas dans le buffer de destination, arrêter l'échappement 
        memcpy(dst + o, rep, rlen); // Copier la chaîne de remplacement dans le buffer de destination
        o += rlen; // Avancer l'index de sortie de la longueur de la chaîne de remplacement
    }
    dst[o] = '\0'; // Terminer la chaîne de destination par un caractère nul
}

/**
 * @brief Génère une réponse HTML pour afficher les paramètres actuels.
 *
 * Cette fonction génère une page web contenant les paramètres de connexion Wi-Fi, MQTT et compteurs actuels.
 *
 * @param req Pointeur vers la requête HTTP.
 */
static esp_err_t config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8"); // Définit le type de contenu de la réponse HTTP à HTML avec encodage UTF-8
    httpd_resp_set_hdr(req, "Cache-Control", "no-store"); // Ajoute un en-tête pour empêcher la mise en cache de la page de configuration

    // Buffers échappés (tailles confortables)
    char esc_ssid[128], esc_pass[128]; // Buffers pour les valeurs échappées à afficher dans le formulaire HTML (tailles adaptées aux champs de configuration)
    char esc_mqserv[256], esc_mquser[128], esc_mqpass[128],esc_mqport[128]; // Buffers pour les valeurs MQTT échappées (tailles adaptées aux champs de configuration MQTT)
    char esc_name[128]; // Buffer pour les noms de compteurs échappés (taille adaptée à la configuration des noms de compteurs)

    html_escape(wifi_ssid,   esc_ssid,  sizeof esc_ssid); // Échappe les caractères spéciaux dans le SSID pour l'affichage HTML
    html_escape(wifi_pass,   esc_pass,  sizeof esc_pass); // Échappe les caractères spéciaux dans le mot de passe Wi-Fi pour l'affichage HTML
    html_escape(mqtt_Server, esc_mqserv,sizeof esc_mqserv); //  Échappe les caractères spéciaux dans le serveur MQTT pour l'affichage HTML
    html_escape(mqtt_user,   esc_mquser,sizeof esc_mquser); //  Échappe les caractères spéciaux dans le nom d'utilisateur MQTT pour l'affichage HTML
    html_escape(mqtt_pass,   esc_mqpass,sizeof esc_mqpass); //  Échappe les caractères spéciaux dans le mot de passe MQTT pour l'affichage HTML
    html_escape(mqtt_port,   esc_mqport,sizeof esc_mqport); //  Échappe les caractères spéciaux dans le port MQTT pour l'affichage HTML

    // Macro pour checker les envois    
    #define SEND(S) do {                              \
        esp_err_t __e = httpd_resp_sendstr_chunk(req, (S)); \
        if (__e != ESP_OK) {                          \
            ESP_LOGE("HTTP", "send failed (%d) at [%s]", __e, (S)); \
            httpd_resp_sendstr_chunk(req, NULL);      \
            return __e;                               \
        }                                             \
    } while(0)

    // Buffer pour composer les lignes (jamais vide)
    char line[512];

    // --- Envoi du header/page ---
    SEND("<!DOCTYPE html>"
         "<html><head>"
         "<meta charset=\"UTF-8\">"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
         "<title>ESP32 Configuration</title>"
         "<style>"
         "body{font-family:sans-serif;margin:16px;max-width:860px}"
         "label{display:block;margin-top:10px}"
         "input{width:100%;max-width:520px;padding:6px;margin:4px 0}"
         "h2{margin-bottom:6px} h3{margin-top:18px}"
         "button{padding:8px 14px;margin-top:12px}"
         "</style>"
         "</head><body>"
         "<h2>Configuration ESP32</h2>"
         "<form method=\"POST\" action=\"/save\">"

         "<h3>Wi‑Fi</h3>"
         "<label>SSID</label>"
    );

    // SSID
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"ssid\" value=\"%s\"><br><br>",
             esc_ssid);
    SEND(line);

    // Pass Wi-Fi
    SEND("<label>Mot de passe</label>");
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"pass\" value=\"%s\"><br><br>",
             esc_pass);
    SEND(line);

    // --- MQTT --- 
    SEND("<h3>MQTT</h3>"
         "<label>Serveur MQTT</label>");
    snprintf(line, sizeof line,
        "<input type=\"text\" name=\"mqtt_server\" "
        "placeholder=\"mqtt://192.168.1.1\" value=\"%s\">:",
         esc_mqserv);
    SEND(line);
    snprintf(line, sizeof line,
        "<input type=\"text\" name=\"mqtt_port\" "
        "placeholder=\"1883\" value=\"%s\"><br><br>",
         esc_mqport);
    SEND(line);

    SEND("<label>Utilisateur MQTT</label>");
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"mqtt_user\" value=\"%s\"><br><br>",
             esc_mquser);
    SEND(line);

    SEND("<label>Mot de passe MQTT</label>");
    snprintf(line, sizeof line,
             "<input type=\"text\" name=\"mqtt_pass\" value=\"%s\"><br><br>",
             esc_mqpass);
    SEND(line);

    // --- Compteurs ---
    SEND("<h3>Compteurs</h3>");
    for (int i = 0; i < NB_COUNTERS; i++) {
        html_escape(mqtt_names[i], esc_name, sizeof esc_name);
        snprintf(line, sizeof line,
                 "Compteur %d:<br>"
                 "<input type=\"number\" name=\"c%d\" value=\"%lu\"><br>"
                 "Nom:<br>"
                 "<input type=\"text\" name=\"m%d\" value=\"%s\"><br><br>",
                 i + 1,
                 i, (unsigned long)counters[i],
                 i, esc_name);
        SEND(line);
    }

    // Bouton + fin
    SEND("<button type=\"submit\">Enregistrer</button>"
         "</form></body></html>");

    // Fin de la réponse chunked
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;

    #undef SEND
}

/**
 * @brief Décode une chaîne URL-encoded.
 *
 * Cette fonction décode une chaîne encodée en URL (ex: %20 devient espace).
 *
 * @param src Chaîne source à décoder.
 * @param dst Buffer de destination pour la chaîne décodée.
 * @param dst_size Taille du buffer de destination.
 */
static void url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t si = 0, di = 0; // Index de lecture dans src et d'écriture dans dst
    if (!src || !dst || dst_size == 0) return; // Vérifie les pointeurs et la taille du buffer de destination

    while (src[si] != '\0' && di + 1 < dst_size) // Parcourt la chaîne source tant que le caractère n'est pas nul et que le buffer de destination n'est pas plein
    {
        if (src[si] == '%' &&
            src[si+1] && src[si+2] &&
            isxdigit((unsigned char)src[si+1]) &&
            isxdigit((unsigned char)src[si+2])) // Si on trouve un '%' suivi de deux caractères hexadécimaux, on les décode
        {
            char hex[3] = { src[si+1], src[si+2], 0 }; // Extrait les deux caractères hexadécimaux suivants
            dst[di++] = (char) strtol(hex, NULL, 16); // Convertit les caractères hexadécimaux en un caractère ASCII et l'ajoute à la destination
            si += 3; // Avance l'index de lecture de 3 caractères (le '%' et les deux hexadécimaux)
        }
        else if (src[si] == '+') // Les '+' dans les données encodées en URL représentent des espaces, on les convertit en espace
        {
            dst[di++] = ' '; // Remplace le '+' par un espace dans la destination
            si++; // Avance l'index de lecture d'un caractère
        }
        else // Sinon, on copie le caractère tel quel
        {
            dst[di++] = src[si++]; // Copie le caractère actuel de la source vers la destination et avance les index de lecture et d'écriture
        }
    }
    dst[di] = '\0'; // Termine la chaîne de destination par un caractère nul
}

/**
 * @brief Enregistre les paramètres configurés via le serveur web dans NVS.
 *
 * Cette fonction lit les données POST envoyées par le client via le serveur web et enregistre les paramètres de connexion Wi-Fi,
 * MQTT et compteurs dans la non-volatile storage (NVS).
 */
static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[512]; // Buffer pour recevoir les données POST (taille adaptée à la configuration attendue)
    int total_len = req->content_len; // Longueur totale des données POST à recevoir
    int received = 0; // Nombre de bytes reçus jusqu'à présent
    int ret; // Variable pour stocker le résultat de la fonction de réception
 
    if (total_len >= (int)sizeof(buf))  // Si les données POST sont plus grandes que le buffer, on renvoie une erreur
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return ESP_FAIL;
    }

    while (received < total_len)  // Tant que nous n'avons pas reçu toutes les données POST, continuer à les recevoir
    {
        ret = httpd_req_recv(req, buf + received, total_len - received); // Reçoit une partie des données POST et les stocke dans le buffer à la position appropriée
        if (ret <= 0)  // Si une erreur se produit lors de la réception, ou si le client a fermé la connexion, on arrête la réception
        {
            return ESP_FAIL;// Arrête la réception en cas d'erreur ou de fermeture de la connexion par le client
        }
        received += ret; // Met à jour le nombre de bytes reçus avec le nombre de bytes reçus dans cette itération
    }

    buf[received] = '\0'; // Termine la chaîne reçue par un caractère nul pour pouvoir la traiter comme une chaîne C    
    ESP_LOGI("SAVE", "POST RAW: %s", buf); // Log de la charge utile brute reçue pour le débogage

    // -------- PARSING ROBUSTE --------
    // IMPORTANT: séparer sur "&" (et non sur "&amp;")
    char *saveptr = NULL; // Pointeur de sauvegarde pour strtok_r, permettant de continuer à parcourir la chaîne après chaque token
    char *token = strtok_r(buf, "&", &saveptr); // Sépare la chaîne reçue en tokens basés sur le caractère '&', qui sépare les paires clé-valeur dans les données POST encodées en URL

    while (token != NULL)// Tant qu'il y a des tokens à traiter, continuer à les analyser
    {
        char *eq = strchr(token, '='); // Cherche le caractère '=' dans le token pour séparer la clé de la valeur
        if (eq) // Si un '=' est trouvé, cela signifie que le token est une paire clé-valeur valide
        {
            *eq = '\0'; // Remplace le '=' par un caractère nul pour séparer la clé et la valeur dans le token
            char *key   = token; // La clé est la partie du token avant le '='
            char *value = eq + 1; // La valeur est la partie du token après le '='

            // Décoder la valeur encodée en x-www-form-urlencoded
            char decoded[256]; // Buffer pour la valeur décodée (taille adaptée à la configuration attendue)
            url_decode(value, decoded, sizeof(decoded)); // Décode la valeur encodée en URL pour obtenir la valeur originale

            // ---- Compteurs ----
            for (int i = 0; i < NB_COUNTERS; i++) // Vérifie si la clé correspond à un compteur (ex: c0, c1, etc.)
            {
                char expected[8]; // Buffer pour la clé attendue (ex: "c0", "c1", etc.)
                snprintf(expected, sizeof(expected), "c%d", i); // Formate la clé attendue pour le compteur i

                if (strcmp(key, expected) == 0) //  Si la clé correspond à celle du compteur i, on met à jour la valeur du compteur
                {
                    if (decoded[0] == '\0')  // Si la valeur décodée est une chaîne vide, on considère que le compteur doit être réinitialisé à zéro
                    {
                        counters[i] = 0; // Réinitialise le compteur i à zéro si la valeur décodée est vide
                    } else {
                        counters[i] = strtoul(decoded, NULL, 10); // Sinon, convertit la valeur décodée en un nombre entier non signé et l'assigne au compteur i
                    }
                    ESP_LOGI("SAVE", "Counter %d = %lu", i, (unsigned long)counters[i]); // Log de la nouvelle valeur du compteur i pour le débogage
                }
            }

            // ---- Noms MQTT ----
            for (int i = 0; i < NB_COUNTERS; i++) // Vérifie si la clé correspond à un nom de compteur MQTT (ex: m0, m1, etc.)
            {
                char expected[8]; // Buffer pour la clé attendue (ex: "m0", "m1", etc.)
                snprintf(expected, sizeof(expected), "m%d", i); // Formate la clé attendue pour le nom du compteur i

                if (strcmp(key, expected) == 0) // Si la clé correspond à celle du nom du compteur i, on met à jour le nom du compteur
                {
                    strncpy(mqtt_names[i], decoded, sizeof(mqtt_names[i]) - 1); // Copie la valeur décodée dans le tableau des noms MQTT pour le
                    mqtt_names[i][sizeof(mqtt_names[i]) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                    ESP_LOGI("SAVE", "MQTT name %d = %s", i, mqtt_names[i]); // Log du nouveau nom MQTT du compteur i pour le débogage
                }
            }

            // ---- WiFi ----
            if (strcmp(key, "ssid") == 0) // Si la clé est "ssid", on met à jour le SSID Wi-Fi
            {
                strncpy(wifi_ssid, decoded, sizeof(wifi_ssid) - 1); // Copie la valeur décodée dans la variable wifi_ssid pour le SSID Wi-Fi
                wifi_ssid[sizeof(wifi_ssid) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                ESP_LOGI("SAVE", "SSID = %s", wifi_ssid); // Log du nouveau SSID Wi-Fi pour le débogage
            }
            else if (strcmp(key, "pass") == 0) // Si la clé est "pass", on met à jour le mot de passe Wi-Fi
            {
                strncpy(wifi_pass, decoded, sizeof(wifi_pass) - 1); // Copie la valeur décodée dans la variable wifi_pass pour le mot
                wifi_pass[sizeof(wifi_pass) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                ESP_LOGI("SAVE", "PASS updated (len=%u)", (unsigned)strlen(wifi_pass)); // Log de la mise à jour du mot de passe Wi-Fi (affiche la longueur pour éviter d'afficher le mot de passe en clair dans les logs)
            }
            // ---- MQTT Server ----
            else if (strcmp(key, "mqtt_server") == 0) // Si la clé est "mqtt_server", on met à jour le serveur MQTT
            {
                strncpy(mqtt_Server, decoded, sizeof(mqtt_Server) - 1); // Copie la valeur décodée dans la variable mqtt_Server pour le serveur MQTT
                mqtt_Server[sizeof(mqtt_Server) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                ESP_LOGI("SAVE", "MQTT_SERVER = %s", mqtt_Server); // Log du nouveau serveur MQTT pour le débogage
            }
            // ---- MQTT User ----
            else if (strcmp(key, "mqtt_user") == 0) // Si la clé est "mqtt_user", on met à jour le nom d'utilisateur MQTT
            {
                strncpy(mqtt_user, decoded, sizeof(mqtt_user) - 1); // Copie la valeur décodée dans la variable mqtt_user pour le nom d'utilisateur MQTT
                mqtt_user[sizeof(mqtt_user) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                ESP_LOGI("SAVE", "MQTT_USER = %s", mqtt_user); // Log du nouveau nom d'utilisateur MQTT pour le débogage
            }
            // ---- MQTT Password ----
            else if (strcmp(key, "mqtt_pass") == 0) // Si la clé est "mqtt_pass", on met à jour le mot de passe MQTT
            {
                strncpy(mqtt_pass, decoded, sizeof(mqtt_pass) - 1); //  Copie la valeur décodée dans la variable mqtt_pass pour le mot
                mqtt_pass[sizeof(mqtt_pass) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                ESP_LOGI("SAVE", "MQTT_PASS updated (len=%u)", (unsigned)strlen(mqtt_pass)); // Log de la mise à jour du mot de passe MQTT (affiche la longueur pour éviter d'afficher le mot de passe en clair dans les logs)  
            }
            // ---- MQTT Port ----
            else if (strcmp(key, "mqtt_port") == 0) // Si la clé est "mqtt_port", on met à jour le port MQTT
            {
                strncpy(mqtt_port, decoded, sizeof(mqtt_port) - 1); // Copie la valeur décodée dans la variable mqtt_port pour le port MQTT
                mqtt_port[sizeof(mqtt_port) - 1] = '\0'; // Assure que la chaîne est terminée par un caractère nul pour éviter les débordements de tampon
                ESP_LOGI("SAVE", "MQTT_PORT updated (len=%u)", (unsigned)strlen(mqtt_port)); // Log de la mise à jour du port MQTT (affiche la longueur pour éviter d'afficher le port en clair dans les logs)
            }
        }

        token = strtok_r(NULL, "&", &saveptr);// Récupère le token suivant pour continuer à analyser les paires clé-valeur dans les données POST
    }

    // -------- SAUVEGARDE NVS --------
    nvs_handle_t handle; // Handle pour accéder à la NVS
    esp_err_t err; // Variable pour stocker le résultat des opérations NVS

    // --- Compteurs + noms ---
    err = nvs_open("counters", NVS_READWRITE, &handle); // Ouvre un espace de noms "counters" en mode lecture-écriture pour stocker les compteurs et leurs noms
    if (err == ESP_OK) // Si l'ouverture de l'espace de noms "counters" est réussie, on enregistre les compteurs et leurs noms dans la NVS
    {
        for (int i = 0; i < NB_COUNTERS; i++) // Pour chaque compteur, on enregistre sa valeur et son nom dans la NVS
        {
            char key[8]; // Buffer pour la clé à utiliser dans la NVS (ex: "c0", "m0", etc.)

            snprintf(key, sizeof(key), "c%d", i);// Formate la clé pour le compteur i (ex: "c0", "c1", etc.)
            nvs_set_u32(handle, key, counters[i]); // Enregistre la valeur du compteur i dans la NVS avec la clé correspondante

            snprintf(key, sizeof(key), "m%d", i); // Formate la clé pour le nom du compteur i (ex: "m0", "m1", etc.)
            nvs_set_str(handle, key, mqtt_names[i]); // Enregistre le nom du compteur i dans la NVS avec la clé correspondante
        }

        nvs_commit(handle);//   Valide les modifications apportées à la NVS pour s'assurer qu'elles sont écrites de manière persistante
        nvs_close(handle); // Ferme le handle de la NVS pour libérer les ressources associées
    }
    else // Si l'ouverture de l'espace de noms "counters" échoue, on log une erreur
    {
        ESP_LOGE("SAVE", "Failed to open NVS counters: %s", esp_err_to_name(err)); // Log de l'erreur d'ouverture de la NVS pour les compteurs, en affichant le message d'erreur correspondant à l'erreur retournée par nvs_open
    }

    // --- WiFi --- 
    err = nvs_open("wifi", NVS_READWRITE, &handle); // Ouvre un espace de noms "wifi" en mode lecture-écriture pour stocker les paramètres de connexion Wi-Fi
    if (err == ESP_OK) // Si l'ouverture de l'espace de noms "wifi" est réussie, on enregistre les paramètres de connexion Wi-Fi dans la NVS
    {
        nvs_set_str(handle, "ssid", wifi_ssid); // Enregistre le SSID Wi-Fi dans la NVS avec la clé "ssid"
        nvs_set_str(handle, "pass", wifi_pass); // Enregistre le mot de passe Wi-Fi dans la NVS avec la clé "pass"
        nvs_commit(handle); // Valide les modifications apportées à la NVS pour s'assurer qu'elles sont écrites de manière persistante
        nvs_close(handle); // Ferme le handle de la NVS pour libérer les ressources associées
    }
    else // Si l'ouverture de l'espace de noms "wifi" échoue, on log une erreur
    {
        ESP_LOGE("SAVE", "Failed to open NVS wifi: %s", esp_err_to_name(err)); // Log de l'erreur d'ouverture de la NVS pour le Wi-Fi, en affichant le message d'erreur correspondant à l'erreur retournée par nvs_open
    }

    // --- MQTT ---
    err = nvs_open("mqtt", NVS_READWRITE, &handle); // Ouvre un espace de noms "mqtt" en mode lecture-écriture pour stocker les paramètres de connexion MQTT
    if (err == ESP_OK) // Si l'ouverture de l'espace de noms "mqtt" est réussie, on enregistre les paramètres de connexion MQTT dans la NVS
    {
        nvs_set_str(handle, "mqtt_server", mqtt_Server); // Enregistre le serveur MQTT dans la NVS avec la clé "mqtt_server"
        nvs_set_str(handle, "mqtt_user",   mqtt_user); // Enregistre le nom d'utilisateur MQTT dans la NVS avec la clé "mqtt_user"
        nvs_set_str(handle, "mqtt_pass",   mqtt_pass); // Enregistre le mot de passe MQTT dans la NVS avec la clé "mqtt_pass"
        nvs_set_str(handle, "mqtt_port",   mqtt_port); // Enregistre le port MQTT dans la NVS avec la clé "mqtt_port"
        nvs_commit(handle); // Valide les modifications apportées à la NVS pour s'assurer qu'elles sont écrites de manière persistante
        nvs_close(handle); // Ferme le handle de la NVS pour libérer les ressources associées
    }
    else // Si l'ouverture de l'espace de noms "mqtt" échoue, on log une erreur
    {
        ESP_LOGE("SAVE", "Failed to open NVS mqtt: %s", esp_err_to_name(err)); // Log de l'erreur d'ouverture de la NVS pour le MQTT, en affichant le message d'erreur correspondant à l'erreur retournée par nvs_open
    }


    // --- Réponse au client ---
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h2>Configuration saved, Please reboot the device</h2>"
        "<p>Rebooting...</p></body></html>");

    vTaskDelay(pdMS_TO_TICKS(1000)); // Attendre un moment pour que le client puisse recevoir la réponse

    return ESP_OK; // Retourne ESP_OK pour indiquer que la requête a été traitée avec succès
}

/**
 * @brief Démarre le serveur web pour la configuration.
 *
 * Cette fonction initialise et démarre un serveur web qui permet de configurer les paramètres de connexion Wi-Fi, MQTT et compteurs via une interface Web.
 */
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); // Utilise la configuration par défaut pour le serveur HTTP
    httpd_start(&server, &config); // Démarre le serveur HTTP avec la configuration spécifiée et stocke le handle du serveur dans la variable server

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = config_get_handler
    }; // Structure pour définir le gestionnaire de la route racine ("/") qui répond aux requêtes GET en appelant la fonction config_get_handler

    httpd_register_uri_handler(server, &root); // Enregistre le gestionnaire de la route racine ("/") auprès du serveur HTTP pour qu'il puisse traiter les requêtes GET à cette route

     httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = NULL
    }; // Structure pour définir le gestionnaire de la route "/save" qui répond aux requêtes POST en appelant la fonction save_post_handler
    httpd_register_uri_handler(server, &save); // Enregistre le gestionnaire de la route "/save" auprès du serveur HTTP pour qu'il puisse traiter les requêtes POST à cette route
}

/**
 * @brief Démarre le mode AP (point d'accès) pour la configuration.
 *
 * Cette fonction configure l'ESP32 en mode AP ouvert et démarre un serveur web pour permettre la configuration des paramètres de connexion Wi-Fi, MQTT et compteurs.
 */
void start_config_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode (open)..."); // Log de démarrage du mode AP pour la configuration
    // --- INITIALISATION REQUISE ---
    esp_netif_init(); // Initialise la pile réseau (obligatoire avant d'utiliser le Wi-Fi)
    esp_event_loop_create_default(); // Crée une boucle d'événements par défaut pour gérer les événements système
    esp_netif_create_default_wifi_ap(); // Crée une interface réseau par défaut pour le mode point d'accès Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Utilise la configuration par défaut pour l'initialisation du Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); // Initialise le Wi-Fi avec la configuration spécifiée et vérifie que l'initialisation s'est déroulée sans erreur


    // Configuration AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID, // SSID du point d'accès pour la configuration
            .ssid_len = strlen(AP_SSID), // Longueur du SSID du point d'accès
            .channel = 1, // Canal Wi-Fi utilisé pour le point d'accès (1-13)
            .password = "",             // Mot de passe du point d'accès (vide pour un AP ouvert)
            .max_connection = 4, // Nombre maximum de connexions simultanées au point d'accès
            .authmode = WIFI_AUTH_OPEN // Mode d'authentification pour le point d'accès (WIFI_AUTH_OPEN pour un AP ouvert sans mot de passe)
        }
    }; // Structure de configuration pour le mode point d'accès, définissant le SSID, la longueur du SSID, le canal, le mot de passe (vide pour un AP ouvert), le nombre maximum de connexions et le mode d'authentification

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));          // Met en mode AP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config)); // Applique la config
    ESP_ERROR_CHECK(esp_wifi_start());                          // Démarre l’AP
    vTaskDelay(pdMS_TO_TICKS(1500)); // Attendre un moment pour que le point d'accès soit opérationnel
    ESP_LOGI(TAG, "AP Started. SSID: %s (open)", AP_SSID); // Log de succès du démarrage du point d'accès, affichant le SSID utilisé pour la configuration

    start_webserver(); // Lance le serveur web de configuration
}
