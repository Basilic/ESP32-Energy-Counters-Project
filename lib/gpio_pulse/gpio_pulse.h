#ifndef GPIO_PULSE_H
#define GPIO_PULSE_H

/**
 * @file gpio_pulse.h
 * @brief Header pour le module de comptage d'impulsions ESP32.
 *
 * Ce module :
 *  - Configure les GPIO utilisés pour les compteurs d'énergie
 *  - Gère l'ISR avec anti-rebond pour chaque entrée de compteur
 *  - Fournit un tableau global `counters[]` contenant les valeurs actuelles
 *
 * Usage typique :
 * 1. Appeler gpio_init_pulses() au démarrage de l'application
 * 2. Lire les valeurs des compteurs via le tableau counters[]
 */

#include <stdint.h>  // Pour uint32_t

// Nombre de compteurs utilisés
#define NB_COUNTERS 5

// Tableau global contenant la valeur de chaque compteur
// Mis à jour par les ISR (interrupts) et lu par les tâches
extern uint32_t counters[NB_COUNTERS];

/**
 * @brief Initialise les GPIO pour les compteurs d'impulsions et configure les ISR.
 *
 * Cette fonction :
 *  - Configure chaque GPIO comme entrée avec pull-up
 *  - Configure les interruptions sur front montant
 *  - Installe le service ISR
 *  - Les ISR mettront à jour le tableau counters[] avec les impulsions détectées
 */
void gpio_init_pulses(void);

#endif // GPIO_PULSE_H
