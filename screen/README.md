# Screen Brightness & AMOLED Color Profiles (Samsung Galaxy Book 12)

Native hardware control for the Samsung Galaxy Book 12 (SM-W720) AMOLED panel via DisplayPort AUX channel (DP-AUX) and graphical/CLI color profile switcher.

---

## 📁 Directory Structure

- **`src/`**: C source code of the brightness daemon (`brightness.c`, `dpAux.c`, `tables.c`, `main.c`, `Makefile`).
- **`galaxybook-brightness-wrapper.sh`**: Startup wrapper applying the saved color profile before launching the daemon.
- **`galaxybook-colorprofile`**: Script / GUI dialog (`kdialog` on KDE or `zenity` on GNOME/GTK, plus CLI fallback) to switch AMOLED color profiles.
- **`configs/`**:
  - `galaxybook-brightness.service`: Systemd background service.
  - `galaxybook-brightness-sudoers`: Sudoers rule allowing smooth daemon restart without password prompt when changing profiles.
  - `galaxybook-colorprofile.desktop`: Desktop menu entry for KDE / GNOME / XFCE.
- **`install.sh`**: Automated build and install script.
- **`uninstall.sh`**: Complete uninstallation script.

---

## 🚀 Installation

From the repository root or inside this folder:

```bash
cd screen
./install.sh
```

---

## 🎨 Supported AMOLED Color Profiles

1. **Adobe RGB**
2. **DCI-P3**
3. **sRGB** (Natural standard - recommended for accurate color rendition)
4. **Adaptive / Dynamic** (Samsung default vibrant OLED mode)
5. **Advanced custom profiles** (Modes 4, 5, 6)

You can switch profiles at any time via the application menu or by running:
```bash
galaxybook-colorprofile
# Or via CLI argument (0 to 6):
galaxybook-colorprofile 3
```
