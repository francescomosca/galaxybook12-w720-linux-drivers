# Samsung Galaxy Book 12 (SM-W720) - Driver e Fix Hardware Linux

[🇬🇧 **Read documentation in English**](README.md)

Raccolta organizzata e modulare di driver kernel, patch e utilità per abilitare il pieno supporto hardware sul tablet 2-in-1 **Samsung Galaxy Book 12 (SM-W720)** con qualsiasi distribuzione Linux (Arch Linux, CachyOS, Ubuntu, Debian, Fedora, ecc., con Kernel 6.x e 7.x).

> [!WARNING]
> **SPERIMENTALE**: Questi driver e configurazioni sono stati sviluppati su CachyOS e non sono stati testati su altri dispositivi e/o con altre distribuzioni Linux. Usare con cautela e a proprio rischio.

---

## 💻 Panoramica Compatibilità Hardware

| Componente | Stato | Tipo Driver | Distribuzioni Supportate | Note |
| :--- | :---: | :--- | :--- | :--- |
| **Fotocamere (Doppia)** | ✅ | Moduli Kernel (`imx258`, `imx241`, `ipu-bridge`, `cio2`, `int3472`) | Tutte (Kernel 6.x / 7.x) | Posteriore (13MP Sony IMX258) + Frontale (5MP Sony IMX241) via IPU3 e PipeWire |
| **Luminosità OLED** | ✅ | Demone User-space (bus diretto DP-AUX) | Tutte (KDE, GNOME, XFCE) | Controllo hardware reale senza sfarfallio dello schermo |
| **Profili Colore AMOLED** | ✅ | Script User-space + GUI (`kdialog` / `zenity`) | Tutte | Profili Adobe RGB, DCI-P3, sRGB, Adattivo |
| **Audio (ALC298)** | ✅ | Inizializzatore C nativo + hook systemd sleep | Tutte | Risolve gli altoparlanti muti all'avvio e dopo la sospensione (sleep/resume) |
| **Accelerometro** | ✅ | Modulo Kernel (`st_accel_samsung`) + `hwdb` | Tutte (con systemd) | Auto-rotazione 1:1 allineata al display OLED (Wayland / X11) |
| **Sensore Luce Ambientale** | ❌ | Driver mancante / In lavorazione | — | La regolazione automatica della luminosità ambientale non è al momento supportata |

---

## 📦 Prerequisiti & Dipendenze di Sistema

Prima di compilare e installare, assicurati di aver installato i tool di compilazione, DKMS e gli header del kernel:

### Arch Linux / CachyOS / Manjaro
```bash
sudo pacman -S base-devel linux-headers dkms libcamera iio-sensor-proxy
```

### Ubuntu / Debian / Linux Mint
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) dkms libcamera-tools iio-sensor-proxy zenity libudev-dev
```

### Fedora
```bash
sudo dnf install make gcc kernel-devel kernel-headers dkms libcamera-tools iio-sensor-proxy zenity systemd-devel
```

---

## 🚀 Installazione

Il modo più semplice per configurare tutto è utilizzare lo script **master installer** nella radice della repository. Configura e installa tutti i componenti nell'ordine corretto, registra i moduli in DKMS per la persistenza agli aggiornamenti e avvia i servizi systemd:

```bash
git clone https://github.com/francescomosca/samsung-galaxybook12-linux-drivers.git
cd samsung-galaxybook12-linux-drivers
./install.sh
```

Lo script principale installerà e configurerà automaticamente:
1. **Screen**: Compila il demone di luminosità DP-AUX, installa le regole sudoers e abilita il servizio systemd.
2. **Audio**: Compila l'inizializzatore del codec ALC298 e abilita i servizi systemd per boot e resume dallo standby.
3. **Accelerometro**: Compila e registra `st_accel_samsung` in DKMS, configura la matrice di calibrazione rotazione in `hwdb`.
4. **Fotocamere**: Compila e registra i 5 moduli fotocamera in DKMS, installa i profili di tuning IPU3 libcamera e le policy Firefox.

---

## 📂 Installazione Modulare (Componenti Singoli)

Se desideri installare o disinstallare solo componenti specifici, ciascuna sottocartella è indipendente:

### 1. 📷 [cameras/](cameras/)
Abilita entrambe le fotocamere (posteriore e frontale) tramite l'ISP Intel IPU3.
- Patch per `ipu-bridge`, ricevitore CSI-2 `ipu3-cio2` e PMIC/GPIO `int3472`.
- Driver dedicati per **Sony IMX258** (posteriore) e **Sony IMX241** (frontale).
- Profili di calibrazione libcamera (`.yaml`) e policy PipeWire per browser.
```bash
cd cameras
./install.sh
```

### 2. 💡 [screen/](screen/)
Controlla la luminosità dello schermo AMOLED Samsung tramite canale DisplayPort AUX (`/dev/drm_dp_aux*`) e gestisce il cambio profili colore.
- Demone C ad alte prestazioni (`galaxybook-brightness`).
- Selettore grafico profili colore AMOLED (`galaxybook-colorprofile`) compatibile KDE e GNOME/GTK.
- Servizio systemd persistente.
```bash
cd screen
./install.sh
```

### 3. 🔊 [audio/](audio/)
Inizializzazione hardware diretta per il codec audio Realtek ALC298.
- Inizializzatore C nativo (`alc298_init`) che riattiva l'amplificatore all'avvio e dopo il risveglio dallo sleep.
- Disabilita il power-saving aggressivo HDA che ammutolisce l'uscita audio.
```bash
cd audio
./install.sh
```

### 4. 🧭 [accel-sensor/](accel-sensor/)
Driver kernel nativo per l'accelerometro STMicroelectronics LIS2HH12 (ACPI ID `SAM0201`).
- Calibrazione matrice di orientamento (`hwdb`) per rotazione naturale 1:1.
- Integrazione nativa con `iio-sensor-proxy` e desktop environment (KDE Plasma Wayland, GNOME).
```bash
cd accel-sensor
./install.sh
```

---

## 🔄 Aggiornamenti Automatici del Kernel (DKMS)
I moduli kernel per fotocamere e accelerometro si integrano nativamente con **DKMS (Dynamic Kernel Module Support)**.
Ogni volta che la tua distribuzione Linux aggiorna il kernel (es. con `pacman -Syu`, `apt upgrade` o `dnf upgrade`), DKMS **ricompila e installa automaticamente** i moduli in background per il nuovo kernel. Non è richiesto alcun intervento manuale.

---

## 🌟 Ringraziamenti & Crediti
- Basato sulle ricerche hardware originali del repository [SamsungGalaxyBook12 di Teetoow](https://github.com/Teetoow/SamsungGalaxyBook12).
- Sottosistemi camera del kernel Linux curati dalla community Linux Media (Dan Scally, Hans de Goede, Sakari Ailus).
- Reverse engineering assistito, adattamento driver e fix sviluppati con supporto AI.

## 📄 Licenza
Questo progetto è distribuito sotto licenza [GNU General Public License v2.0 (GPL-2.0)](LICENSE).
