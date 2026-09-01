// ============================================================================
//  Smart ePod - Brain 1: ESP32 WROVER
//  Audio output, OLED user interface, resistor-ladder keypad.
//
//  ---------------------------------------------------------------------------
//  THE ONE INVARIANT: the DAC is fed on every single loop pass, forever.
//
//  Audio when there is audio, digital silence otherwise - while browsing, while
//  paused, while skipping, during diagnostics. The ESP32's continuous DAC runs
//  from a chain of DMA descriptors; if that chain is ever allowed to empty the
//  engine stops, and from then on every write times out waiting for a
//  descriptor that will never come free. Playback stalls with a full buffer and
//  no error anywhere. That failure only ever showed up on track transitions,
//  because those were the moments nothing was being written.
//
//  Never let the chain run dry and the whole class of bug disappears.
//  ---------------------------------------------------------------------------
//
//  This board owns no library state. The playlist, the selection and the file
//  cursor live on the XIAO S3; this side is a display, a keypad and a sample
//  clock. It reports button presses upward and renders whatever it is told.
//
//  Everything here is non-blocking: one loop, explicit state, no waiting.
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/dac_continuous.h>
#include <esp_system.h>

// A crash leaves no output at all, so leave a trail. RTC memory survives a
// reset, so the last place the firmware was is still there on the way back up.
RTC_NOINIT_ATTR static char crumb[24];
RTC_NOINIT_ATTR static uint32_t crumbMagic;
#define CRUMB_MAGIC 0x43524D42
static inline void crumbSet(const char* where) {
  strncpy(crumb, where, sizeof(crumb) - 1);
  crumb[sizeof(crumb) - 1] = '\0';
  crumbMagic = CRUMB_MAGIC;
}

// ---------------------------------------------------------------- pins -----
#define RX2_PIN   27   // from XIAO TX (GPIO 43)
#define TX2_PIN   32   // to   XIAO RX (GPIO 44)
#define BTN_PAD   34   // resistor ladder, ADC1
#define LED_R     18
#define LED_G     19
#define LED_B     23
#define OLED_SDA  21
#define OLED_SCL  22

// Battery sensing is optional and off by default: it needs a divider that the
// stock build does not have. To enable, fit two 100k resistors from the
// switched battery positive to GND with the midpoint on GPIO 35, then set
// BATT_SENSE_ENABLED to 1. Without it the icon is drawn empty with a dash,
// which is honest rather than a permanently full battery.
#define BATT_PIN            35
#define BATT_SENSE_ENABLED  0
#define BATT_DIVIDER        2.0f      // 100k / 100k
#define BATT_FULL_MV        4100
#define BATT_EMPTY_MV       3300

// 460800, not 921600. Audio needs 22050 * 10 = 220500 bps, so this is still
// more than 2x headroom, and halving the bit rate halves the exposure to
// crosstalk between the TX and RX jumper wires.
#define UART_BAUD       460800
#define UART_RX_BUFFER  16384

// --------------------------------------------------------------- audio -----
// 22050 Hz: with DAC_DIGI_CLK_SRC_DEFAULT the ESP32's continuous DAC will not
// configure below 19.6 kHz.
#define AUDIO_RATE      22050
#define DMA_DESC_NUM    16
#define DMA_BUF_SIZE    1024          // 512 samples per descriptor after
                                      // 16-bit alignment; 16 x 512 = ~371 ms
#define AUDIO_SILENCE   0x80          // mid-scale = no signal on an 8-bit DAC

// dac_continuous_write() consumes a whole DMA descriptor per internal
// iteration however few bytes it is given, so small writes silently destroy
// the runway. Never hand it less than DAC_PUSH_MIN.
#define DAC_PUSH_CHUNK  2048
#define DAC_PUSH_MIN    1024
#define DAC_PUSH_TIMEOUT_MS 20

// EVERY write length must be a multiple of DAC_ALIGN, and this is not a
// nicety - it is the difference between music and a screech.
//
// The DAC's DMA runs through I2S in 16-bit stereo (I2S_STD_SLOT_BOTH), so the
// hardware frame is 4 bytes. The driver expands each byte we hand it into the
// HIGH half of a 16-bit slot and never writes the low half, so a descriptor
// whose length is not a multiple of 4 splits a frame: everything after it
// shifts by two bytes and the DAC starts reading the low halves, which are
// always zero. Real sample, zero, real sample, zero.
//
// load_bytes = our length * 2, so our length must be even. The driver may also
// halve a short write internally (its "split" path), so 8 keeps it even
// through that too.
//
// Tones never hit this because they are pushed in fixed 2048-byte chunks with
// an even remainder. The stream pushed whatever the ring happened to hold -
// odd about half the time.
#define DAC_ALIGN 8

// The stream is a raw byte pipe with no framing, so the sender marks the end
// with a run of zeros. Audio is clamped away from 0x00 upstream, so the marker
// cannot occur inside music.
#define STREAM_END_RUN  32
#define STREAM_IDLE_MS  5000          // backstop if the sender vanishes
#define STREAM_DRAIN_MS 150           // swallow stragglers before text resumes
#define RING_SIZE       8192
#define STREAM_PREFILL  4096          // lead-in before the first sample plays

#define BUF_REPORT_MS   50
#define OLED_REFRESH_MS 500

// ------------------------------------------------------------- buttons -----
// Measured on the ladder pad, 12-bit ADC:
//
//   LEFT    ~0        PREV / up
//   MIDDLE  200-300   SELECT
//   RIGHT   ~818      NEXT / down
//   idle    4095
//
// The gap between the highest button (~820) and the resting level (4095) is
// enormous, so plain fixed bands are both sufficient and more predictable than
// anything adaptive. Each band has room either side; anything falling outside
// all three is a reading caught mid-transition and is treated as no press.
//
// These numbers belong to this pad. If it is ever rebuilt, send BTNLOG and
// read them off the console rather than guessing.
#define BTN_LEFT_MAX    120           // PREV
#define BTN_MID_MIN     150           // SELECT
#define BTN_MID_MAX     500
#define BTN_RIGHT_MIN   600           // NEXT
#define BTN_RIGHT_MAX   1200
#define BTN_IDLE_MIN    1500          // above this, nothing is pressed

#define BTN_POLL_MS    30
#define BTN_STABLE     3              // 3 x 30 ms = 90 ms to register
#define BTN_LONG_MS    800
#define BTN_GAP_MS     180
#define BTN_REPEAT_MS  280
#define BTN_LOG_MS     250

enum Button { BTN_NONE, BTN_PREV, BTN_SELECT, BTN_NEXT };

// -------------------------------------------------------------- volume -----
// Applied through a 256-entry lookup on the way to the DAC. VOL_UNITY is
// already the loudest the hardware goes: the DAC range is fixed at 0-3.3 V and
// the 220 ohm series resistors set the level into the earphones. Above unity
// the table re-scales and clips, trading distortion for apparent loudness.
#define VOL_UNITY 16
#define VOL_MAX   24

// ============================================================================
//  State
// ============================================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledReady = false;

dac_continuous_handle_t dacHandle = NULL;
bool dacReady = false;
bool dacCyclic = false;

static uint8_t ringBuf[RING_SIZE];
static uint8_t stageBuf[DAC_PUSH_CHUNK];
static uint8_t silenceBuf[DAC_PUSH_CHUNK];

// --- link ---
// The S3->WROVER direction carries text or raw PCM, never both. DRAIN is the
// short window after a stream ends where stragglers are swallowed so the next
// command is not parsed out of the middle of the audio.
enum LinkMode { LINK_TEXT, LINK_PCM, LINK_DRAIN };
LinkMode linkMode = LINK_TEXT;

char cmdLine[160];
uint8_t cmdLen = 0;

// --- stream ---
uint16_t ringHead = 0, ringTail = 0, ringLevel = 0;
uint32_t streamPlayed = 0;
uint32_t streamTotal = 0;
uint8_t  zeroRun = 0;
bool streamPaused = false;
bool streamPrimed = false;
bool streamEnded = false;
bool streamAbort = false;             // skip requested: mute and race to marker
uint32_t streamDry = 0;
uint32_t streamGap = 0;               // silence blocks forced into the music
uint32_t streamMinBuf = 0xFFFFFFFF;
bool streamWasDry = false;
unsigned long streamLastData = 0;
unsigned long streamDrainAt = 0;

// --- ui ---
enum Screen { SCREEN_SPLASH, SCREEN_HOME, SCREEN_LIST, SCREEN_PLAY, SCREEN_REC, SCREEN_SETTINGS };

// Home is a three-item menu. Two folders and the recorder - which is the whole
// device, so there is nothing else to put on it.
enum HomeItem { HOME_MUSIC, HOME_RECORDINGS, HOME_RECORD, HOME_SETTINGS, HOME_ITEMS };
int homeSel = 0;
Screen screen = SCREEN_SPLASH;
char uiTrack[41] = "";
int  uiIndex = 0;
int  uiTotal = 0;
char uiState[12] = "STOPPED";
char uiMessage[41] = "";
unsigned long uiMessageUntil = 0;
unsigned long splashUntil = 0;
bool uiDirty = true;
unsigned long lastOledDraw = 0;

// Settings & progress state variables
bool settingsBle = false;
bool settingsWifi = false;
bool settingsAudio = false;
bool settingsVision = false;
char settingsSsid[32] = "ePod-Music";
char settingsPass[32] = "epodmusicpass";
char settingsIp[20] = "192.168.4.1";
int settingsSel = 0;
int progressPercent = -2;
char progressText[26] = "";

// Library summary for the home screen, pushed by the S3.
int libTracks = 0;
uint32_t libSeconds = 0;
uint32_t libCardMB = 0;
int libMusic = 0;                     // files in /music
int libRecordings = 0;                // files in /recordings
int libSource = 0;                    // 0 = music, 1 = recordings

// Recording readout, pushed by the S3 four times a second.
uint32_t recSeconds = 0;
int recLevel = 0;

// A window of the library for the list screen. The S3 owns the playlist and
// sends only the rows that are on screen.
#define LIST_ROWS 4
char rowName[LIST_ROWS][26];
int rowFirst = 0;
int rowSelSlot = 0;

// Overlay shown while the S3 is busy, e.g. reading a freshly inserted card.
#define BUSY_MS 900
char busyText[26] = "";
unsigned long busyStart = 0;
bool busyActive = false;

uint8_t volume = VOL_UNITY;
uint8_t volTable[256];

unsigned long lastBtnPoll = 0;
unsigned long lastBufReport = 0;
// Live readout, toggled with BTNLOG. Deliberately keeps running during
// playback: the rail sags most while the DAC, SD card and OLED are all busy,
// and that is exactly when the thresholds need checking.
bool btnLog = false;
unsigned long lastBtnLog = 0;
#define BTN_LOG_MS 250
Button btnCandidate = BTN_NONE;
uint8_t btnStableCount = 0;
Button btnHeld = BTN_NONE;
unsigned long btnHeldSince = 0;
unsigned long btnRepeatAt = 0;
unsigned long lastBtnEvent = 0;
bool btnLongFired = false;

// --- diagnostics ---
enum Waveform { WAVE_SINE, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE };
Waveform lastWave = WAVE_SQUARE;
bool stopRequested = false;

// ---------------------------------------------------------- prototypes -----
bool dacBegin();
void serviceAudio();
void serviceLink();
void serviceUi();
void drawScreen();
void handleCommand(char* line);
void beginStream(uint32_t totalBytes);
void finishStream();
void sendLine(const char* text);
void rebuildVolTable();
void adjustVolume(int delta);
Button readButton();
void pollButtons();
void drawScreen();
void setMessage(const char* text);
static void drawSettings();
static void drawProgress();
uint8_t batteryPercent();
void reportResetReason();
void printHelp();
void printStatus();
void playTone(Waveform wave, uint32_t freq, uint32_t durationMs);
uint8_t waveSample(Waveform wave, uint32_t phase);
bool parseWaveform(const char* name, Waveform& out);
const char* waveformName(Waveform wave);
void testLED();
void testAudio(const char* args);
void testOLED();
void testButton();
void testQuiet();
bool idleWait(uint32_t ms);

// ============================================================================
//  DAC - the engine that must never stop
// ============================================================================
bool dacBegin() {
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_ALL;          // GPIO 25 + GPIO 26
  cfg.desc_num  = DMA_DESC_NUM;
  cfg.buf_size  = DMA_BUF_SIZE;
  cfg.freq_hz   = AUDIO_RATE;
  cfg.offset    = 0;
  cfg.clk_src   = DAC_DIGI_CLK_SRC_DEFAULT;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;        // same mono data to both ears

  if (dac_continuous_new_channels(&cfg, &dacHandle) != ESP_OK) return false;
  if (dac_continuous_enable(dacHandle) != ESP_OK) return false;
  return true;
}

// Idle output. Instead of writing silence over and over - which only kept the
// descriptor chain alive if every write landed in time, and wedged the driver
// when one did not - the DMA is handed a repeating buffer once. The hardware
// then loops it forever with no further writes, so the chain physically cannot
// empty while nothing is playing.
//
// The next ordinary dac_continuous_write() breaks the loop; the driver resets
// the descriptor pool itself on that transition.
void dacIdleSilence() {
  if (!dacReady || dacCyclic) return;
  size_t loaded = 0;
  if (dac_continuous_write_cyclically(dacHandle, silenceBuf, DAC_PUSH_CHUNK,
                                      &loaded) == ESP_OK) {
    dacCyclic = true;
  }
}

// Feeds the DAC on every pass, from whatever source is appropriate.
void serviceAudio() {
  if (linkMode != LINK_PCM) {
    dacIdleSilence();                            // idle, browsing, draining
    return;
  }

  // --- pull whatever has arrived into the ring, scanning for the marker ---
  if (!streamPaused || streamAbort) {
    int avail = Serial2.available();
    while (avail > 0 && ringLevel < RING_SIZE && !streamEnded) {
      uint16_t space = RING_SIZE - ringLevel;
      uint16_t contiguous = RING_SIZE - ringHead;
      uint16_t want = space < contiguous ? space : contiguous;
      if (static_cast<int>(want) > avail) want = static_cast<uint16_t>(avail);
      size_t got = Serial2.readBytes(&ringBuf[ringHead], want);
      if (got == 0) break;

      for (size_t i = 0; i < got; i++) {
        if (ringBuf[ringHead + i] == 0x00) {
          ringBuf[ringHead + i] = AUDIO_SILENCE;  // mute, do not slam the rail
          if (++zeroRun >= STREAM_END_RUN) {
            got = i + 1;
            streamEnded = true;
            break;
          }
        } else {
          zeroRun = 0;
        }
      }

      ringHead = static_cast<uint16_t>((ringHead + got) % RING_SIZE);
      ringLevel = static_cast<uint16_t>(ringLevel + got);
      avail -= static_cast<int>(got);
      streamLastData = millis();
    }
  }

  // --- health, before anything consumes the ring ---
  uint32_t backlog = ringLevel + Serial2.available();
  if (!streamEnded && !streamPaused && backlog < streamMinBuf) streamMinBuf = backlog;
  if (!streamEnded && !streamPaused && !streamAbort && ringLevel == 0) {
    if (!streamWasDry) {
      streamDry++;
      streamWasDry = true;
    }
  } else if (ringLevel > 0) {
    streamWasDry = false;
  }

  // --- feed the DAC ---
  if (streamPaused && !streamAbort) {
    dacIdleSilence();                            // hold output, keep ring intact
    streamLastData = millis();
    return;
  }

  if (!streamPrimed) {
    // Build a lead-in before the first sample plays, so the DMA queue starts
    // deep rather than hovering one chunk above empty. Silence keeps the
    // engine turning meanwhile.
    if (ringLevel >= STREAM_PREFILL || streamEnded) streamPrimed = true;
    else {
      dacIdleSilence();
      return;
    }
  }

  // Push whatever the ring holds, rounded down to the DMA frame. An earlier
  // version waited for DAC_PUSH_MIN and pushed a block of SILENCE otherwise,
  // which dropped 93 ms holes into the music every time the ring dipped.
  // A small write wastes a DMA descriptor; a silence block destroys the audio.
  size_t pushable = ringLevel;
  if (pushable > DAC_PUSH_CHUNK) pushable = DAC_PUSH_CHUNK;
  pushable &= ~static_cast<size_t>(DAC_ALIGN - 1);

  // A tail shorter than one frame cannot be played without breaking alignment.
  // At 22050 Hz that is under a third of a millisecond, so drop it and let the
  // stream close.
  if (pushable == 0 && streamEnded && ringLevel > 0) {
    ringTail = static_cast<uint16_t>((ringTail + ringLevel) % RING_SIZE);
    streamPlayed += ringLevel;
    ringLevel = 0;
  }

  if (pushable == 0) {
    // Genuinely nothing to play. Silence keeps the DMA chain turning, and this
    // is the one case that is honestly a gap - counted as such.
    streamGap++;
    dacIdleSilence();
    return;
  }

  if (streamAbort) {
    // Skipping: discard the audio but keep the engine fed, so the chain never
    // empties on the way to the marker.
    ringTail = static_cast<uint16_t>((ringTail + pushable) % RING_SIZE);
    ringLevel = static_cast<uint16_t>(ringLevel - pushable);
    streamPlayed += pushable;
    dacIdleSilence();
    return;
  }

  for (size_t i = 0; i < pushable; i++) {
    stageBuf[i] = volTable[ringBuf[(ringTail + i) % RING_SIZE]];
  }
  size_t loaded = 0;
  dacCyclic = false;                             // a real write ends the loop
  dac_continuous_write(dacHandle, stageBuf, pushable, &loaded, DAC_PUSH_TIMEOUT_MS);
  loaded &= ~static_cast<size_t>(DAC_ALIGN - 1); // stay frame aligned
  ringTail = static_cast<uint16_t>((ringTail + loaded) % RING_SIZE);
  ringLevel = static_cast<uint16_t>(ringLevel - loaded);
  streamPlayed += loaded;
}

// ============================================================================
//  Stream lifecycle
// ============================================================================
void beginStream(uint32_t totalBytes) {
  ringHead = ringTail = ringLevel = 0;
  zeroRun = 0;
  streamPlayed = 0;
  streamTotal = totalBytes;
  streamPaused = false;
  streamPrimed = false;
  streamEnded = false;
  streamAbort = false;
  streamDry = 0;
  streamGap = 0;
  streamMinBuf = 0xFFFFFFFF;
  streamWasDry = false;
  streamLastData = millis();
  strncpy(uiState, "PLAYING", sizeof(uiState) - 1);
  screen = SCREEN_PLAY;
  busyActive = false;
  uiDirty = true;
  linkMode = LINK_PCM;
}

void finishStream() {
  linkMode = LINK_DRAIN;
  streamDrainAt = millis() + STREAM_DRAIN_MS;
  streamPaused = false;
  strncpy(uiState, "STOPPED", sizeof(uiState) - 1);
  if (screen == SCREEN_PLAY) screen = SCREEN_LIST;
  uiDirty = true;
}

// Watches for the conditions that end a stream. Called from the main loop, not
// from inside the audio path, so nothing here can stall the DAC.
static void checkStreamEnd() {
  if (linkMode == LINK_PCM) {
    if (streamEnded && ringLevel == 0) finishStream();
    else if (!streamEnded && ringLevel == 0 &&
             millis() - streamLastData > STREAM_IDLE_MS) finishStream();
    return;
  }

  if (linkMode == LINK_DRAIN) {
    while (Serial2.available()) Serial2.read();
    if (static_cast<int32_t>(millis() - streamDrainAt) >= 0) {
      char line[64];
      if (streamMinBuf == 0xFFFFFFFF) streamMinBuf = 0;
      snprintf(line, sizeof(line), "STAT dry=%lu gap=%lu minbuf=%lu",
               static_cast<unsigned long>(streamDry),
               static_cast<unsigned long>(streamGap),
               static_cast<unsigned long>(streamMinBuf));
      sendLine(line);
      snprintf(line, sizeof(line), "END %lu", static_cast<unsigned long>(streamPlayed));
      sendLine(line);
      linkMode = LINK_TEXT;
      cmdLen = 0;
    }
  }
}

// ============================================================================
//  Link - text commands, parsed without ever blocking
// ============================================================================
void sendLine(const char* text) {
  Serial2.println(text);
}

void serviceLink() {
  if (linkMode != LINK_TEXT) return;
  while (Serial2.available()) {
    char c = static_cast<char>(Serial2.read());
    if (c == '\r' || c == '\n') {
      if (cmdLen > 0) {
        cmdLine[cmdLen] = '\0';
        cmdLen = 0;
        handleCommand(cmdLine);
        return;                                  // one command per pass
      }
    } else if (c >= 32 && c <= 126 && cmdLen < sizeof(cmdLine) - 1) {
      cmdLine[cmdLen++] = c;
    }
  }
}

static void copyField(char* dst, size_t dstSize, const char* src) {
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

void handleCommand(char* line) {
  // TRACK and MSG carry free text, so only the keyword may be case-folded.
  if (strncmp(line, "TRACK ", 6) == 0) {
    // TRACK <index> <total> <name...>
    char* p = line + 6;
    uiIndex = atoi(p);
    char* sp = strchr(p, ' ');
    if (sp) {
      uiTotal = atoi(sp + 1);
      char* sp2 = strchr(sp + 1, ' ');
      if (sp2) copyField(uiTrack, sizeof(uiTrack), sp2 + 1);
    }
    uiDirty = true;
    return;
  }
  if (strncmp(line, "STATE ", 6) == 0) {
    copyField(uiState, sizeof(uiState), line + 6);
    // Leaving playback returns to the library rather than the home screen:
    // that is where the user was, and where they most likely want to be.
    if (strcmp(uiState, "PLAYING") != 0 && screen == SCREEN_PLAY) {
      screen = SCREEN_LIST;
    }
    uiDirty = true;
    return;
  }
  if (strncmp(line, "MSG ", 4) == 0) {
    setMessage(line + 4);
    return;
  }
  if (strncmp(line, "BUSY ", 5) == 0) {
    copyField(busyText, sizeof(busyText), line + 5);
    busyStart = millis();
    busyActive = true;
    uiDirty = true;
    return;
  }
  if (strncmp(line, "INFO ", 5) == 0) {
    // INFO <tracks> <seconds> <cardMB>
    const char* p = line + 5;
    libTracks = atoi(p);
    const char* sp = strchr(p, ' ');
    if (sp) {
      libSeconds = static_cast<uint32_t>(atol(sp + 1));
      const char* sp2 = strchr(sp + 1, ' ');
      if (sp2) libCardMB = static_cast<uint32_t>(atol(sp2 + 1));
    }
    uiDirty = true;
    return;
  }
  if (strncmp(line, "SRC ", 4) == 0) {
    // SRC <0|1> <musicCount> <recCount>
    libSource = atoi(line + 4);
    const char* sp = strchr(line + 4, ' ');
    if (sp) {
      libMusic = atoi(sp + 1);
      const char* sp2 = strchr(sp + 1, ' ');
      if (sp2) libRecordings = atoi(sp2 + 1);
    }
    uiDirty = true;
    return;
  }
  if (strncmp(line, "SETTINGS ", 9) == 0) {
    // SETTINGS <ble> <wifi> <ssid> <pass> <ip> <voice>
    //
    // Split into fields first. The old nested-strchr version stopped after the
    // IP, so the Voice AI flag was never read - the menu showed OFF for ever
    // and every press sent AUDIO_ON - and the unterminated IP field swallowed
    // that trailing flag into the address shown on screen.
    char* field[7] = {0};
    int n = 0;
    for (char* p = line + 9; *p && n < 7; ) {
      field[n++] = p;
      char* sp = strchr(p, ' ');
      if (!sp) break;
      *sp = 0;
      p = sp + 1;
    }
    if (n > 0) settingsBle   = atoi(field[0]) != 0;
    if (n > 1) settingsWifi  = atoi(field[1]) != 0;
    if (n > 2) copyField(settingsSsid, sizeof(settingsSsid), field[2]);
    if (n > 3) copyField(settingsPass, sizeof(settingsPass), field[3]);
    if (n > 4) copyField(settingsIp,   sizeof(settingsIp),   field[4]);
    if (n > 5) settingsAudio  = atoi(field[5]) != 0;
    if (n > 6) settingsVision = atoi(field[6]) != 0;
    uiDirty = true;
    return;
  }
  if (strncmp(line, "PROGRESS ", 9) == 0) {
    progressPercent = atoi(line + 9);
    const char* space = strchr(line + 9, ' ');
    if (space) {
      copyField(progressText, sizeof(progressText), space + 1);
    } else {
      progressText[0] = 0;
    }
    uiDirty = true;
    return;
  }
  if (strncmp(line, "REC ", 4) == 0) {
    // REC <seconds> <peakPercent>
    recSeconds = static_cast<uint32_t>(atol(line + 4));
    const char* sp = strchr(line + 4, ' ');
    recLevel = sp ? atoi(sp + 1) : 0;
    screen = SCREEN_REC;
    uiDirty = true;
    return;
  }
  if (strncmp(line, "RECEND ", 7) == 0) {
    // RECEND ok <name>   |   RECEND fail <reason>
    const char* rest = line + 7;
    char note[40];
    if (strncmp(rest, "ok ", 3) == 0) {
      snprintf(note, sizeof(note), "Saved %s", rest + 3);
    } else {
      const char* why = strchr(rest, ' ');
      snprintf(note, sizeof(note), "Not saved: %s", why ? why + 1 : "failed");
    }
    setMessage(note);
    screen = SCREEN_LIST;
    return;
  }
  if (strncmp(line, "WIN ", 4) == 0) {
    // WIN <firstIndex> <selectedSlot>
    rowFirst = atoi(line + 4);
    const char* sp = strchr(line + 4, ' ');
    rowSelSlot = sp ? atoi(sp + 1) : 0;
    if (rowSelSlot < 0) rowSelSlot = 0;
    if (rowSelSlot >= LIST_ROWS) rowSelSlot = LIST_ROWS - 1;
    uiDirty = true;
    return;
  }
  if (strncmp(line, "ROW ", 4) == 0) {
    // ROW <slot> <name>   (name may be empty to blank the row)
    int slot = atoi(line + 4);
    const char* sp = strchr(line + 4, ' ');
    if (slot >= 0 && slot < LIST_ROWS) {
      copyField(rowName[slot], sizeof(rowName[slot]), sp ? sp + 1 : "");
    }
    uiDirty = true;
    return;
  }

  for (char* q = line; *q; q++) *q = toupper(*q);

  if (strncmp(line, "AUDIO_BEGIN", 11) == 0) {
    // AUDIO_BEGIN <rate> <totalBytes>
    const char* sp = strchr(line + 11, ' ');
    uint32_t total = 0;
    if (sp) {
      const char* sp2 = strchr(sp + 1, ' ');
      if (sp2) total = static_cast<uint32_t>(atol(sp2 + 1));
    }
    beginStream(total);
  } else if (strcmp(line, "AUDIO_STOP") == 0) {
    if (linkMode == LINK_PCM) streamAbort = true;
  } else if (strcmp(line, "HELP") == 0) {
    printHelp();
  } else if (strcmp(line, "STATUS") == 0) {
    printStatus();
  } else if (strcmp(line, "PING") == 0) {
    sendLine("PONG - WROVER link is active");
  } else if (strcmp(line, "VOL") == 0) {
    char l[48];
    snprintf(l, sizeof(l), "VOL %u", volume);
    sendLine(l);
    snprintf(l, sizeof(l), "Volume %u of %u (%u = unity)", volume, VOL_MAX, VOL_UNITY);
    sendLine(l);
  } else if (strncmp(line, "VOL ", 4) == 0) {
    adjustVolume(atoi(line + 4) - static_cast<int>(volume));
  } else if (strcmp(line, "LED") == 0) {
    testLED();
  } else if (strcmp(line, "AUDIO") == 0) {
    testAudio("");
  } else if (strncmp(line, "AUDIO ", 6) == 0) {
    testAudio(line + 6);
  } else if (strcmp(line, "OLED") == 0) {
    testOLED();
  } else if (strcmp(line, "BTN") == 0) {
    testButton();
  } else if (strcmp(line, "QUIET") == 0) {
    testQuiet();
  } else if (strncmp(line, "BTNLOG", 6) == 0) {
    if (strcmp(line, "BTNLOG ON") == 0)       btnLog = true;
    else if (strcmp(line, "BTNLOG OFF") == 0) btnLog = false;
    else                                      btnLog = !btnLog;   // bare toggle
    sendLine(btnLog ? "Button logging ON - stays on through playback."
                    : "Button logging OFF");
  } else if (strcmp(line, "BATT") == 0) {
    char l[64];
    snprintf(l, sizeof(l), "Battery sense %s, raw=%d, %u%%",
             BATT_SENSE_ENABLED ? "on" : "OFF (see BATT_SENSE_ENABLED)",
             BATT_SENSE_ENABLED ? analogRead(BATT_PIN) : -1, batteryPercent());
    sendLine(l);
  } else {
    char l[96];
    snprintf(l, sizeof(l), "Unknown Command: %s", line);
    sendLine(l);
    sendLine("Send HELP for the command list.");
  }
}

void printHelp() {
  sendLine("WROVER commands:");
  sendLine("  HELP STATUS PING BATT");
  sendLine("  TRACK <i> <n> <name>  - set display");
  sendLine("  STATE <text>          - set status line");
  sendLine("  MSG <text>            - transient notice");
  sendLine("  BUSY <text>           - loading overlay");
  sendLine("  INFO <tracks> <secs> <cardMB>");
  sendLine("  WIN <first> <slot> / ROW <slot> <name>");
  sendLine("  SRC <src> <music> <recs>  - folder counts");
  sendLine("  REC <sec> <level> / RECEND <ok|fail> <name>");
  sendLine("  AUDIO_BEGIN <rate> <bytes> - enter PCM mode");
  sendLine("  VOL [0-24]            - volume, 16 = unity");
  sendLine("  LED OLED BTN QUIET");
  sendLine("  BTNLOG [ON|OFF]       - live button ADC readout");
  sendLine("  AUDIO <wave> [freq] [ms]  SINE SQUARE TRI SAW NOISE SWEEP ALL");
  sendLine("Buttons: LEFT/RIGHT tap = prev/next, hold = volume");
  sendLine("         MIDDLE tap = play/pause, hold = stop");
}

void printStatus() {
  char l[64];
  snprintf(l, sizeof(l), "Audio: %d Hz 8-bit DMA DAC, %s", AUDIO_RATE,
           dacReady ? "ok" : "FAILED");
  sendLine(l);
  snprintf(l, sizeof(l), "UART: %d 8-N-1", UART_BAUD);
  sendLine(l);
  snprintf(l, sizeof(l), "OLED: %s   Volume: %u", oledReady ? "ok" : "FAILED", volume);
  sendLine(l);
}

// ============================================================================
//  Volume
// ============================================================================
void rebuildVolTable() {
  for (int i = 0; i < 256; i++) {
    int32_t scaled = ((i - 128) * volume) / VOL_UNITY;
    if (scaled > 127) scaled = 127;
    if (scaled < -128) scaled = -128;
    volTable[i] = static_cast<uint8_t>(scaled + 128);
  }
}

void adjustVolume(int delta) {
  int next = static_cast<int>(volume) + delta;
  if (next > VOL_MAX) next = VOL_MAX;
  if (next < 0) next = 0;
  if (next == static_cast<int>(volume)) return;
  volume = static_cast<uint8_t>(next);
  rebuildVolTable();
  char l[24];
  snprintf(l, sizeof(l), "VOL %u", volume);
  sendLine(l);
  uiDirty = true;
}

// ============================================================================
//  Buttons
// ============================================================================
Button readButton() {
  int v = analogRead(BTN_PAD);
  if (v <= BTN_LEFT_MAX)                              return BTN_PREV;
  if (v >= BTN_MID_MIN   && v <= BTN_MID_MAX)         return BTN_SELECT;
  if (v >= BTN_RIGHT_MIN && v <= BTN_RIGHT_MAX)       return BTN_NEXT;
  return BTN_NONE;                                    // idle, or mid-transition
}

// What a press means depends on which screen is showing, so navigation lives
// here. The S3 is told the resulting intent, never the raw button.
//
//   HOME   MIDDLE = open the library
//   LIST   LEFT/RIGHT = move the selection, MIDDLE = play, hold = home
//   PLAY   LEFT/RIGHT = previous/next track, MIDDLE = pause, hold = stop
//   any    hold LEFT/RIGHT = volume down/up
void pollButtons() {
  Button now = readButton();

  if (now == btnCandidate) {
    if (btnStableCount < BTN_STABLE) btnStableCount++;
  } else {
    btnCandidate = now;
    btnStableCount = 1;
  }
  if (btnStableCount < BTN_STABLE) return;

  if (btnCandidate != BTN_NONE && btnHeld == BTN_NONE) {
    if (millis() - lastBtnEvent < BTN_GAP_MS) return;   // contact bounce
    btnHeld = btnCandidate;
    btnHeldSince = millis();
    btnRepeatAt = millis();
    btnLongFired = false;
    return;
  }

  if (btnCandidate == btnHeld && btnHeld != BTN_NONE) {
    unsigned long held = millis() - btnHeldSince;
    if (btnHeld == BTN_SELECT) {
      if (!btnLongFired && held >= BTN_LONG_MS) {
        btnLongFired = true;
        lastBtnEvent = millis();
        if (linkMode == LINK_PCM) {
          sendLine("BTN STOP");
          streamAbort = true;
          streamPaused = false;
        } else if (screen == SCREEN_REC) {
          sendLine("BTN RECSTOP");
        } else if (screen == SCREEN_LIST) {
          screen = SCREEN_HOME;                 // back out of the library
          uiDirty = true;
        } else if (screen == SCREEN_SETTINGS) {
          screen = SCREEN_HOME;
          uiDirty = true;
        }
      }
    } else if (held >= BTN_LONG_MS && millis() - btnRepeatAt >= BTN_REPEAT_MS) {
      btnRepeatAt = millis();
      btnLongFired = true;                      // suppress the tap on release
      lastBtnEvent = millis();
      adjustVolume(btnHeld == BTN_NEXT ? +1 : -1);
    }
    return;
  }

  if (btnCandidate == BTN_NONE && btnHeld != BTN_NONE) {
    Button released = btnHeld;
    btnHeld = BTN_NONE;
    lastBtnEvent = millis();
    if (btnLongFired) return;

    if (linkMode == LINK_PCM) {                 // now playing
      switch (released) {
        case BTN_PREV: sendLine("BTN PREV"); streamAbort = true; streamPaused = false; break;
        case BTN_NEXT: sendLine("BTN NEXT"); streamAbort = true; streamPaused = false; break;
        case BTN_SELECT:
          // Pause is local: the S3 keeps its cursor and simply stops being
          // asked for data. Reported anyway, because a silent pause is
          // indistinguishable from a hang in the log.
          streamPaused = !streamPaused;
          copyField(uiState, sizeof(uiState), streamPaused ? "PAUSED" : "PLAYING");
          sendLine(streamPaused ? "BTN PAUSE" : "BTN RESUME");
          uiDirty = true;
          break;
        default: break;
      }
      return;
    }

    if (screen == SCREEN_REC) {
      // Only one thing to do while recording, and every button does it: a
      // recording you cannot stop is worse than one stopped by accident.
      sendLine("BTN RECSTOP");
      return;
    }

    if (screen == SCREEN_SETTINGS) {
      switch (released) {
        case BTN_PREV:
          settingsSel = (settingsSel + 5 - 1) % 5;
          uiDirty = true;
          break;
        case BTN_NEXT:
          settingsSel = (settingsSel + 1) % 5;
          uiDirty = true;
          break;
        case BTN_SELECT:
          if (settingsSel == 0) {
            sendLine(settingsBle ? "BTN BLE_OFF" : "BTN BLE_ON");
          } else if (settingsSel == 1) {
            sendLine(settingsWifi ? "BTN WIFI_OFF" : "BTN WIFI_ON");
          } else if (settingsSel == 2) {
            // Stays on this screen, like BLE and Wi-Fi: the S3 answers with a
            // fresh SETTINGS line and the ON/OFF label is the confirmation.
            // Jumping home here hid the only feedback the toggle has.
            sendLine(settingsAudio ? "BTN AUDIO_OFF" : "BTN AUDIO_ON");
          } else if (settingsSel == 3) {
            // Vision and voice are mutually exclusive on the S3; it decides
            // which one wins and reports back, so nothing is assumed here.
            sendLine(settingsVision ? "BTN VISION_OFF" : "BTN VISION_ON");
          } else if (settingsSel == 4) {
            screen = SCREEN_HOME;
          }
          uiDirty = true;
          break;
        default:
          break;
      }
      return;
    }

    if (screen == SCREEN_LIST) {
      switch (released) {
        case BTN_PREV: sendLine("BTN UP");   break;
        case BTN_NEXT: sendLine("BTN DOWN"); break;
        case BTN_SELECT: sendLine("BTN PLAY"); break;
        default: break;
      }
      return;
    }

    // Home menu.
    switch (released) {
      case BTN_PREV:
        homeSel = (homeSel + HOME_ITEMS - 1) % HOME_ITEMS;
        uiDirty = true;
        break;
      case BTN_NEXT:
        homeSel = (homeSel + 1) % HOME_ITEMS;
        uiDirty = true;
        break;
      case BTN_SELECT:
        if (homeSel == HOME_RECORD) {
          sendLine("BTN REC");                  // the S3 answers with REC ...
        } else if (homeSel == HOME_SETTINGS) {
          screen = SCREEN_SETTINGS;
          settingsSel = 0;
          sendLine("BTN SETTINGS_REQ");
          uiDirty = true;
        } else {
          sendLine(homeSel == HOME_MUSIC ? "BTN SRC 0" : "BTN SRC 1");
          screen = SCREEN_LIST;
          uiDirty = true;
        }
        break;
      default:
        break;
    }
  }
}

// ============================================================================
//  Battery
// ============================================================================
uint8_t batteryPercent() {
#if BATT_SENSE_ENABLED
  // Two readings, averaged; the ADC is noisy and shares the RTC analog block
  // with the DAC.
  int raw = (analogRead(BATT_PIN) + analogRead(BATT_PIN)) / 2;
  int mv = static_cast<int>(raw * (3300.0f / 4095.0f) * BATT_DIVIDER);
  if (mv <= BATT_EMPTY_MV) return 0;
  if (mv >= BATT_FULL_MV) return 100;
  return static_cast<uint8_t>((100L * (mv - BATT_EMPTY_MV)) /
                              (BATT_FULL_MV - BATT_EMPTY_MV));
#else
  return 255;                                    // unknown
#endif
}

// ============================================================================
//  User interface
// ============================================================================
void setMessage(const char* text) {
  copyField(uiMessage, sizeof(uiMessage), text);
  uiMessageUntil = millis() + 2500;
  uiDirty = true;
}

static void drawBattery(int x, int y) {
  display.drawRect(x, y, 18, 9, SSD1306_WHITE);
  display.fillRect(x + 18, y + 3, 2, 3, SSD1306_WHITE);
  uint8_t pct = batteryPercent();
  if (pct == 255) {
    display.setCursor(x + 6, y + 1);
    display.print('-');                          // no sense divider fitted
  } else {
    int w = (14 * pct) / 100;
    if (w > 0) display.fillRect(x + 2, y + 2, w, 5, SSD1306_WHITE);
  }
}

// Wraps the track name over up to two 21-character rows.
static void drawTrackName(int y) {
  const int perRow = 21;
  int len = strlen(uiTrack);
  char row[perRow + 1];

  int split = len > perRow ? perRow : len;
  if (len > perRow) {
    for (int i = perRow; i > perRow - 8 && i > 0; i--) {
      if (uiTrack[i] == ' ') { split = i; break; }
    }
  }
  strncpy(row, uiTrack, split);
  row[split] = '\0';
  display.setCursor(0, y);
  display.print(row);

  if (len > split) {
    int start = (uiTrack[split] == ' ') ? split + 1 : split;
    strncpy(row, uiTrack + start, perRow);
    row[perRow] = '\0';
    display.setCursor(0, y + 10);
    display.print(row);
  }
}

static void drawHeader(const char* title) {
  drawBattery(0, 0);
  display.setCursor(24, 1);
  display.print(title);
  display.setCursor(96, 1);
  display.print('v');
  display.print(volume);
  display.drawFastHLine(0, 12, 128, SSD1306_WHITE);
}

// Fast overlay while the S3 is doing something the user should see happening.
// The bar is driven by elapsed time rather than real progress: reading a card
// is quick, and the point is to make it feel acknowledged rather than to
// measure it.
static void drawBusy() {
  unsigned long elapsed = millis() - busyStart;
  int w = static_cast<int>((elapsed * 96) / BUSY_MS);
  if (w > 96) w = 96;

  display.fillRect(8, 18, 112, 30, SSD1306_BLACK);
  display.drawRect(8, 18, 112, 30, SSD1306_WHITE);
  display.setCursor(14, 24);
  display.print(busyText);
  display.drawRect(14, 36, 100, 7, SSD1306_WHITE);
  if (w > 0) display.fillRect(16, 38, w, 3, SSD1306_WHITE);
}

// Home is both a menu and a status page. The menu is what the buttons act on;
// the panel underneath answers "what is on this thing and how is it doing"
// without needing a separate screen, and its top line follows the selection so
// it is always about whatever is highlighted.
static void drawHome() {
  drawHeader("ePod");

  const char* label[HOME_ITEMS] = {"Music", "Recordings", "Record new", "Settings"};
  for (int i = 0; i < HOME_ITEMS; i++) {
    int y = 14 + i * 8;
    bool sel = (i == homeSel);
    if (sel) {
      display.fillRect(0, y - 1, 128, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(4, y);
    display.print(sel ? '>' : ' ');
    display.print(' ');
    display.print(label[i]);
    if (i == HOME_MUSIC || i == HOME_RECORDINGS) {
      int n = (i == HOME_MUSIC) ? libMusic : libRecordings;
      display.setCursor(n >= 10 ? 108 : 114, y);
      display.print(n);
    }
    if (sel) display.setTextColor(SSD1306_WHITE);
  }

  display.drawFastHLine(0, 47, 128, SSD1306_WHITE);

  // Contextual line: what the highlighted item actually contains.
  display.setCursor(0, 49);
  if (homeSel == HOME_RECORD) {
    display.print(AUDIO_RATE / 1000);
    display.print("kHz 8-bit mono mic");
  } else {
    int n = (homeSel == HOME_MUSIC) ? libMusic : libRecordings;
    if (n == 0) {
      display.print(libCardMB > 0 ? "empty folder" : "no card");
    } else {
      display.print(n);
      display.print(n == 1 ? " file, " : " files, ");
      display.print(libSeconds / 60);
      display.print(" min");
    }
  }

  // Fixed line: the device itself.
  display.setCursor(0, 57);
  if (libCardMB > 0) {
    if (libCardMB >= 1024) {
      display.print(libCardMB / 1024);
      display.print("GB");
    } else {
      display.print(libCardMB);
      display.print("MB");
    }
  } else {
    display.print("no SD");
  }
  uint32_t up = millis() / 1000;
  display.setCursor(48, 57);
  display.print("up ");
  display.print(up / 3600);
  display.print('h');
  if ((up / 60) % 60 < 10) display.print('0');
  display.print((up / 60) % 60);
  display.setCursor(104, 57);
  display.print(batteryPercent() == 255 ? "--" : "ok");
}

static void drawRec() {
  drawHeader("Recording");

  // A filled circle is the universal record indicator and costs four pixels.
  display.fillCircle(10, 24, 5, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(28, 17);
  char t[12];
  snprintf(t, sizeof(t), "%lu:%02lu", (unsigned long)(recSeconds / 60),
           (unsigned long)(recSeconds % 60));
  display.print(t);
  display.setTextSize(1);

  // Peak level over the last quarter second.
  display.setCursor(0, 38);
  display.print("level");
  display.drawRect(34, 37, 94, 9, SSD1306_WHITE);
  int w = (recLevel * 90) / 100;
  if (w > 90) w = 90;
  if (w > 0) display.fillRect(36, 39, w, 5, SSD1306_WHITE);

  display.drawFastHLine(0, 50, 128, SSD1306_WHITE);
  display.setCursor(0, 54);
  display.print("any key = stop & save");
}

static void drawList() {
  drawHeader(libSource == 0 ? "Music" : "Recordings");

  for (int i = 0; i < LIST_ROWS; i++) {
    int y = 16 + i * 11;
    bool sel = (i == rowSelSlot);
    if (sel) {
      display.fillRect(0, y - 2, 128, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(2, y);
    if (rowName[i][0]) {
      display.print(rowFirst + i + 1);
      display.print('.');
      display.print(rowName[i]);
    }
    if (sel) display.setTextColor(SSD1306_WHITE);
  }

  display.setCursor(0, 56);
  display.print("MID=play  hold=home");
}

static void drawPlay() {
  drawHeader("Now playing");

  display.setCursor(0, 16);
  display.print(uiIndex);
  display.print('/');
  display.print(uiTotal);

  drawTrackName(26);

  int filled = 0;
  if (streamTotal > 0) {
    uint32_t scaled = static_cast<uint32_t>(
        (static_cast<uint64_t>(streamPlayed) * 124) / streamTotal);
    filled = scaled > 124 ? 124 : static_cast<int>(scaled);
  }
  display.drawRect(0, 42, 128, 7, SSD1306_WHITE);
  if (filled > 0) display.fillRect(2, 44, filled, 3, SSD1306_WHITE);

  display.setCursor(0, 54);
  display.print(streamPaused ? "||" : ">");

  if (streamTotal > 0) {
    uint32_t at = streamPlayed / AUDIO_RATE;
    uint32_t len = streamTotal / AUDIO_RATE;
    char t[20];
    snprintf(t, sizeof(t), "%lu:%02lu/%lu:%02lu",
             static_cast<unsigned long>(at / 60), static_cast<unsigned long>(at % 60),
             static_cast<unsigned long>(len / 60), static_cast<unsigned long>(len % 60));
    display.setCursor(128 - static_cast<int>(strlen(t)) * 6, 54);
    display.print(t);
  }
}

void drawScreen() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (screen == SCREEN_SPLASH) {
    display.setTextSize(2);
    display.setCursor(28, 18);
    display.print("ePod");
    display.setTextSize(1);
    display.setCursor(16, 44);
    display.print("zero waste audio");
    display.display();
    return;
  }

  switch (screen) {
    case SCREEN_PLAY: drawPlay(); break;
    case SCREEN_LIST: drawList(); break;
    case SCREEN_REC:  drawRec();  break;
    case SCREEN_SETTINGS: drawSettings(); break;
    default:          drawHome(); break;
  }

  // A transient notice sits over the body, then the screen returns.
  if (uiMessage[0] && millis() < uiMessageUntil) {
    display.fillRect(0, 26, 128, 14, SSD1306_BLACK);
    display.drawFastHLine(0, 26, 128, SSD1306_WHITE);
    display.drawFastHLine(0, 39, 128, SSD1306_WHITE);
    display.setCursor(2, 29);
    display.print(uiMessage);
  }

  if (progressPercent >= -1) drawProgress();
  else if (busyActive) drawBusy();

  display.display();
}

void serviceUi() {
  unsigned long now = millis();

  if (screen == SCREEN_SPLASH && now >= splashUntil) {
    screen = SCREEN_HOME;
    uiDirty = true;
  }
  if (uiMessage[0] && now >= uiMessageUntil) {
    uiMessage[0] = '\0';
    uiDirty = true;
  }
  if (busyActive && now - busyStart >= BUSY_MS + 250) {
    busyActive = false;
    uiDirty = true;
  }

  if (now - lastBtnPoll >= BTN_POLL_MS) {
    lastBtnPoll = now;
    pollButtons();
  }

  if (btnLog && now - lastBtnLog >= BTN_LOG_MS) {
    lastBtnLog = now;
    int v = analogRead(BTN_PAD);
    Button b = readButton();
    const char* name = b == BTN_NEXT ? "NEXT" :
                       b == BTN_PREV ? "PREV" :
                       b == BTN_SELECT ? "SELECT" : "none";
    char line[96];
    snprintf(line, sizeof(line),
             "ADC %4d  bands: PREV<=%d  SEL %d-%d  NEXT %d-%d  -> %s",
             v, BTN_LEFT_MAX, BTN_MID_MIN, BTN_MID_MAX,
             BTN_RIGHT_MIN, BTN_RIGHT_MAX, name);
    Serial2.println(line);
  }

  if (linkMode == LINK_PCM && now - lastBufReport >= BUF_REPORT_MS) {
    lastBufReport = now;
    // The sender steers its rate from this, so it goes out every pass it is
    // due, whatever the DAC is doing.
    char line[48];
    snprintf(line, sizeof(line), "BUF %lu %lu %lu",
             static_cast<unsigned long>(ringLevel + Serial2.available()),
             static_cast<unsigned long>(streamDry),
             static_cast<unsigned long>(streamGap));
    Serial2.println(line);
  }

  // Redraw on change, on a timer while playing so the bar moves, and fast
  // while the busy overlay is animating.
  unsigned long refresh = busyActive ? 60 : OLED_REFRESH_MS;
  if (screen == SCREEN_HOME && now - lastOledDraw >= 5000) uiDirty = true;
  if (uiDirty ||
      ((screen == SCREEN_PLAY || screen == SCREEN_REC || busyActive) &&
       now - lastOledDraw >= refresh)) {
    uiDirty = false;
    lastOledDraw = now;
    drawScreen();
  }
}

// ============================================================================
//  Diagnostics
// ============================================================================
// Waits without ever starving the DAC. Every diagnostic uses this instead of
// delay(), so the descriptor chain keeps turning even during a long test.
bool idleWait(uint32_t ms) {
  unsigned long until = millis() + ms;
  while (static_cast<int32_t>(millis() - until) < 0) {
    serviceAudio();
    serviceUi();
    if (linkMode == LINK_TEXT) {
      while (Serial2.available()) {
        char c = static_cast<char>(Serial2.read());
        if (c == '\r' || c == '\n') {
          if (cmdLen > 0) {
            cmdLine[cmdLen] = '\0';
            cmdLen = 0;
            if (strcasecmp(cmdLine, "STOP") == 0) {
              stopRequested = true;
              return true;
            }
          }
        } else if (c >= 32 && c <= 126 && cmdLen < sizeof(cmdLine) - 1) {
          cmdLine[cmdLen++] = c;
        }
      }
    }
  }
  return false;
}

bool parseWaveform(const char* name, Waveform& out) {
  if (!strcmp(name, "SINE") || !strcmp(name, "SIN"))    { out = WAVE_SINE;     return true; }
  if (!strcmp(name, "SQUARE") || !strcmp(name, "SQ"))   { out = WAVE_SQUARE;   return true; }
  if (!strcmp(name, "TRI") || !strcmp(name, "TRIANGLE")){ out = WAVE_TRIANGLE; return true; }
  if (!strcmp(name, "SAW") || !strcmp(name, "RAMP"))    { out = WAVE_SAW;      return true; }
  if (!strcmp(name, "NOISE") || !strcmp(name, "RAND"))  { out = WAVE_NOISE;    return true; }
  return false;
}

const char* waveformName(Waveform wave) {
  switch (wave) {
    case WAVE_SINE:     return "SINE";
    case WAVE_SQUARE:   return "SQUARE";
    case WAVE_TRIANGLE: return "TRIANGLE";
    case WAVE_SAW:      return "SAW";
    case WAVE_NOISE:    return "NOISE";
  }
  return "?";
}

uint8_t waveSample(Waveform wave, uint32_t phase) {
  uint16_t p = static_cast<uint16_t>(phase);
  switch (wave) {
    case WAVE_SINE:     return static_cast<uint8_t>(127.5f + 127.0f * sinf(p * (2.0f * PI / 65536.0f)));
    case WAVE_SQUARE:   return p < 32768 ? 255 : 0;
    case WAVE_TRIANGLE: return p < 32768 ? static_cast<uint8_t>(p >> 7)
                                         : static_cast<uint8_t>(255 - ((p - 32768) >> 7));
    case WAVE_SAW:      return static_cast<uint8_t>(p >> 8);
    case WAVE_NOISE:    return static_cast<uint8_t>(esp_random() & 0xFF);
  }
  return AUDIO_SILENCE;
}

void playTone(Waveform wave, uint32_t freq, uint32_t durationMs) {
  if (freq == 0) freq = 440;
  if (durationMs == 0) durationMs = 1500;
  if (freq < 20) freq = 20;
  if (freq > AUDIO_RATE / 2) freq = AUDIO_RATE / 2;
  if (durationMs > 10000) durationMs = 10000;

  const uint32_t phaseStep = (freq * 65536UL) / AUDIO_RATE;
  uint32_t remaining = (AUDIO_RATE / 1000UL) * durationMs;
  uint32_t phase = 0;

  while (remaining >= DAC_ALIGN && !stopRequested) {
    size_t n = remaining < DAC_PUSH_CHUNK ? remaining : DAC_PUSH_CHUNK;
    n &= ~static_cast<size_t>(DAC_ALIGN - 1);    // frame aligned, as always
    for (size_t i = 0; i < n; i++) {
      stageBuf[i] = volTable[waveSample(wave, phase)];
      phase = (phase + phaseStep) & 0xFFFF;
    }
    size_t loaded = 0;
    dacCyclic = false;
    dac_continuous_write(dacHandle, stageBuf, n, &loaded, 200);
    remaining -= loaded ? loaded : n;
    serviceUi();
  }
  lastWave = wave;
}

void testLED() {
  stopRequested = false;
  sendLine("LED test: Red -> Green -> Blue. Send STOP to end.");
  do {
    digitalWrite(LED_R, HIGH); if (idleWait(500)) break; digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, HIGH); if (idleWait(500)) break; digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, HIGH); if (idleWait(500)) break; digitalWrite(LED_B, LOW);
  } while (!stopRequested);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
  stopRequested = false;
  sendLine("LED test done.");
}

void testAudio(const char* args) {
  stopRequested = false;
  char buf[48];
  copyField(buf, sizeof(buf), args);

  char* tok[3] = {NULL, NULL, NULL};
  int count = 0;
  for (char* p = strtok(buf, " "); p && count < 3; p = strtok(NULL, " ")) tok[count++] = p;

  if (count == 0) {
    playTone(lastWave, 440, 1500);
  } else if (!strcmp(tok[0], "ALL")) {
    Waveform every[] = {WAVE_SINE, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE};
    for (uint8_t i = 0; i < 5 && !stopRequested; i++) {
      char l[32];
      snprintf(l, sizeof(l), "Tone: %s", waveformName(every[i]));
      sendLine(l);
      playTone(every[i], 440, 700);
      idleWait(150);
    }
  } else if (!strcmp(tok[0], "SWEEP")) {
    for (uint32_t f = 200; f <= 4000 && !stopRequested; f += 200) playTone(lastWave, f, 80);
  } else {
    Waveform wave;
    if (!parseWaveform(tok[0], wave)) {
      sendLine("Use SINE, SQUARE, TRI, SAW, NOISE, SWEEP or ALL.");
      return;
    }
    playTone(wave, count > 1 ? atoi(tok[1]) : 440, count > 2 ? atoi(tok[2]) : 1500);
  }
  stopRequested = false;
  sendLine("Audio test done.");
}

void testOLED() {
  sendLine("OLED test.");
  if (oledReady) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 25);
    display.print("UART OK!");
    display.display();
  }
  idleWait(2000);
  uiDirty = true;
  sendLine("OLED test done.");
}

void testButton() {
  stopRequested = false;
  sendLine("Button test. Press buttons; send STOP to end.");
  while (!stopRequested) {
    int v = analogRead(BTN_PAD);
    Button b = readButton();                     // exactly what the UI sees
    const char* name = b == BTN_NEXT ? "NEXT" :
                       b == BTN_PREV ? "PREV" :
                       b == BTN_SELECT ? "SELECT" : "none";
    char l[80];
    snprintf(l, sizeof(l), "ADC %d  -> %s", v, name);
    sendLine(l);
    if (idleWait(300)) break;
  }
  stopRequested = false;
  sendLine("Button test done.");
}

// Walks the two idle-noise suspects on and off so you can hear which one owns
// the hiss. The DAC keeps running throughout except in the step that disables
// it on purpose.
void testQuiet() {
  stopRequested = false;
  sendLine("Idle noise hunt. Listen for which steps go quiet.");

  sendLine("1/4 baseline: DAC on, OLED on");
  if (idleWait(5000)) return;

  sendLine("2/4 DAC disabled");
  dacReady = false;
  dac_continuous_disable(dacHandle);
  bool aborted = idleWait(5000);
  dac_continuous_enable(dacHandle);
  dacReady = true;
  dacCyclic = false;
  dacIdleSilence();
  if (aborted) return;

  sendLine("3/4 OLED off, DAC on");
  if (oledReady) display.ssd1306_command(SSD1306_DISPLAYOFF);
  aborted = idleWait(5000);
  if (oledReady) display.ssd1306_command(SSD1306_DISPLAYON);
  if (aborted) { uiDirty = true; return; }

  sendLine("4/4 button ADC polled hard");
  unsigned long until = millis() + 5000;
  while (static_cast<int32_t>(millis() - until) < 0) {
    analogRead(BTN_PAD);
    serviceAudio();
  }

  uiDirty = true;
  sendLine("Done.");
  sendLine("Quiet at 2 -> DAC analog block / 3V3 rail.");
  sendLine("Quiet at 3 -> OLED charge pump.");
  sendLine("Worse at 4 -> ADC coupling into the DAC.");
  sendLine("Never quiet -> supply or ground wiring.");
}

void reportResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  const char* why;
  switch (reason) {
    case ESP_RST_POWERON:   why = "power-on"; break;
    case ESP_RST_EXT:       why = "external reset pin"; break;
    case ESP_RST_SW:        why = "software restart"; break;
    case ESP_RST_PANIC:     why = "PANIC / exception"; break;
    case ESP_RST_INT_WDT:   why = "INTERRUPT WATCHDOG"; break;
    case ESP_RST_TASK_WDT:  why = "TASK WATCHDOG"; break;
    case ESP_RST_BROWNOUT:  why = "BROWNOUT - the 3V3 rail dipped"; break;
    default:                why = "unknown"; break;
  }
  Serial.print("WROVER last reset: ");
  Serial.println(why);

  if (reason == ESP_RST_POWERON) {
    crumbMagic = 0;                              // cold boot: no history
  } else if (crumbMagic == CRUMB_MAGIC) {
    Serial.print("WROVER was in: ");
    Serial.println(crumb);
  }
}

// ============================================================================
//  setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  reportResetReason();

  Serial2.setRxBufferSize(UART_RX_BUFFER);
  Serial2.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  pinMode(BTN_PAD, INPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
#if BATT_SENSE_ENABLED
  pinMode(BATT_PIN, INPUT);
#endif

  memset(silenceBuf, AUDIO_SILENCE, sizeof(silenceBuf));
  rebuildVolTable();

  dacReady = dacBegin();
  if (dacReady) dacIdleSilence();
  if (!dacReady) {
    Serial.println("DAC init FAILED");
    // Say so over the link too: the WROVER's own console is rarely watched,
    // and silent audio with a full buffer looks exactly like a hang.
    sendLine("MSG DAC INIT FAILED");
  }

  Wire.begin(OLED_SDA, OLED_SCL);
  // 100 kHz is the default and a full frame is 1 KB: that is ~90 ms of blocked
  // loop on every redraw. At 400 kHz it is ~23 ms.
  Wire.setClock(400000);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledReady) Serial.println("OLED allocation failed");

  splashUntil = millis() + 1800;
  screen = SCREEN_SPLASH;
  uiDirty = true;

  Serial.println("WROVER ready.");
}

void loop() {
  crumbSet("audio");
  serviceAudio();      // first, always: the DMA chain must never run dry
  crumbSet("streamEnd");
  checkStreamEnd();
  crumbSet("link");
  serviceLink();
  crumbSet("ui");
  serviceUi();
  crumbSet("idle");
}


// ============================================================================
//  On-Demand settings & progress implementations
// ============================================================================
static void drawProgress() {
  display.fillRect(8, 18, 112, 30, SSD1306_BLACK);
  display.drawRect(8, 18, 112, 30, SSD1306_WHITE);
  display.setCursor(14, 22);
  display.print(progressText[0] ? progressText : "Receiving...");
  
  if (progressPercent >= 0) {
    display.drawRect(14, 36, 100, 7, SSD1306_WHITE);
    int w = progressPercent;
    if (w > 100) w = 100;
    if (w < 0) w = 0;
    if (w > 0) display.fillRect(16, 38, w, 3, SSD1306_WHITE);
  } else {
    display.setCursor(14, 36);
    display.print("Receiving data...");
  }
}

static void drawSettings() {
  drawHeader("AI + Radio Settings");

  // Five rows at 9 px instead of four at 10: the Vision entry had to come from
  // somewhere, and a 64 px panel has no spare lines. The detail footer shrank
  // to a single line to pay for it.
  const char* label[] = {"BLE Bluetooth", "Wi-Fi AP", "Voice AI", "Vision AI", "Back"};
  for (int i = 0; i < 5; i++) {
    int y = 12 + i * 9;
    bool sel = (i == settingsSel);
    if (sel) {
      display.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(2, y);
    display.print(sel ? '>' : ' ');
    display.print(' ');
    display.print(label[i]);

    const char* val = NULL;
    if      (i == 0) val = settingsBle    ? "ON" : "OFF";
    else if (i == 1) val = settingsWifi   ? "ON" : "OFF";
    else if (i == 2) val = settingsAudio  ? "ON" : "OFF";
    else if (i == 3) val = settingsVision ? "ON" : "OFF";
    if (val) { display.setCursor(104, y); display.print(val); }

    if (sel) display.setTextColor(SSD1306_WHITE);
  }

  // One line, and it says whichever thing is most worth knowing right now.
  display.setCursor(0, 57);
  if (settingsWifi) {
    display.print("P:");
    display.print(settingsPass);
  } else if (settingsVision) {
    display.print("Watching for guests");
  } else if (settingsAudio) {
    display.print("Say next / back");
  } else if (settingsBle) {
    display.print("BLE ePod: pair me");
  } else {
    display.print("All radios off");
  }
}
