v1.07d

Build out your environment below on Pi or Linux...


Step 1 — Install Arduino CLI...

```console
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/
arduino-cli version   # confirm it works
```

Step 2 — Initialize config and install the RP2040 board package...

```console
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

Step 3 — Install required libraries for the TFT_eSPI library (the ILI9488 driver files are extracted from it)...

```console
arduino-cli lib install "TFT_eSPI"
arduino-cli lib install "Adafruit GFX Library"
```

Step 4 — Clone the repo

```console
git clone https://github.com/drexjj/zbitxfrontpanel
```

Step 5 — Build the files

```console
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --output-dir ./build \
  .
```

Step 6 — Go into the build folder and copy your uf2 file to the zbitx
