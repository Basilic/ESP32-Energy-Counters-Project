#ifndef STORAGE_H
#define STORAGE_H

/**
 * @file storage.h
 * @brief Header pour le module de stockage persistant des compteurs ESP32 via NVS.
 *
 * Ce module permet de :
 *  - Initialiser la NVS et charger les compteurs depuis la mémoire flash
 *  - Sauvegarder un compteur spécifique dans la NVS
 *
 * Usage typique :
 * 1. Appeler nvs_init_and_load() au démarrage pour initialiser la NVS
 *    et charger les compteurs dans le tableau global `counters[]`.
 * 2. Appeler save_counter_to_nvs(idx, value) pour sauvegarder un compteur
 *    après un certain nombre d'impulsions.
 */

#include <stdint.h>  // Pour uint32_t

/**
 * @brief Initialise la NVS et charge les compteurs depuis la mémoire persistante.
 *
 * Cette fonction :
 *  - Initialise la NVS (efface si nécessaire)
 *  - Ouvre l'espace "counters" en lecture/écriture
 *  - Lit les compteurs existants et les stocke dans counters[]
 *  - Initialise à 0 si aucun compteur n'est trouvé
 */
void nvs_init_and_load(void);

/**
 * @brief Sauvegarde un compteur spécifique dans la NVS.
 *
 * @param idx   Index du compteur (0 à NB_COUNTERS-1)
 * @param value Valeur du compteur à sauvegarder
 *
 * Cette fonction :
 *  - Ouvre l'espace "counters" en lecture/écriture
 *  - Écrit la valeur dans la NVS
 *  - Commit pour s'assurer que la valeur est persistée
 */

void save_counter_to_nvs(int idx, uint32_t value);

#endif // STORAGE_H
