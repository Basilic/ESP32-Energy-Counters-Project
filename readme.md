# ESP32 Energy Counters Project

> ⚠️ **Warning : Ce projet, dans sa version initiale, a été entièrement écrit et commenté par une IA. Il n’a jamais été testé sur le matériel. Utilisez-le à vos risques et périls !**

## Introduction

Ce projet est conçu pour un **ESP32 WROOM** afin de mesurer et publier la consommation électrique via des compteurs à impulsions (Wh).  
Le système lit des impulsions électriques provenant de jusqu'à **5 compteurs**, applique un anti-rebond, stocke les valeurs dans la mémoire NVS, et publie périodiquement les valeurs sur un **broker MQTT**.  

Ce projet a été dans ça version initial entierement écrit et commenté par une IA... Jamais testé!!

### Cas d'utilisation

- Surveillance de la consommation électrique domestique ou industrielle.
- Collecte des compteurs via MQTT pour un tableau de bord ou un serveur domotique.
- Projet réutilisable pour tout type de compteur à impulsions fonctionnant sur front montant.

---

## Matériel requis

- ESP32 WROOM
- Compteurs d'énergie à impulsions (Wh)
- Câblage vers les GPIO configurés (voir `config.h`)
- Connexion réseau Wi-Fi
- Broker MQTT accessible

---

## Structure du projet

- lib/
  - gpio_pulse/
    - gpio_pulse.c
    - gpio_pulse.h
  - mqtt/
    - mqtt.c
    - mqtt.h
  - storage/
    - storage.c
    - storage.h
  - watchdog/
    - watchdog.c
    - watchdog.h
  - config/
    - config.h
- src/
  - main.c
- platformio.ini  (si PlatformIO utilisé)
- README.md



---

## Configuration

Tous les paramètres critiques sont centralisés dans **`config.h`** :

### Wi-Fi
```c
#define WIFI_SSID        "TON_SSID"
#define WIFI_PASS        "TON_PASSWORD"
#define WIFI_CONNECTED_BIT BIT0
````

### MQTT

```c
#define MQTT_BROKER_URI  "mqtt://192.168.1.10"
#define MQTT_USERNAME    "user"
#define MQTT_PASSWORD    "pass"
#define MQTT_PUBLISH_PERIOD_MS (5 * 60 * 1000) // 5 minutes
```

### Compteurs

```c
#define NB_COUNTERS 5
#define DEBOUNCE_US 100000 // 100 ms anti-rebond
static const gpio_num_t pulse_pins[NB_COUNTERS] = {
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_21,
    GPIO_NUM_22,
    GPIO_NUM_23
};
```

---

## Compilation et Flash

### Avec PlatformIO

1. Installer [PlatformIO](https://platformio.org/) (extension VSCode recommandée)
2. Ouvrir le projet dans VSCode
3. Vérifier le fichier `platformio.ini` pour l'ESP32 :

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = esp-idf
monitor_speed = 115200
```

4. Build et upload :

```bash
pio run       # Compile
pio run --target upload  # Flash sur l'ESP32
pio device monitor      # Ouvrir le moniteur série
```

### Avec ESP-IDF

1. Installer ESP-IDF selon la documentation officielle : [https://docs.espressif.com/projects/esp-idf](https://docs.espressif.com/projects/esp-idf)
2. Initialiser l'environnement :

```bash
. $HOME/esp/esp-idf/export.sh
```

3. Configurer le projet (optionnel) :

```bash
idf.py menuconfig
```

4. Compiler et flasher :

```bash
idf.py build
idf.py flash
idf.py monitor
```

---

## Structure logicielle

* **`main.c`** : initialise la NVS, GPIO, crée les tâches FreeRTOS
* **`gpio_pulse`** : lecture des GPIO de compteurs avec ISR et anti-rebond
* **`storage`** : sauvegarde et lecture des compteurs dans la NVS
* **`wifi`** : gestion de la connexion Wi-Fi
* **`mqtt`** : client MQTT pour publier les compteurs
* **`watchdog`** : surveillance des tâches critiques pour éviter le blocage

---

## Usage

1. Connecter les compteurs aux GPIO définis.
2. Configurer Wi-Fi et MQTT dans `config.h`.
3. Compiler et flasher l'ESP32.
4. Les compteurs sont stockés en NVS toutes les 100 impulsions.
5. Les valeurs sont publiées sur MQTT toutes les 5 minutes.

Exemple de payload publié sur MQTT :

```json
{"c0":123,"c1":456,"c2":789,"c3":101,"c4":202}
```

---

## Recommandations

* Assurez-vous que chaque compteur ne dépasse pas la fréquence maximale gérée par l'anti-rebond (100 ms).
* Vérifier la configuration du broker MQTT et le réseau Wi-Fi avant de flasher.
* Pour plusieurs ESP32, modifier `MQTT_BROKER_URI` et les topics pour éviter les collisions.

---

## Licence

Ce projet est sous licence MIT – libre pour modification et utilisation.
