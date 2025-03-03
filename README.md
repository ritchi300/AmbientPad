# Ambientpad

## Overview
This project is an ESP32-based **WAV Player and Metronome** that utilizes **dual MAX98357A I2S amplifiers** for stereo audio output. The **left channel** plays WAV audio files from an **SD card**, while the **right channel** outputs a metronome click. The system features **crossfade between tracks**, a **rotary encoder** for BPM control, **footswitch inputs** for track navigation, and an **OLED display** for status visualization.

## Features
- **WAV playback** from SD card on **left audio channel**
- **Metronome click** on **right audio channel**
- **Crossfade transition** between tracks
- **Rotary encoder (GPIO 4, 35, 34)** to adjust BPM (30–240 BPM)
- **Rotary button** cycles through time signatures (4/4, 3/4, 6/8)
- **Footswitch controls:**
  - **GPIO 17**: Next track
  - **GPIO 16**: Previous track
  - **GPIO 33**: Toggle metronome
- **128x64 OLED display (SSD1306)** for track and BPM display
- **FreeRTOS-based display task** for efficient UI updates

---

## Hardware Requirements
### Required Components:
- **ESP32 Development Board**
- **MicroSD Card Module** (SPI-based, CS: GPIO 5)
- **Dual MAX98357A I2S Amplifier Module**
- **Rotary Encoder with Button**
- **SSD1306 OLED Display (128x64, I2C)**
- **Footswitches (optional, GPIO-based)**
- **Speaker(s)** (one or two for stereo output)

### Pin Configuration:
| Component           | GPIO Pin | Description       |
|--------------------|---------|-----------------|
| **Footswitch 2**   | 17      | Next track       |
| **Footswitch 3**   | 16      | Previous track   |
| **Footswitch 4**   | 33      | Toggle metronome |
| **Encoder SW**     | 4       | Rotary Button    |
| **Encoder DT**     | 35      | Encoder DT       |
| **Encoder CLK**    | 34      | Encoder CLK      |
| **OLED SDA**       | 21      | I2C SDA          |
| **OLED SCL**       | 22      | I2C SCL          |
| **SD CS**          | 5       | SD Card Chip Select |
| **I2S BCLK**       | 27      | I2S Clock        |
| **I2S LRC**        | 25      | I2S Left/Right Clock |
| **I2S DOUT**       | 26      | I2S Data Output  |

---

## Software Setup
### Dependencies
Ensure you have the following libraries installed in the Arduino IDE:
- **Arduino Core for ESP32**
- **Adafruit GFX Library**
- **Adafruit SSD1306 Library**
- **SD Library**

### Installation
1. **Clone this repository:**
   ```sh
   git clone https://github.com/ritchi300/AmbientPad.git
   ```
2. **Open the project in Arduino IDE** and select **ESP32 Dev Module** as the board.
3. **Install required libraries** (via Library Manager).
4. **Upload the code** to your ESP32 board.

---

## Usage
### Playing Tracks
- Insert a **microSD card** with **WAV files** into the SD card module.
- The **left channel** plays the selected **WAV file**, and the **right channel** outputs the metronome.
- The **OLED display** shows the track name and BPM.

### Changing Tracks
- **Press Footswitch 2 (GPIO 17)** → Next track
- **Press Footswitch 3 (GPIO 16)** → Previous track

### Metronome Control
- **Press Footswitch 4 (GPIO 33)** → Toggle metronome on/off

### BPM Control
- **Rotate Encoder (CLK, DT on GPIO 34, 35)** → Increase/Decrease BPM (30–240 BPM)
- **Press Encoder Button (GPIO 4)** → Change Time Signature (4/4, 3/4, 6/8)

### Crossfade Feature
- When switching files, a **3-second crossfade** is applied to create a smooth transition.

---

## Technical Details
### I2S Configuration
- **Sample Rate:** 44.1 kHz
- **Bit Depth:** 16-bit
- **Channels:** Stereo (left: WAV, right: metronome)
- **DMA Buffer:** 8 x 256 samples

### FreeRTOS Display Task
- Runs on **Core 0** to update the OLED without blocking audio playback.
- Uses **mutex locks** to ensure thread safety.

---

## Future Improvements
- Add **Bluetooth audio streaming**
- Support for **MP3 playback**
- Advanced **metronome customization**
- **WiFi-based track selection**

---

## License
This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

## Author
Developed by **Your Name** - [GitHub Profile](https://github.com/yourusername)

## Contributions
Pull requests and contributions are welcome! If you find bugs or have feature requests, please open an issue.

---

