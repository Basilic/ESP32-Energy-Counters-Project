/**
 * @file gpio_pulse.c
 * @brief Gestion des impulsions GPIO avec débouncing par temporisation.
 *
 * Ce module gère la configuration des GPIO en entrée interruption (front montant),
 * lance un timer logiciel pour valider la stabilité du niveau (débouncing) et incrémente
 * un compteur si le signal est toujours HIGH après la temporisation. Les impulsions validées
 * sont ensuite envoyées vers une tâche de debug via une file.
 *
 * Architecture :
 *  GPIO ISR → Timer debounce → Validation → File → Tâche debug
 */

#include "freertos/FreeRTOS.h"      // API FreeRTOS
#include "freertos/queue.h"         // Gestion des files (queues)
#include "esp_log.h"                // Système de logs ESP-IDF
#include "driver/gpio.h"            // Driver GPIO ESP-IDF
#include "gpio_pulse.h"             // Header du module
#include "esp_timer.h"              // Timer haute résolution (µs)
#include "esp_attr.h"               // Attribut IRAM_ATTR pour ISR
#include "config.h"                 // Configuration globale (pins, NB_COUNTERS, DEBOUNCE_US)
#include "nvs_flash.h"       // Fonctions NVS pour initialiser la mémoire flash
#include "nvs.h"             // Fonctions NVS pour lire/écrire des valeurs

volatile uint32_t isr_count = 0;    // Compteur debug du nombre d'interruptions reçues
static const char *TAG = "GPIO_PULSE"; // Identifiant de log du module
static QueueHandle_t pulse_queue;   // Queue pour transmettre les index validés à la task debug
uint32_t counters[NB_COUNTERS] = {0}; // Tableau global des compteurs d’impulsions
static pulse_ctx_t pulse_ctx[NB_COUNTERS]; // Contexte associé à chaque GPIO (index + timer)


/**
 * @brief Tâche FreeRTOS pour gérer le bouton de démarrage (BOOT).
 *
 * Cette tâche configure un GPIO en entrée interruption et détecte les appuis longs sur ce bouton.
 * Si l'appui est prolongé, elle active le mode de configuration et redémarre le système.
 *
 * @param pv Paramètre non utilisé
 */
void task_boot_button(void *pv)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO, // Masque pour le GPIO du bouton
        .mode = GPIO_MODE_INPUT, // Configure en entrée
        .pull_up_en = GPIO_PULLUP_ENABLE, // Active la résistance de pull-up interne
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Désactive la résistance de pull-down
        .intr_type = GPIO_INTR_DISABLE // Pas d'interruption nécessaire pour ce bouton
    }; // Configure le GPIO du bouton en entrée avec pull-up et sans interruption

    gpio_config(&io_conf); // Applique la configuration du GPIO

    int64_t press_start_time = 0; // Variable pour stocker le temps de début d'appui sur le bouton
    bool pressed = false; // Indique si le bouton est actuellement considéré comme appuyé
    bool reboot_triggered = false; // Indique si le reboot a déjà été déclenché pour éviter les redémarrages multiples

    ESP_LOGI(TAG, "Boot button task started"); // Log de démarrage de la tâche

    while (1)
    {
        int level = gpio_get_level(BOOT_BUTTON_GPIO); // Lit le niveau du GPIO du bouton (0 = appuyé, 1 = relâché)

        if (level == 0) // Bouton appuyé (LOW) // Logique inverse à cause du pull-up
        {
            if (!pressed) // Si ce n'était pas déjà considéré comme appuyé, on enregistre le temps de début d'appui
            {
                pressed = true; // Marque le bouton comme appuyé
                press_start_time = esp_timer_get_time(); // Enregistre le temps actuel en microsecondes
                reboot_triggered = false; // Reset du flag de reboot pour permettre un nouveau reboot si le bouton est maintenu à nouveau
                ESP_LOGI(TAG, "BOOT pressed"); // Log de détection d'appui sur le bouton
            }
            else if (!reboot_triggered) // Si le bouton est toujours considéré comme appuyé et que le reboot n'a pas encore été déclenché, on vérifie la durée d'appui
            {
                int64_t now = esp_timer_get_time(); // Obtient le temps actuel en microsecondes
                int64_t elapsed_ms = (now - press_start_time) / 1000; // Calcule le temps écoulé en millisecondes

                if (elapsed_ms >= BOOT_LONG_PRESS_TIME_MS) // Si le temps d'appui dépasse le seuil défini pour un appui long, on déclenche le reboot
                {
                    reboot_triggered = true; // Marque le reboot comme déclenché pour éviter les redémarrages multiples

                    ESP_LOGW(TAG, "BOOT LONG PRESS detected -> REBOOT"); // Log de détection d'un appui long sur le bouton
                    nvs_handle_t handle; // Handle pour accéder à la NVS de configuration
                    esp_err_t ret = nvs_open("config", NVS_READWRITE, &handle); // Ouvre la NVS "config" en mode lecture/écriture
                    if (ret == ESP_OK) { // Si l'ouverture réussit, on écrit le flag de mode configuration dans la NVS pour indiquer au système de démarrer en mode configuration après le reboot
                        uint8_t flag = 1; // 1 = mode configuration activé
                        ret = nvs_set_u8(handle, "config_mode", flag); // Tente d'écrire le flag de mode configuration dans la NVS
                        nvs_commit(handle); // Commite les modifications pour s'assurer que la valeur est bien sauvegardée
                        nvs_close(handle); // Ferme la NVS après écriture
                        ESP_LOGI(TAG, "Config mode flag saved to NVS"); // Log de succès de sauvegarde du flag de mode configuration
                    } else { // Si l'ouverture échoue, on log une erreur
                        ESP_LOGE(TAG, "Impossible d'ouvrir NVS pour flag config mode"); // Log d'erreur
                    }
                    vTaskDelay(pdMS_TO_TICKS(200));  // petit délai pour flush logs
                    while(gpio_get_level(BOOT_BUTTON_GPIO)==0){} // Attente que le bouton soit relâché pour éviter de redémarrer en boucle si le bouton est maintenu
                    esp_restart();                   // 🔥 reboot propre ESP32
                }
            }
        }
        else // Bouton relâché (HIGH)
        {
            pressed = false;  // Reset si relâché
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // polling léger + anti-rebond
    }
}

/**
 * @brief Callback du timer de débouncing.
 *
 * Cette fonction est appelée après une temporisation définie (DEBOUNCE_US) suite à un front montant détecté sur un GPIO.
 * Elle vérifie que le signal reste HIGH et, si c'est le cas, incrémente le compteur correspondant et envoie l'index
 * vers la tâche de debug.
 *
 * @param arg Pointeur vers la structure pulse_ctx_t associée au GPIO concerné
 */
static void verify_stability_callback(void *arg)
{
    pulse_ctx_t *ctx = (pulse_ctx_t *)arg;  // Récupère le contexte du GPIO concerné

    ESP_LOGI(TAG, "Pulse debounce timer expired for GPIO %d (compteur %d)", ctx->gpio, ctx->idx); // Log expiration timer

    if (gpio_get_level(ctx->gpio) == 1)     // Vérifie que le niveau est toujours HIGH
    {
        counters[ctx->idx]++;               // Incrémente le compteur correspondant

        int idx = ctx->idx;                 // Copie locale de l’index

        xQueueSend(pulse_queue, &idx, 0);   // Envoie l’index vers la task debug (contexte non ISR)
    }
}

/**
 * @brief Tâche FreeRTOS pour afficher les impulsions validées.
 *
 * Cette tâche attend en permanence des index provenant d'une file et affiche la valeur du compteur associé.
 *
 * @param pv Paramètre non utilisé
 */
static void pulse_debug_task(void *pv)
{
    int idx;                                        // Variable pour stocker l’index reçu

    ESP_LOGI(TAG, "Pulse debug task started");      // Log démarrage tâche

    while (1)                                       // Boucle infinie
    {
        if (xQueueReceive(pulse_queue, &idx, portMAX_DELAY)) // Attend un index depuis la queue
        {
            ESP_LOGI(TAG,
                     "Pulse valide sur compteur %d -> valeur = %lu",
                     idx,
                     counters[idx]);               // Affiche compteur mis à jour
        }
    }
}

/**
 * @brief ISR déclenchée sur front montant GPIO.
 *
 * Cette interruption :
 *  - Compte le nombre total d’interruptions reçues (debug)
 *  - Stoppe le timer si déjà actif
 *  - Relance un timer de validation (debounce)
 *
 * @param arg Pointeur vers la structure pulse_ctx_t du GPIO concerné
 */
static void IRAM_ATTR pulse_isr(void *arg)
{
    isr_count++;                                   // Incrémente compteur ISR global (debug)

    pulse_ctx_t *ctx = (pulse_ctx_t *)arg;         // Récupère le contexte du GPIO

    esp_timer_stop(ctx->verify_timer);             // Stoppe le timer si déjà lancé

    esp_timer_start_once(ctx->verify_timer, DEBOUNCE_US); // Lance le timer debounce
}

/**
 * @brief Initialise les GPIO, timers et interruptions.
 *
 * Cette fonction :
 *  - Configure chaque GPIO en entrée interruption
 *  - Crée un timer de validation par GPIO
 *  - Attache une ISR à chaque pin
 *  - Crée une tâche de debug unique
 */
void gpio_init_pulses(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);          // Force le niveau de log global à INFO

    ESP_LOGE(TAG, "GPIO pulse init Start");        // Log début initialisation

    gpio_config_t io_conf = {                      // Structure de configuration GPIO
        .mode = GPIO_MODE_INPUT,                   // Configure en entrée
        .pull_up_en = GPIO_PULLUP_DISABLE,         // Pull-up interne désactivé
        .pull_down_en = GPIO_PULLDOWN_DISABLE,     // Pull-down interne désactivé
        .intr_type = GPIO_INTR_POSEDGE             // Interruption sur front montant
    };

    pulse_queue = xQueueCreate(10, sizeof(int));   // Création queue de taille 10

    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);  // Installe le service ISR en IRAM

    for (int i = 0; i < NB_COUNTERS; i++)          // Boucle sur tous les compteurs
    {
        io_conf.pin_bit_mask = 1ULL << pulse_pins[i]; // Sélectionne la pin courante

        gpio_config(&io_conf);                     // Applique configuration GPIO

        pulse_ctx[i].idx = i;                      // Associe index compteur

        pulse_ctx[i].gpio = pulse_pins[i];         // Associe numéro GPIO

        const esp_timer_create_args_t timer_args = // Structure config timer
        {
            .callback = &verify_stability_callback, // Callback à exécuter
            .arg = &pulse_ctx[i],                   // Argument passé au callback
            .name = "pulseVerify"                   // Nom debug timer
        };

        esp_timer_create(&timer_args, &pulse_ctx[i].verify_timer); // Création timer

        gpio_isr_handler_add(pulse_pins[i],        // Attache ISR à la pin
                             pulse_isr,
                             &pulse_ctx[i]);
    }

    xTaskCreate(                                   // Création tâche debug unique
        pulse_debug_task,
        "pulse_debug_task",
        4096,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "GPIO pulse init OK");           // Log fin initialisation
}
