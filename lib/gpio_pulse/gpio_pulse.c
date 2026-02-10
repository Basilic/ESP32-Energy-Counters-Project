/**
 * @file gpio_pulse.c
 * @brief Fichier gérant les entrées de compteurs d'impulsions.
 *        Contient la configuration GPIO, l'ISR pour le comptage et l'anti-rebond.
 */

#include "driver/gpio.h"// Inclusion des fonctions de gestion des GPIO fournies par ESP-IDF
#include "gpio_pulse.h"// Inclusion du header du module gpio_pulse pour accéder aux compteurs et prototypes
#include "esp_timer.h"// Inclusion du timer ESP pour récupérer le temps en microsecondes dans l'ISR
#include "esp_attr.h"// Inclusion pour utiliser IRAM_ATTR et placer l'ISR en mémoire IRAM (rapide et safe)
#include "config.h"// Inclusion du header global de configuration (ex : DEBOUNCE_US, NB_COUNTERS)

static int64_t last_pulse_time[NB_COUNTERS] = {0}; // Tableau pour stocker le temps du dernier pulse pour chaque compteur, utilisé pour le debounce

uint32_t counters[NB_COUNTERS] = {0};; // Compteurs de chaque entrée

/**
 * @brief ISR (Interrupt Service Routine) pour gérer les impulsions des compteurs.
 *        Utilise un anti-rebond logiciel en microsecondes pour éviter les fausses impulsions.
 *        Chaque ISR incrémente le compteur correspondant si le temps depuis le dernier pulse
 *        dépasse le seuil DEBOUNCE_US.
 *
 * @param arg : index du compteur (0 à NB_COUNTERS-1) passé lors de l'enregistrement de l'ISR
 */
static void IRAM_ATTR pulse_isr(void* arg)
{
    int idx = (int)arg;                       // Récupère l'index du compteur depuis l'argument passé à l'ISR
    int64_t now = esp_timer_get_time();       // Récupère le temps actuel en microsecondes depuis le démarrage du microcontrôleur

    // Vérifie si le temps écoulé depuis la dernière impulsion est supérieur au temps de debounce
    if (now - last_pulse_time[idx] > DEBOUNCE_US) {
        last_pulse_time[idx] = now;          // Met à jour le temps du dernier pulse pour ce compteur
        counters[idx]++;                     // Incrémente le compteur correspondant
    }
    // Si le temps écoulé est inférieur au temps de debounce, l'impulsion est ignorée pour filtrer les rebonds
}


/**
 * @brief Initialise les entrées GPIO pour les compteurs d'impulsions.
 *        Configure chaque pin comme entrée avec pull-up, active une interruption sur front montant
 *        et enregistre l'ISR correspondante pour compter les impulsions.
 *        Enfin, installe le service ISR pour gérer les interruptions en IRAM.
 */
void gpio_init_pulses(void)
{
    // Structure de configuration GPIO
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,           // Configure le GPIO en mode entrée
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Active la résistance de pull-up interne
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Désactive la résistance de pull-down
        .intr_type = GPIO_INTR_POSEDGE,    // Déclenche l'interruption sur front montant
    };

    // Boucle sur tous les compteurs pour configurer chaque pin
    for (int i = 0; i < NB_COUNTERS; i++) {
        io_conf.pin_bit_mask = 1ULL << pulse_pins[i]; // Sélectionne le pin correspondant au compteur i
        gpio_config(&io_conf);                        // Applique la configuration au GPIO
        gpio_isr_handler_add(pulse_pins[i], pulse_isr, (void *)i); // Ajoute l'ISR pour ce pin, avec l'index comme argument
    }

    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);   // Installe le service ISR pour que les interruptions puissent être gérées en IRAM
}