#include "storage.h"         // Header du module storage pour les prototypes
#include "nvs_flash.h"       // Fonctions NVS pour initialiser la mémoire flash
#include "nvs.h"             // Fonctions NVS pour lire/écrire des valeurs
#include "esp_log.h"         // Fonctions ESP_LOG pour debug
#include "gpio_pulse.h"      // Pour accéder au tableau global counters

// Tag utilisé pour les messages de log ESP_LOG
static const char *TAG = "STORAGE";


/**
 * @brief Initialise la NVS (Non-Volatile Storage) et charge les compteurs.
 * 
 * Cette fonction :
 * 1. Initialise la NVS (efface si version incompatible ou pas de pages libres).
 * 2. Ouvre l'espace "counters" en lecture/écriture.
 * 3. Lit les valeurs de chaque compteur dans la NVS et les stocke dans counters[].
 * 4. Si aucune valeur n'est trouvée, initialise à 0.
 * 5. Ferme la NVS et affiche un log.
 */
void nvs_init_and_load(void)
{
    // Initialise la NVS
    esp_err_t ret = nvs_flash_init();  // Retourne ESP_OK si succès, sinon un code d'erreur

    // Si pas de pages libres ou version NVS incompatible, on efface et on réinitialise
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); // Efface toute la NVS pour repartir propre
        ret = nvs_flash_init();             // Réinitialisation après effacement
    }

    ESP_ERROR_CHECK(ret); // Vérifie que l'init finale est correcte, arrête le programme si erreur

    // Ouvre l'espace "counters" en lecture/écriture
    nvs_handle_t handle;
    ret = nvs_open("counters", NVS_READWRITE, &handle); 
    if (ret != ESP_OK) {  // Si ouverture échoue
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS"); // Log l'erreur
        return; // On ne peut pas continuer si on ne peut pas accéder à la NVS
    }

    for (int i = 0; i < NB_COUNTERS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "c%d", i);

        uint32_t value = 0;
        ret = nvs_get_u32(handle, key, &value); // Lecture du compteur depuis la NVS

        if (ret == ESP_OK) {                     // Lecture réussie
            counters[i] = value;                 // Stocke la valeur dans le tableau
        } else if (ret == ESP_ERR_NVS_NOT_FOUND) { // La clé n'existe pas encore
            counters[i] = 0;                     // Initialise à 0
        } else {                                 // Autre erreur (ex: corruption NVS)
            ESP_LOGW(TAG, "Erreur lecture NVS compteur %d", i); // Log warning
            counters[i] = 0;                     // Sécurité : initialise à 0
        }
    }

    nvs_close(handle); // Ferme la NVS
    ESP_LOGI(TAG, "Compteurs chargés depuis NVS");
}



/**
 * @brief Sauvegarde un compteur spécifique dans la NVS.
 * 
 * @param idx : index du compteur (0 à NB_COUNTERS-1)
 * @param value : valeur à sauvegarder
 *
 * Cette fonction :
 * 1. Ouvre la NVS "counters" en lecture/écriture.
 * 2. Écrit la valeur du compteur dans la NVS.
 * 3. Effectue un commit pour garantir l'écriture dans la flash.
 * 4. Ferme la NVS et affiche un log.
 */
void save_counter_to_nvs(int idx, uint32_t value)
{
    nvs_handle_t handle;     

    // Ouvre l'espace "counters" en lecture/écriture
    esp_err_t ret = nvs_open("counters", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {  // Vérifie que l'ouverture a réussi
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS pour écriture compteur %d", idx);
        return; // Ne peut pas sauvegarder si l'ouverture échoue
    }

    char key[8];
    snprintf(key, sizeof(key), "c%d", idx);

    // Écrit la valeur du compteur dans la NVS
    ret = nvs_set_u32(handle, key, value); 
    if (ret != ESP_OK) { // Vérifie que l'écriture a réussi
        ESP_LOGE(TAG, "Impossible d'écrire compteur %d", idx);
        nvs_close(handle);
        return; // Arrête la fonction si écriture impossible
    }

    // Commit pour sauvegarder définitivement dans la flash
    ret = nvs_commit(handle);
    if (ret != ESP_OK) { // Vérifie que le commit a réussi
        ESP_LOGE(TAG, "Erreur commit NVS compteur %d", idx);
    }

    nvs_close(handle); // Ferme la NVS
    ESP_LOGI(TAG, "Compteur %d sauvegardé : %lu", idx, value);
}
