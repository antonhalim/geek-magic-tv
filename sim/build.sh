#!/bin/sh
# Builds the desktop simulator into sim/cyberclock-sim.
# Needs the PlatformIO libraries: run `pio run -e smalltv` once first.
set -e
cd "$(dirname "$0")/.."
LIB=.pio/libdeps/smalltv
# Display settings: keep in step with build_flags in platformio.ini (SMOOTH_FONT left out: no file system here).
TFT="-DUSER_SETUP_LOADED=1 -DST7789_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=240 -DTFT_MOSI=13 -DTFT_SCLK=14
     -DTFT_CS=-1 -DTFT_DC=0 -DTFT_RST=2 -DTFT_SPI_MODE=SPI_MODE3 -DSPI_FREQUENCY=40000000 -DTFT_INVERSION_ON
     -DTFT_RGB_ORDER=TFT_BGR -DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4 -DLOAD_FONT7"
# Copy TFT_eSPI and widen the font addresses it keeps in 32-bit variables (fine on the ESP8266, not on a 64-bit Mac).
B=sim/.build
rm -rf $B && mkdir -p $B && cp -R $LIB/TFT_eSPI $B/
perl -pi -e 's/uint32_t flash_address/uintptr_t flash_address/' $B/TFT_eSPI/TFT_eSPI.cpp $B/TFT_eSPI/Extensions/Sprite.cpp
c++ -std=c++17 -O2 -w $TFT -Isim/shims -I$B/TFT_eSPI -I$LIB/ArduinoJson/src -include sim/shims/sim_time.h \
  $B/TFT_eSPI/TFT_eSPI.cpp src/display.cpp src/logic.cpp src/faces/*.cpp sim/sim.cpp \
  -o sim/cyberclock-sim
echo "built sim/cyberclock-sim  ->  run it and open http://localhost:8240"
