# Current version is 1.08

## Build out your environment below on Pi or Linux...


## Step 1 — Install Arduino CLI...

```console
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/
arduino-cli version
```

## Step 2 — Initialize config and install the RP2040 board package...

```console
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

## Step 3 — Install required libraries for the TFT_eSPI library (the ILI9488 driver files are extracted from it)...

```console
arduino-cli lib install "TFT_eSPI"
arduino-cli lib install "Adafruit GFX Library"
```

## Step 4 — Clone this repo

```console
git clone https://github.com/drexjj/zbitxfrontpanel
```

## Step 5 — Build the files in the zbitxfrontpanel directory

```console
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipicow \
  --output-dir ./build \
  .
```

## Step 6 — Go into the build folder and copy your uf2 file to the zbitx


## Notes:
You can also change the version number displayed in the zbitxfrontpanel.ino file

## Troubleshooting
You may get a compiler error in some Linux systems depending on your configuration. Many times, this can be resolved by copying the TFT_setup.h file to your arduino library directory and rename it to user_setup.h 


The typical library location is in ~/Arduino/libraries/TFT_eSPI/User_Setup.h  


Please keep in mind that overwriting this file will change all of the globals for other projects, so please use with caution.


You could also try making a platform.local.txt and placing it into your ~/.arduino15/packages/rp2040/hardware/rp2040/x.x.x/ folder


Here are the contents of platform.local.txt

```console
compiler.cpp.extra_flags=-DUSER_SETUP_LOADED=1 -DILI9488_DRIVER=1
build.extra_flags=-DUSER_SETUP_LOADED=1 -DILI9488_DRIVER=1
```