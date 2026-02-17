/**
 * @file gpio_pulse.c
 * @brief Gestion des impulsions GPIO avec debounce par temporisation.
 *
 * Ce module :
 *  - Configure plusieurs GPIO en entrée interruption (front montant)
 *  - Lance un timer logiciel pour valider la stabilité du niveau (debounce)
 *  - Incrémente un compteur si le signal est toujours HIGH après temporisation
 *  - Envoie les impulsions validées vers une tâche de debug via une queue
 *
 * Architecture :
 *  GPIO ISR  →  Timer debounce  →  Validation  →  Queue  →  Task debug
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



void task_boot_button(void *pv)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    int64_t press_start_time = 0;
    bool pressed = false;
    bool reboot_triggered = false;

    ESP_LOGI(TAG, "Boot button task started");

    while (1)
    {
        int level = gpio_get_level(BOOT_BUTTON_GPIO);

        if (level == 0) // Bouton appuyé (LOW)
        {
            if (!pressed)
            {
                pressed = true;
                press_start_time = esp_timer_get_time();
                reboot_triggered = false;
                ESP_LOGI(TAG, "BOOT pressed");
            }
            else if (!reboot_triggered)
            {
                int64_t now = esp_timer_get_time();
                int64_t elapsed_ms = (now - press_start_time) / 1000;

                if (elapsed_ms >= BOOT_LONG_PRESS_TIME_MS)
                {
                    reboot_triggered = true;

                    ESP_LOGW(TAG, "BOOT LONG PRESS detected -> REBOOT");
                    nvs_handle_t handle;
                    esp_err_t ret = nvs_open("config", NVS_READWRITE, &handle);
                    if (ret == ESP_OK) {
                        uint8_t flag = 1; // 1 = mode configuration activé
                        ret = nvs_set_u8(handle, "config_mode", flag);
                        nvs_commit(handle);
                        nvs_close(handle);
                        ESP_LOGI(TAG, "Config mode flag saved to NVS");
                    } else {
                        ESP_LOGE(TAG, "Impossible d'ouvrir NVS pour flag config mode");
                    }
                    vTaskDelay(pdMS_TO_TICKS(200));  // petit délai pour flush logs
                    while(gpio_get_level(BOOT_BUTTON_GPIO)==0){}
                    esp_restart();                   // 🔥 reboot propre ESP32
                }
            }
        }
        else
        {
            pressed = false;  // Reset si relâché
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // polling léger + anti-rebond
    }
}

/**
 * @brief Callback du timer de debounce.
 *
 * Cette fonction est appelée après DEBOUNCE_US microsecondes
 * suivant un front montant détecté sur un GPIO.
 *
 * Elle vérifie que le signal est toujours à l’état HIGH.
 * Si oui, l’impulsion est validée et le compteur correspondant est incrémenté.
 *
 * @param arg Pointeur vers la structure pulse_ctx_t associée au GPIO
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
 * @brief Tâche FreeRTOS de debug des impulsions validées.
 *
 * Cette tâche attend en permanence des index provenant de la queue.
 * Lorsqu’un index est reçu, elle affiche la valeur du compteur associé.
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
