/*************************************************************
 *  ESP32 WAV Player + Metronome (Stereo I2S) + OLED Display
 *  
 *  Now with Rotary Encoder on GPIO 4 (SW), 35 (DT), 34 (CLK):
 *   - Rotate to change BPM (30–240)
 *   - Press to cycle time signature
 *
 *  Footswitches:
 *    - FOOTSWITCH2_PIN (17): Next track (both edges)
 *    - FOOTSWITCH3_PIN (16): Previous track (both edges)
 *    - FOOTSWITCH4_PIN (33): Toggle Metronome (both edges)
 *
 *  Features:
 *   - WAV streaming on the LEFT channel (with crossfade)
 *   - Metronome click on the RIGHT channel
 *   - When switching files, a crossfade is performed:
 *       The old file fades from full volume (1.0) down to oldMin 
 *       (e.g., 0.1) over CROSSFADE_MS, then the old file is closed.
 *   - Display updates on a separate FreeRTOS task.
 *************************************************************/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"

// ---------------- PIN DEFINITIONS ----------------

// Footswitches
#define FOOTSWITCH2_PIN  17   // Next track
#define FOOTSWITCH3_PIN  16   // Previous track
#define FOOTSWITCH4_PIN  33   // Toggle Metronome

// Rotary Encoder
#define ENCODER_SW_PIN   4
#define ENCODER_DT_PIN   35
#define ENCODER_CLK_PIN  34

// OLED I2C
#define OLED_SDA_PIN     21
#define OLED_SCL_PIN     22

// SD Card (SPI)
#define SD_CS            5
// SCK=18, MISO=19, MOSI=23

// I2S pins for dual MAX98357A (Left & Right)
#define I2S_BCLK         27
#define I2S_LRC          25
#define I2S_DOUT         26

// ---------------- DISPLAY SETTINGS ----------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- GLOBAL VARIABLES ----------------
File currentFile;    // legacy, not used in crossfade

int playingFileIndex   = 0;   // index of the file that is fully playing
int selectedFileIndex  = 0;   // index chosen by user

File root;
String fileList[50];          // up to 50 .wav files
int fileCount = 0;

bool playing = false;

// Pending file selection (for 2-second commit)
bool waitingToPlay     = false;
uint32_t lastInputTime = 0;
uint32_t blinkInterval = 200;
uint32_t lastBlinkToggle = 0;
bool showSelectedName  = true;

// BPM / Metronome
int currentBPM = 120;
int minBPM = 30;
int maxBPM = 240;

int timeSignatures[3][2] = {
  {4, 4},
  {3, 4},
  {6, 8}
};
int currentTimeSigIndex = 0;

// ---------------- CROSSFADE VARIABLES ----------------
// We use two file handles during crossfade:
File oldFile;    // the old file fading out
File newFile;    // the new file fading in
bool crossfadeActive = false;
#define CROSSFADE_MS 3000  // crossfade duration in ms
uint32_t crossfadeSamples = 0;   // total number of samples for crossfade
uint32_t crossfadeProgress = 0;  // processed samples so far
float oldMin = 0.1f;             // old file fades to 0.1 (i.e. 10% volume)

// ---------------- METRONOME VARIABLES ----------------
unsigned long lastClickTime = 0;
int currentBeat = 0;
bool metronomeEnabled = true;   // toggled by footswitch4
bool clickActive = false;
bool accentBeat = false;

#define SAMPLE_RATE       44100
#define CLICK_DURATION_MS 20
#define TWO_PI            6.28318530718

unsigned long clickStartTime = 0;
static uint32_t clickSampleCount = 0;

#define SELECT_TIMEOUT_MS 2000  // 2-second delay for file commit

// ---------------- I2S CONFIG ----------------
static const i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S_MSB,
  .intr_alloc_flags = 0,
  .dma_buf_count = 8,
  .dma_buf_len = 256,
  .use_apll = false,
  .tx_desc_auto_clear = true
};

static const i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_BCLK,
  .ws_io_num  = I2S_LRC,
  .data_out_num = I2S_DOUT,
  .data_in_num  = I2S_PIN_NO_CHANGE
};

// ---------------- DISPLAY TASK DATA ----------------
portMUX_TYPE displayMux = portMUX_INITIALIZER_UNLOCKED;

String g_wavName = "";
int    g_bpm = 120;
bool   g_showName = true;
bool   g_displayNeedsUpdate = false;
int    g_top = 4;
int    g_bottom = 4;

TaskHandle_t displayTaskHandle = NULL;

// ---------- Rotary Encoder State Globals ----------
static bool lastEncoderSW;
static bool lastEncoderCLK;
static bool lastEncoderDT;

// ---------- Footswitch Previous States (for edge detection) ----------
static bool prevFS2 = true;
static bool prevFS3 = true;
static bool prevFS4 = true;

// ----------------------------------------------------
//  FUNCTION PROTOTYPES
// ----------------------------------------------------
void setupI2S();
void setupSD();
void setupOLED();
void listWavFiles();
void startPlaying(int fileIndex);
void startCrossfade(int newIndex);
bool readWavDataAndPlay();
int16_t generateClickSample();
void updateMetronome();

void handleFootswitches();
void nextFile();
void prevFile();
void toggleMetronome();

void setupEncoder();
void handleEncoder();
void cycleTimeSignature();

void requestDisplayUpdate(const String &wav, int bpm, bool showName, int top, int bottom);
void displayTask(void * parameter);

// ----------------------------------------------------
//  SETUP
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(FOOTSWITCH2_PIN, INPUT_PULLUP);
  pinMode(FOOTSWITCH3_PIN, INPUT_PULLUP);
  pinMode(FOOTSWITCH4_PIN, INPUT_PULLUP);

  prevFS2 = digitalRead(FOOTSWITCH2_PIN);
  prevFS3 = digitalRead(FOOTSWITCH3_PIN);
  prevFS4 = digitalRead(FOOTSWITCH4_PIN);

  setupEncoder();
  setupI2S();
  setupSD();
  setupOLED();

  listWavFiles();
  if (fileCount == 0) {
    Serial.println("No valid WAV files found on SD!");
    while (1) { delay(100); }
  }

  playingFileIndex = 0;
  selectedFileIndex = 0;
  startPlaying(playingFileIndex);

  playing = true;

  xTaskCreatePinnedToCore(
    displayTask,
    "DisplayTask",
    4096,
    NULL,
    1,
    &displayTaskHandle,
    0
  );

  requestDisplayUpdate(fileList[playingFileIndex], currentBPM, true,
                       timeSignatures[currentTimeSigIndex][0],
                       timeSignatures[currentTimeSigIndex][1]);
}

// ----------------------------------------------------
//  LOOP
// ----------------------------------------------------
void loop() {
  handleFootswitches();
  handleEncoder();

  if (!readWavDataAndPlay()) {
    if (newFile) {
      newFile.seek(44);
    }
  }

  if (waitingToPlay) {
    if (millis() - lastInputTime >= SELECT_TIMEOUT_MS) {
      startCrossfade(selectedFileIndex);
      waitingToPlay = false;
      requestDisplayUpdate(fileList[selectedFileIndex], currentBPM, true,
                           timeSignatures[currentTimeSigIndex][0],
                           timeSignatures[currentTimeSigIndex][1]);
    } else {
      if (millis() - lastBlinkToggle >= blinkInterval) {
        lastBlinkToggle = millis();
        showSelectedName = !showSelectedName;
        requestDisplayUpdate(fileList[selectedFileIndex], currentBPM,
                             showSelectedName,
                             timeSignatures[currentTimeSigIndex][0],
                             timeSignatures[currentTimeSigIndex][1]);
      }
    }
  }

  updateMetronome();
}

// ----------------------------------------------------
//  Setup Encoder
// ----------------------------------------------------
void setupEncoder() {
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);

  lastEncoderSW  = digitalRead(ENCODER_SW_PIN);
  lastEncoderCLK = digitalRead(ENCODER_CLK_PIN);
  lastEncoderDT  = digitalRead(ENCODER_DT_PIN);
}

// ----------------------------------------------------
//  Handle Encoder
// ----------------------------------------------------
void handleEncoder() {
  bool currentCLK = digitalRead(ENCODER_CLK_PIN);
  bool currentDT  = digitalRead(ENCODER_DT_PIN);

  if (currentCLK != lastEncoderCLK) {
    if (currentDT != currentCLK) {
      currentBPM++;
    } else {
      currentBPM--;
    }
    if (currentBPM < minBPM) currentBPM = minBPM;
    if (currentBPM > maxBPM) currentBPM = maxBPM;

    requestDisplayUpdate(fileList[playingFileIndex],
                         currentBPM,
                         showSelectedName,
                         timeSignatures[currentTimeSigIndex][0],
                         timeSignatures[currentTimeSigIndex][1]);
    Serial.print("Encoder => BPM: ");
    Serial.println(currentBPM);
  }
  lastEncoderCLK = currentCLK;

  bool currentSW = digitalRead(ENCODER_SW_PIN);
  if (lastEncoderSW && !currentSW) {
    Serial.println("Encoder Press => cycleTimeSignature()");
    cycleTimeSignature();
    requestDisplayUpdate(fileList[playingFileIndex],
                         currentBPM,
                         showSelectedName,
                         timeSignatures[currentTimeSigIndex][0],
                         timeSignatures[currentTimeSigIndex][1]);
  }
  lastEncoderSW = currentSW;
}

// ----------------------------------------------------
//  Cycle Time Signature
// ----------------------------------------------------
void cycleTimeSignature() {
  currentTimeSigIndex++;
  if (currentTimeSigIndex >= 3) currentTimeSigIndex = 0;
  Serial.print("Time Sig => ");
  Serial.print(timeSignatures[currentTimeSigIndex][0]);
  Serial.print("/");
  Serial.println(timeSignatures[currentTimeSigIndex][1]);
}

// ----------------------------------------------------
//  I2S Setup
// ----------------------------------------------------
void setupI2S() {
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

// ----------------------------------------------------
//  SD Setup
// ----------------------------------------------------
void setupSD() {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card init failed!");
    while (1) { delay(100); }
  }
  Serial.println("SD Card initialized.");
}

// ----------------------------------------------------
//  OLED Setup
// ----------------------------------------------------
void setupOLED() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed!");
    while (1) { delay(100); }
  }
  display.clearDisplay();
  display.display();
}

// ----------------------------------------------------
//  List WAV Files
// ----------------------------------------------------
void listWavFiles() {
  root = SD.open("/");
  fileCount = 0;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String fname = String(entry.name());
    entry.close();

    if (fname.startsWith("._")) continue;
    if (fname.endsWith(".wav") || fname.endsWith(".WAV")) {
      fileList[fileCount] = fname;
      fileCount++;
      if (fileCount >= 50) break;
    }
  }
  root.close();

  if (fileCount == 0) {
    Serial.println("No valid WAV files found on SD!");
    while (1) { delay(100); }
  }
  Serial.println("WAV Files found:");
  for (int i = 0; i < fileCount; i++) {
    Serial.println(fileList[i]);
  }
}

// ----------------------------------------------------
//  Start Playing (No Crossfade)
// ----------------------------------------------------
void startPlaying(int fileIndex) {
  if (oldFile) { oldFile.close(); }
  if (newFile) { newFile.close(); }
  crossfadeActive = false;

  String fname = "/" + fileList[fileIndex];
  newFile = SD.open(fname);
  if (!newFile) {
    Serial.print("Failed to open ");
    Serial.println(fname);
    return;
  }
  uint8_t header[44];
  newFile.read(header, 44);

  playingFileIndex = fileIndex;
  Serial.print("Now playing (no crossfade): ");
  Serial.println(fname);
}

// ----------------------------------------------------
//  Start Crossfade (From current file to new file)
// ----------------------------------------------------
void startCrossfade(int newIndex) {
  if (!newFile) {
    startPlaying(newIndex);
    return;
  }
  oldFile = newFile;

  String fname = "/" + fileList[newIndex];
  newFile = SD.open(fname);
  if (!newFile) {
    Serial.print("Failed to open new file for crossfade: ");
    Serial.println(fname);
    newFile = oldFile;
    oldFile = File();
    return;
  }
  uint8_t header[44];
  newFile.read(header, 44);

  crossfadeSamples  = (SAMPLE_RATE * CROSSFADE_MS) / 1000UL;
  crossfadeProgress = 0;
  crossfadeActive   = true;

  playingFileIndex = newIndex;
  Serial.print("Crossfading to: ");
  Serial.println(fname);
}

// ----------------------------------------------------
//  Read WAV Data + Interleave w/ Metronome
// ----------------------------------------------------
bool readWavDataAndPlay() {
  if (!oldFile && !newFile) return false;

  const int bufferSize = 256;
  int16_t oldBuf[bufferSize], newBuf[bufferSize];
  memset(oldBuf, 0, sizeof(oldBuf));
  memset(newBuf, 0, sizeof(newBuf));

  int bytesOld = 0;
  if (oldFile) {
    bytesOld = oldFile.read((uint8_t*)oldBuf, bufferSize*sizeof(int16_t));
    if (bytesOld==0) {
      oldFile.close();
      oldFile = File();
      crossfadeActive=false;
    }
  }

  int bytesNew=0;
  if (newFile) {
    bytesNew = newFile.read((uint8_t*)newBuf, bufferSize*sizeof(int16_t));
    if (bytesNew==0) {
      newFile.seek(44);
    }
  }

  int samplesOld = bytesOld/sizeof(int16_t);
  int samplesNew = bytesNew/sizeof(int16_t);
  int maxSamples = max(samplesOld, samplesNew);
  if (maxSamples==0) return false;

  int16_t i2sStereoBuffer[bufferSize*2];

  for (int i=0; i<maxSamples; i++){
    float oldVal = (i<samplesOld) ? (float)oldBuf[i] : 0.0f;
    float newVal = (i<samplesNew) ? (float)newBuf[i] : 0.0f;

    float ratio=1.0f;
    if (crossfadeActive && crossfadeSamples>0) {
      ratio = (float)crossfadeProgress/(float)crossfadeSamples;
      if (ratio>1.0f) ratio=1.0f;
    }

    float oldVol = 1.0f - ratio*(1.0f - oldMin);
    float newVol = ratio;

    float leftMixed = oldVal*oldVol + newVal*newVol;
    int16_t rightSample = generateClickSample();

    i2sStereoBuffer[2*i]     = (int16_t)leftMixed;
    i2sStereoBuffer[2*i + 1] = rightSample;
  }

  size_t bytesWritten;
  i2s_write(I2S_NUM_0, (const char*)i2sStereoBuffer,
            maxSamples*2*sizeof(int16_t),
            &bytesWritten, portMAX_DELAY);

  if (crossfadeActive) {
    crossfadeProgress += maxSamples;
    if (crossfadeProgress>=crossfadeSamples) {
      crossfadeActive=false;
      if (oldFile) {
        oldFile.close();
        oldFile = File();
      }
      Serial.println("Crossfade complete. Old file closed.");
    }
  }
  return true;
}

// ----------------------------------------------------
//  Generate Click Sample (Right channel metronome)
// ----------------------------------------------------
int16_t generateClickSample() {
  // If not active, return silence
  if (!clickActive) {
    return 0;
  }

  // If we've exceeded the click duration, stop
  unsigned long elapsed = millis() - clickStartTime;
  if (elapsed > CLICK_DURATION_MS) {
    clickActive = false;
    return 0;
  }

  // Example frequencies: a bit lower for a more "woody" tone
  float freq = accentBeat ? 800.0f : 600.0f;  // accent vs normal
  // Example base amplitude
  float baseAmplitude = accentBeat ? 4000.0f : 3000.0f;

  // Time in seconds since we started the click
  float t = (float)(clickSampleCount++) / SAMPLE_RATE;

  // Add a short exponential decay envelope for a percussive "wood block" feel.
  // Tweak 'decayRate' to control how fast it decays.
  float decayRate = 50.0f; // bigger = faster decay
  float envelope = expf(-t * decayRate);

  // Calculate the sine wave with envelope
  float val = sinf(TWO_PI * freq * t) * envelope;

  // Scale by amplitude
  return (int16_t)(val * baseAmplitude);
}

// ----------------------------------------------------
//  Update Metronome
// ----------------------------------------------------
void updateMetronome() {
  if (!metronomeEnabled) return;
  float msPerBeat = 60000.0f/(float)currentBPM;
  int beatsPerMeasure = timeSignatures[currentTimeSigIndex][0];
  unsigned long nowMs = millis();
  if ((nowMs-lastClickTime)>=msPerBeat) {
    lastClickTime=nowMs;
    currentBeat++;
    if (currentBeat>beatsPerMeasure) currentBeat=1;
    accentBeat=(currentBeat==1);
    clickActive=true;
    clickStartTime=nowMs;
    clickSampleCount=0;
  }
}

// ----------------------------------------------------
//  Handle Footswitches
// ----------------------------------------------------
void handleFootswitches() {
  bool fs2State=digitalRead(FOOTSWITCH2_PIN);
  bool fs3State=digitalRead(FOOTSWITCH3_PIN);
  bool fs4State=digitalRead(FOOTSWITCH4_PIN);

  // FS2
  if (prevFS2 && !fs2State) {
    Serial.println("FS2 FALLING => nextFile()");
    nextFile();
  }
  if (!prevFS2 && fs2State) {
    Serial.println("FS2 RISING => nextFile()");
    nextFile();
  }

  // FS3
  if (prevFS3 && !fs3State) {
    Serial.println("FS3 FALLING => prevFile()");
    prevFile();
  }
  if (!prevFS3 && fs3State) {
    Serial.println("FS3 RISING => prevFile()");
    prevFile();
  }

  // FS4
  if (prevFS4 && !fs4State) {
    Serial.println("FS4 FALLING => toggleMetronome()");
    toggleMetronome();
  }
  if (!prevFS4 && fs4State) {
    Serial.println("FS4 RISING => toggleMetronome()");
    toggleMetronome();
  }

  prevFS2=fs2State;
  prevFS3=fs3State;
  prevFS4=fs4State;
}

// ----------------------------------------------------
//  Next File
// ----------------------------------------------------
void nextFile() {
  selectedFileIndex++;
  if (selectedFileIndex>=fileCount) selectedFileIndex=0;
  waitingToPlay=true;
  lastInputTime=millis();
  lastBlinkToggle=millis();
  showSelectedName=true;
  Serial.println("Next file selected (pending commit)...");
}

// ----------------------------------------------------
//  Previous File
// ----------------------------------------------------
void prevFile() {
  selectedFileIndex--;
  if (selectedFileIndex<0) selectedFileIndex=fileCount-1;
  waitingToPlay=true;
  lastInputTime=millis();
  lastBlinkToggle=millis();
  showSelectedName=true;
  Serial.println("Previous file selected (pending commit)...");
}

// ----------------------------------------------------
//  Toggle Metronome
// ----------------------------------------------------
void toggleMetronome() {
  metronomeEnabled=!metronomeEnabled;
  if (!metronomeEnabled) {
    clickActive=false;
  }
  Serial.print("Metronome is now ");
  Serial.println(metronomeEnabled?"ENABLED":"DISABLED");
}

// ----------------------------------------------------
//  REQUEST DISPLAY UPDATE
// ----------------------------------------------------
void requestDisplayUpdate(const String &wav, int bpm, bool showName, int top, int bottom) {
  portENTER_CRITICAL(&displayMux);
  g_wavName          = wav;
  g_bpm              = bpm;
  g_showName         = showName;
  g_top             = top;
  g_bottom          = bottom;
  g_displayNeedsUpdate = true;
  portEXIT_CRITICAL(&displayMux);
}

// ----------------------------------------------------
//  DISPLAY TASK (Runs on Core 0) - MINIMAL CHANGE
// ----------------------------------------------------
void displayTask(void * parameter) {
  while(true) {
    vTaskDelay(50 / portTICK_PERIOD_MS);

    portENTER_CRITICAL(&displayMux);
    bool updateNeeded   = g_displayNeedsUpdate;
    String localWav     = g_wavName;
    int    localBpm     = g_bpm;
    bool   localShow    = g_showName;
    int    localTop     = g_top;
    int    localBot     = g_bottom;
    g_displayNeedsUpdate = false;
    portEXIT_CRITICAL(&displayMux);

    if (!waitingToPlay) {
      localShow = true;  // always show the name if not in blinking mode
    }

    if (updateNeeded) {
      display.clearDisplay();
      display.setTextColor(WHITE);

      // Time signature top-left
      display.setTextSize(1);
      display.setCursor(0,0);
      display.print(localTop);
      display.print("/");
      display.print(localBot);

      // BPM top-right
      display.setCursor(80,0);
      display.print(localBpm);
      display.print(" Bpm");

      // Centered file name
      display.setTextSize(2);
      String shortName = localWav;
      shortName.replace(".wav","");
      shortName.replace(".WAV","");
      shortName.replace("/","");

      int16_t textWidth = shortName.length() * (6 * 2);
      int16_t startX = (SCREEN_WIDTH - textWidth)/2;
      int16_t startY = (SCREEN_HEIGHT/2) - 8;

      display.setCursor(startX, startY);

      if (localShow) {
        display.println(shortName);
      } else {
        for (unsigned int i=0; i<shortName.length(); i++){
          display.print(" ");
        }
      }

      display.display();
    }
  }
}
