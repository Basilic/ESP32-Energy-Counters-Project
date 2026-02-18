#ifndef GPIO_PULSE_H
#define GPIO_PULSE_H

/**
 * @file gpio_pulse.h
 * @brief Header pour le module de comptage d'impulsions ESP32.
 *
 * Ce module :
 * - Configure les GPIO utilisés pour les compteurs d'énergie
 * - Gère les ISR déclenchées sur front montant
 * - Utilise un système de validation différée via esp_timer pour vérifier la stabilité du signal
 * - Fournit un tableau global `counters[]` contenant les valeurs actuelles
 *
 * Ce module n’utilise PAS de filtrage classique par “temps minimal entre pulses”.
 * Au lieu de cela, chaque impulsion est validée uniquement si le niveau du GPIO
 * reste stable HAUT pendant DEBOUNCE_US microsecondes après un front montant.
 *
 * Usage typique :
 * 1. Appeler gpio_init_pulses() au démarrage de l'application
 * 2. Lire les valeurs des compteurs via le tableau counters[]
 */

#include <stdint.h>     // Pour uint32_t
#include "esp_timer.h"  // Pour les timers de validation différée
#include "config.h"     // Pour les définitions de configuration (pins, NB_COUNTERS, DEBOUNCE_US)
// ---------------------------------------------------------------------------
// Nombre de compteurs utilisés
// ---------------------------------------------------------------------------
//#define NB_COUNTERS 5

// ---------------------------------------------------------------------------
// Tableau global des compteurs
// Mis à jour par les callbacks de validation (timers)
// ---------------------------------------------------------------------------
extern uint32_t counters[NB_COUNTERS];

void task_boot_button(void *pv);

/**
 * @brief Structure de contexte pour chaque canal d'impulsion.
 *
 * Pour chaque GPIO gérant un compteur, un contexte associe :
 * - idx : l’index du compteur (0..NB_COUNTERS-1)
 * - gpio : le numéro du GPIO utilisé
 * - verify_timer : timer asynchrone utilisé pour vérifier que
 *                  le signal est resté stable après un front montant
 *
 * Cette structure permet de passer au timer toutes les informations
 * nécessaires pour valider ou rejeter une impulsion.
 */
typedef struct {
    int idx;                       ///< Index du compteur
    int gpio;                      ///< Numéro du GPIO associé
    esp_timer_handle_t verify_timer;  ///< Timer de validation du niveau stable
} pulse_ctx_t;

/**
 * @brief Initialise les GPIO pour les compteurs d'impulsions et configure les ISR.
 *
 * Cette fonction :
 * - Configure chaque GPIO comme entrée avec pull-up
 * - Configure les interruptions sur front montant (GPIO_INTR_POSEDGE)
 * - Installe le service ISR (gpio_install_isr_service)
 * - Crée pour chaque entrée un timer esp_timer utilisé pour vérifier la stabilité du signal
 * - Les ISR ne valident plus directement les impulsions :
 *      → Elles ne font qu'enregistrer l'heure du front montant et démarrer un timer
 * - Lorsqu'un timer expire (DEBOUNCE_US µs plus tard), le niveau du GPIO est relu :
 *      → Si toujours HAUT → l'impulsion est validée → counters[idx]++
 *      → Sinon → rebond / glitch → impulsion ignorée
 *
 * Ce système rend la détection :
 * - plus fiable
 * - indépendante de la fréquence des pulses
 * - adaptée aux signaux bruités ou mécaniques
 */
void gpio_init_pulses(void);

#endif // GPIO_PULSE_H