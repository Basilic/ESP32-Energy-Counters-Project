#ifndef WATCHDOG_H
#define WATCHDOG_H

void watchdog_init(void);
void watchdog_add_task(void);
void watchdog_reset(void);

#endif
#ifndef WATCHDOG_H
#define WATCHDOG_H

/**
 * @file watchdog.h
 * @brief Header pour le module Watchdog Timer (WDT) ESP32.
 *        Permet d'initialiser le WDT, d'ajouter une tâche à surveiller
 *        et de réinitialiser le compteur pour éviter le reset.
 *
 * Usage typique :
 * 1. watchdog_init()   → au démarrage de l'application
 * 2. watchdog_add_task() → au démarrage de chaque tâche critique
 * 3. watchdog_reset()  → régulièrement dans la boucle de la tâche
 */

/**
 * @brief Initialise le task watchdog pour les tâches critiques.
 *        Configure le timeout, les cores surveillés et l'action en cas de timeout.
 */
void watchdog_init(void);

/**
 * @brief Ajoute la tâche courante à la liste des tâches surveillées par le WDT.
 *        Chaque tâche critique doit appeler cette fonction au démarrage.
 */
void watchdog_add_task(void);

/**
 * @brief Réinitialise le compteur du WDT pour la tâche courante.
 *        Doit être appelé régulièrement pour éviter le reset.
 */
void watchdog_reset(void);

#endif // WATCHDOG_H
