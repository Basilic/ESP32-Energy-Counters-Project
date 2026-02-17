/**
 * @file watchdog.c
 * @brief Gestion du Watchdog Timer (WDT) pour surveiller les tâches sur les deux cœurs de l'ESP32.
 *        Permet d'initialiser le WDT, d'ajouter une tâche à surveiller et de réinitialiser le WDT.
 */

#include "watchdog.h"       // Header du module watchdog pour les prototypes
#include "esp_task_wdt.h"   // Fonctions ESP-IDF pour le task watchdog
#include "esp_log.h"        // Fonctions ESP_LOG pour debug

#define WDT_TIMEOUT_S 10    // Timeout du watchdog en secondes

static const char *TAG = "WDT"; // Tag utilisé pour les logs ESP_LOG


/**
 * @brief Initialise le task watchdog pour l'ESP32.
 *
 * Configure le timeout, les cœurs surveillés et le comportement en cas de timeout.
 * Devrait être appelé une seule fois au démarrage.
 */
void watchdog_init(void)
{
    // Configuration du watchdog
    esp_task_wdt_config_t config = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,       // Convertit secondes en millisecondes
        .idle_core_mask = (1 << 0) | (1 << 1),   // Surveille les deux cores (Core0 et Core1)
        .trigger_panic = true                     // Si timeout, déclenche un panic (reset du CPU)
    };

    esp_task_wdt_init(&config);                  // Initialise le watchdog avec cette config
    ESP_LOGI(TAG, "Watchdog initialisé (%ds)", WDT_TIMEOUT_S); // Log info
}


/**
 * @brief Ajoute la tâche courante à la liste des tâches surveillées par le watchdog.
 *
 * Chaque tâche critique doit appeler cette fonction au démarrage pour être surveillée.
 */
void watchdog_add_task(void)
{
    esp_task_wdt_add(NULL); // NULL = tâche courante
}


/**
 * @brief Réinitialise le compteur du watchdog pour la tâche courante.
 *
 * Doit être appelé périodiquement dans la boucle principale d'une tâche
 * pour éviter que le WDT déclenche un reset.
 */
void watchdog_reset(void)
{
    esp_task_wdt_reset(); // Reset du compteur WDT pour la tâche actuelle
}
