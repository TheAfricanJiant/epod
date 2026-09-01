// ============================================================================
//  Smart ePod - Brain 2: XIAO ESP32-S3 Sense
//  Music library, playlist and file streaming.
//
//  ---------------------------------------------------------------------------
//  PLAYER STATE MACHINE
//
//      STOPPED  --play-->  PLAYING  --end of file / skip / stop-->  CLOSING
//         ^                                                            |
//         +------------------ END received (or timeout) ---------------+
//
//  CLOSING exists because a stream cannot be abandoned mid-flight. The end
//  marker has to be sent and the WROVER has to confirm it is back in text mode
//  before any command can be sent, or the next command is decoded as audio.
//  Every transition - a button, the end of a track, a console command - goes
//  through the same three functions, so there is one path in and one path out.
//  ---------------------------------------------------------------------------
//
//  Audio on the card is raw 8-bit unsigned mono PCM at STREAM_RATE. All the
//  expensive DSP is done once on a PC by tools/convert.ps1, so this firmware
//  never decodes or resamples. See docs/MUSIC_FORMAT.md
//
//  Two rules keep this loop alive rather than tripping the watchdog:
//    - bytes go out in ~2 ms batches with one Serial1.write(), never per byte
//    - loop() always ends in delay(1), so the idle task gets its turn
// ============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <driver/i2s_pdm.h>
#include "wireless.h"
// Edge Impulse continuous audio keyword inferencing
#include <epod_inferencing.h>

// A crash leaves no output at all - the USB CDC drops with the reset. RTC
// memory survives, so the last place the firmware was is still there on the
// way back up.
RTC_NOINIT_ATTR static char crumb[24];
RTC_NOINIT_ATTR static uint32_t crumbMagic;
#define CRUMB_MAGIC 0x43524D42
static inline void crumbSet(const char* where) {
  strncpy(crumb, where, sizeof(crumb) - 1);
  crumb[sizeof(crumb) - 1] = '\0';
  crumbMagic = CRUMB_MAGIC;
}

// ----------------------------------------------------------------- link ----
#define TX1_PIN    43   // to WROVER RX2 (GPIO 27)
#define RX1_PIN    44   // from WROVER TX2 (GPIO 32)
#define UART_BAUD  460800

// ---------------------------------------------------------------- audio ----
#define STREAM_RATE      22050        // must match AUDIO_RATE on the WROVER
#define STREAM_END_RUN   32
#define PUMP_INTERVAL_US 2000
#define PUMP_MAX_BURST   1024

// ------------------------------------------------------------------- SD ----
// The chip select on the Sense expansion board is GPIO 21, which is also the
// user LED - so it blinks on card access. Other boards differ, so a short list
// is probed and the winner reported.
#define SD_SCK   7
#define SD_MISO  8
#define SD_MOSI  9
#define SD_CLOCK_HZ 10000000
static const int SD_CS_CANDIDATES[] = {21, 3, 2, 4};
// The card is a user-facing consumable: it gets pulled and pushed back in
// whenever somebody wants different music. Both directions are polled, once a
// second, and only while stopped - poking the SPI bus mid-stream would stall
// the audio.
#define SD_POLL_MS 1000

// The WROVER shows four rows at a time and owns no playlist, so only the
// visible window is ever sent.
#define LIST_ROWS     4

// ------------------------------------------------------------ microphone ---
// The XIAO ESP32-S3 Sense carries a PDM microphone on these two pins. The S3
// converts PDM to PCM in hardware, so what comes out of i2s_channel_read() is
// ordinary signed 16-bit mono - no software filter needed.
//
// Recording runs at STREAM_RATE, which means a recording is byte-for-byte the
// same format as a converted music file and plays back through exactly the
// same path with no conversion anywhere.
#define MIC_CLK_PIN   42
#define MIC_DIN_PIN   41
#define MIC_AMPLIFY   8               // hardware PDM2PCM gain, 1-15
#define MIC_READ_BYTES 1024           // 512 samples = ~23 ms per read
#define MIC_READ_TIMEOUT_MS 40        // comfortably longer than that
#define REC_MAX_SECONDS (15 * 60)     // stop before a card fills by accident

// ---------------------------------------------------------------- folders ---
#define MUSIC_DIR "/music"
#define REC_DIR   "/recordings"

#define MAX_TRACKS    64
#define MAX_PATH      64
#define MAX_NAME      40
#define FILE_BUF_SIZE 4096

// ------------------------------------------------------- flow control -----
// The WROVER reports its total backlog (jitter ring plus UART buffer, ~24 KB
// of capacity) every 50 ms. The target is deep on purpose - about 280 ms.
// Reading the next block off the card stalls this loop for tens of
// milliseconds and a shallow buffer turns every one into an audible dropout.
#define BUF_TARGET_LOW  6144
#define BUF_TARGET_HIGH 12288
#define BUF_STALL       18432
#define BUF_STALE_MS    500

#define CLOSING_TIMEOUT_MS 4000

// ============================================================================
//  State
// ============================================================================
struct Track {
  char path[MAX_PATH];
  char name[MAX_NAME];
  uint32_t dataBytes;
  uint32_t dataStart;
};

Track tracks[MAX_TRACKS];
int trackCount = 0;
int selected = 0;

enum PlayerState { PS_STOPPED, PS_PLAYING, PS_CLOSING, PS_RECORDING };
PlayerState state = PS_STOPPED;

// Which folder the library is showing. Recordings and music are the same
// format and play through the same code; only the directory differs.
enum Source { SRC_MUSIC, SRC_REC };
Source source = SRC_MUSIC;
int musicCount = 0;
int recCount = 0;

i2s_chan_handle_t micHandle = NULL;
bool micReady = false;
bool voiceMode = false;           // Edge Impulse voice keyword control enabled
bool visionMode = false;          // Edge Impulse person detection enabled

// ============================================================================
//  Signage state - what the menu web app reads
//
//  The web app polls /api/state four times a second. It cannot see events, only
//  state, so every event is published as a (seq, event) pair: seq increments on
//  each new event and the app acts whenever seq changes. An event is therefore
//  never missed between two polls, and never replayed after a page reload.
// ============================================================================
volatile uint32_t webSeq = 0;
char webEvent[12] = "none";
bool guestPresent = false;

void webEmit(const char* ev) {
  strncpy(webEvent, ev, sizeof(webEvent) - 1);
  webEvent[sizeof(webEvent) - 1] = 0;
  webSeq++;
  Serial.print("Signage: ");
  Serial.println(ev);
}
bool voiceDebug = true;           // per-inference reporting; "VOICE" toggles
volatile bool voiceDumpOnce = false;  // "VOICEDUMP": run one inference with
                                      // the SDK's own debug output on

// The S3 has exactly one PDM receiver (I2S0; I2S1 has no PDM support at all),
// so recording and keyword spotting cannot both hold the microphone. They also
// want different sample rates. These two flags are the handover: the recorder
// raises voiceSuspend and waits for the voice task to drop voiceCapturing
// before it reconfigures the channel.
volatile bool voiceCapturing = false;   // voice task currently owns the mic
volatile bool voiceSuspend   = false;   // recorder is asking it to let go
File recFile;
char recPath[MAX_PATH];
uint32_t recBytes = 0;
int recPeak = 0;
unsigned long lastRecReport = 0;
unsigned long recStartedAt = 0;
bool recWarned = false;
int micGain = 4;                      // extra software gain on top of MIC_AMPLIFY

// User intent, applied once per loop from one place.
//
// play(), stop() and skip() open files, send commands and flush the UART.
// Calling them from inside a serial read loop means re-entering the player
// while that loop is still iterating over the same port - which is the
// difference between the button path and the console path, and the reason one
// of them fell over. Requests are queued here instead and applied by
// applyRequest() at the top of loop(), so there is exactly one place where the
// player changes state.
enum Request { REQ_NONE, REQ_PLAY, REQ_STOP, REQ_NEXT, REQ_PREV,
               REQ_UP, REQ_DOWN, REQ_REFRESH,
               REQ_SRC_MUSIC, REQ_SRC_REC, REQ_REC_START, REQ_REC_STOP };
Request request = REQ_NONE;
int requestIndex = 0;
bool endReceived = false;

// What to do once the WROVER confirms the stream is closed.
enum NextAction { NEXT_NONE, NEXT_PLAY, NEXT_STOP };
NextAction nextAction = NEXT_NONE;
int nextTrack = 0;
unsigned long closingSince = 0;

File audioFile;
uint8_t fileBuf[FILE_BUF_SIZE];
size_t fileLen = 0;
size_t filePos = 0;
uint32_t sentBytes = 0;

uint64_t creditFx = 0;                // 16.16 bytes owed to the receiver
uint32_t lastPumpUs = 0;
uint32_t wroverLevel = 0;
uint32_t wroverDry = 0;
uint32_t wroverGap = 0;               // silence forced into the music
unsigned long lastBufMs = 0;
unsigned long lastHealthMs = 0;
unsigned long stalledSince = 0;
bool stallWarned = false;

bool sdReady = false;
bool rescanPending = false;
int sdCsPin = -1;
// Read once at mount. Calling SD.cardSize() from an app request would put an
// SPI transaction wherever the phone happened to ask.
uint32_t cardMB = 0;
unsigned long lastSdPoll = 0;

// Fixed buffers, not String: 20 BUF lines a second through String churns the
// heap, and a fragmented heap is a slow-motion crash.
char pcLine[80];
uint8_t pcLen = 0;
char wroverLine[160];
uint8_t wroverLen = 0;

// ----------------------------------------------------------- prototypes ---
bool mountSd();
void voiceStartTask();
void setVoiceMode(bool on);
void setVisionMode(bool on);
void webEmit(const char* ev);
extern TaskHandle_t voiceTaskHandle;
bool sdCardPresent();
void scanTracks();
void serviceSd();
void onCardRemoved();
void sendCommand(const char* command);
void sendTrackInfo();
void sendLibraryInfo();
void sendListWindow();
void showStopped();
void sendSettingsToWrover();
void play(int index);
void stop();
void skip(int delta);
void beginTrack(int index);
void closeStream(NextAction action, int index);
void runNextAction();
void pumpAudio();
bool refillFileBuffer();
uint32_t currentRate();
void handlePcInput();
void handleCommandLine(char* line, bool fromApp);
void doRescan();
void handleWroverInput();
void handleWroverLine(const char* line);
void onButton(const char* button);
void applyRequest();
void requestPlay(int index);
void printHelp();
void listTracks();
void reportResetReason();
int wrapIndex(int i);
bool micBegin();
void startRecording();
void serviceRecording();
void stopRecording(bool keep);
void setSource(Source next);
const char* sourceDir();
void sendSourceInfo();

// ============================================================================
//  Boot diagnostics
// ============================================================================
// A crash leaves no serial output at all - the USB CDC drops with the reset.
// The only way to see what happened is to ask on the way back up.
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
  Serial.print("Last reset: ");
  Serial.println(why);
  if (reason == ESP_RST_POWERON) {
    crumbMagic = 0;                              // cold boot: no history
  } else if (crumbMagic == CRUMB_MAGIC) {
    Serial.print("Died in: ");
    Serial.println(crumb);
  }
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
}

// ============================================================================
//  Microphone
// ============================================================================
bool micBegin() {
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chanCfg, NULL, &micHandle) != ESP_OK) return false;

  i2s_pdm_rx_config_t cfg = {
    .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(STREAM_RATE),
    .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                               I2S_SLOT_MODE_MONO),
    .gpio_cfg = {},
  };
  cfg.gpio_cfg.clk = static_cast<gpio_num_t>(MIC_CLK_PIN);
  cfg.gpio_cfg.din = static_cast<gpio_num_t>(MIC_DIN_PIN);
  cfg.gpio_cfg.invert_flags.clk_inv = 0;
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
  // A PDM mic sits on a large DC offset and is quiet; the hardware filter and
  // amplifier deal with both before the samples ever reach us.
  cfg.slot_cfg.hp_en = true;
  cfg.slot_cfg.hp_cut_off_freq_hz = 35.5f;
  cfg.slot_cfg.amplify_num = MIC_AMPLIFY;
#endif

  if (i2s_channel_init_pdm_rx_mode(micHandle, &cfg) != ESP_OK) {
    i2s_del_channel(micHandle);
    micHandle = NULL;
    return false;
  }
  return true;
}

// Recording and keyword spotting need different sample rates AND different
// front-end gain, so the channel is re-clocked and re-slotted rather than torn
// down and rebuilt. The channel must be disabled when this is called.
//
// The gain matters more than it looks. The training data for the impulse was
// captured with the stock Arduino I2S library - hardware amplify_num at its
// default of 1 - and then multiplied by 4 in software (VOLUME_GAIN 2, a left
// shift of 2). Feeding the model MIC_AMPLIFY 8 instead is both a different
// level and a different distortion, because amplify_num saturates inside the
// PDM-to-PCM filter. The voice path reproduces the capture conditions; the
// recorder keeps the gain the recordings were tuned for.
static bool micConfigure(uint32_t rate, uint32_t amplify) {
  if (!micHandle) return false;
  i2s_pdm_rx_clk_config_t clk = I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate);
  if (i2s_channel_reconfig_pdm_rx_clock(micHandle, &clk) != ESP_OK) return false;
  i2s_pdm_rx_slot_config_t slot =
      I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
  slot.hp_en = true;
  slot.hp_cut_off_freq_hz = 35.5f;
  slot.amplify_num = amplify;
#endif
  return i2s_channel_reconfig_pdm_rx_slot(micHandle, &slot) == ESP_OK;
}

// Recordings are named REC001, REC002... The next free number is found by
// reading the directory once rather than probing hundreds of filenames.
static void nextRecordingPath(char* out, size_t outSize) {
  int highest = 0;
  File dir = SD.open(REC_DIR);
  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      const char* name = strrchr(entry.name(), '/');
      name = name ? name + 1 : entry.name();
      if (strncasecmp(name, "REC", 3) == 0) {
        int idx = atoi(name + 3);
        if (idx > highest) highest = idx;
      }
      entry.close();
    }
    dir.close();
  }
  snprintf(out, outSize, "%s/REC%03d.raw", REC_DIR, highest + 1);
}

void startRecording() {
  if (state != PS_STOPPED) return;
  if (!sdReady) {
    sendCommand("MSG no card to record to");
    return;
  }
  if (!micReady) {
    sendCommand("MSG microphone failed");
    return;
  }
  if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);

  nextRecordingPath(recPath, sizeof(recPath));
  recFile = SD.open(recPath, FILE_WRITE);
  if (!recFile) {
    Serial.print("Could not create ");
    Serial.println(recPath);
    sendCommand("MSG cannot write to card");
    return;
  }

  // Ask the keyword task to release the microphone and wait for it. Half a
  // second is far longer than one classifier slice; if it has not let go by
  // then something is wedged and the take is abandoned rather than recorded
  // at the wrong sample rate.
  voiceSuspend = true;
  for (int i = 0; i < 50 && voiceCapturing; i++) delay(10);
  if (voiceCapturing || !micConfigure(STREAM_RATE, MIC_AMPLIFY) ||
      i2s_channel_enable(micHandle) != ESP_OK) {
    voiceSuspend = false;
    recFile.close();
    SD.remove(recPath);
    sendCommand("MSG microphone busy");
    return;
  }

  recBytes = 0;
  recPeak = 0;
  lastRecReport = 0;
  recStartedAt = millis();
  recWarned = false;
  state = PS_RECORDING;
  Serial.print("Recording to ");
  Serial.println(recPath);
  sendCommand("REC 0 0");
}

// Reads a block, converts to the player's own format and appends it. Runs once
// per loop pass while recording.
void serviceRecording() {
  crumbSet("record");
  static int16_t pcm[MIC_READ_BYTES / 2];
  static uint8_t out[MIC_READ_BYTES / 2];

  size_t got = 0;
  // A partial read is data, not a failure. i2s_channel_read() returns
  // ESP_ERR_TIMEOUT whenever it cannot fill the whole buffer inside the
  // timeout, and it still hands back everything it did get. Treating that
  // return as an error threw away every block and produced a recording of
  // exactly nothing, which then got discarded as "too short".
  i2s_channel_read(micHandle, pcm, sizeof(pcm), &got, MIC_READ_TIMEOUT_MS);

  if (got >= sizeof(int16_t)) {
    size_t samples = got / sizeof(int16_t);
    for (size_t i = 0; i < samples; i++) {
      int32_t v = static_cast<int32_t>(pcm[i]) * micGain;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      int32_t mag = v < 0 ? -v : v;
      if (mag > recPeak) recPeak = mag;
      uint8_t b = static_cast<uint8_t>((v + 32768) >> 8);
      if (b == 0x00) b = 0x01;                   // 0x00 is the stream marker
      out[i] = b;
    }

    if (recFile.write(out, samples) != samples) {
      Serial.println("Write failed - card full or removed.");
      stopRecording(true);
      return;
    }
    recBytes += samples;

    // Commit to the card every couple of seconds. If power is lost mid-take,
    // everything up to the last flush survives instead of the whole file.
    if (recBytes % (STREAM_RATE * 2) < samples) recFile.flush();
  }

  // Report on a timer whether or not data arrived. A clock that runs while the
  // level bar stays flat is exactly the symptom that says the microphone is
  // not producing.
  if (millis() - lastRecReport >= 250) {
    lastRecReport = millis();
    char line[48];
    snprintf(line, sizeof(line), "REC %lu %d",
             (unsigned long)(recBytes / STREAM_RATE), (recPeak * 100) / 32768);
    sendCommand(line);
    recPeak = 0;                                 // peak per reporting window
  }

  if (!recWarned && recBytes == 0 && millis() - recStartedAt > 2000) {
    recWarned = true;
    Serial.println("WARNING: no audio from the microphone after 2 s.");
    Serial.println("  Check MIC_CLK_PIN / MIC_DIN_PIN against the board.");
  }

  if (recBytes / STREAM_RATE >= REC_MAX_SECONDS) {
    Serial.println("Recording length limit reached.");
    stopRecording(true);
  }
}

void stopRecording(bool keep) {
  if (state != PS_RECORDING) return;
  i2s_channel_disable(micHandle);
  voiceSuspend = false;                          // keyword spotting may resume

  uint32_t seconds = recBytes / STREAM_RATE;
  const char* name = strrchr(recPath, '/');
  name = name ? name + 1 : recPath;

  char line[MAX_PATH + 24];
  if (recFile) recFile.close();

  // A recording under a second is a mis-tap, not a take.
  if (!keep || recBytes < STREAM_RATE) {
    SD.remove(recPath);
    Serial.print("Recording discarded - captured only ");
    Serial.print(recBytes);
    Serial.println(" bytes.");
    if (recBytes == 0) {
      Serial.println("  The microphone produced nothing at all.");
      snprintf(line, sizeof(line), "RECEND fail no mic input");
    } else {
      snprintf(line, sizeof(line), "RECEND fail too short");
    }
  } else {
    Serial.print("Saved ");
    Serial.print(recPath);
    Serial.print("  (");
    Serial.print(seconds);
    Serial.println("s)");
    snprintf(line, sizeof(line), "RECEND ok %s", name);
  }
  state = PS_STOPPED;
  sendCommand(line);

  // Show the result immediately: switch to the recordings folder so the new
  // file is right there.
  setSource(SRC_REC);
}

// ============================================================================
//  SD card
// ============================================================================
// Called once a second while no card is mounted, so it has to be cheap and
// quiet. Once a working chip select has been found it is the only one retried:
// probing four pins every second is slow and fills the log with driver errors.
bool mountSd() {
  if (sdCsPin >= 0) {
    if (SD.begin(sdCsPin, SPI, SD_CLOCK_HZ)) {
      cardMB = static_cast<uint32_t>(SD.cardSize() / (1024 * 1024));
      return true;
    }
    SD.end();
    return false;
  }
  for (size_t i = 0; i < sizeof(SD_CS_CANDIDATES) / sizeof(SD_CS_CANDIDATES[0]); i++) {
    int cs = SD_CS_CANDIDATES[i];
    if (SD.begin(cs, SPI, SD_CLOCK_HZ)) {
      sdCsPin = cs;
      cardMB = static_cast<uint32_t>(SD.cardSize() / (1024 * 1024));
      return true;
    }
    SD.end();
  }
  return false;
}

static bool isAudioName(const char* name) {
  size_t len = strlen(name);
  if (len < 5) return false;
  const char* ext = name + len - 4;
  return strcasecmp(ext, ".raw") == 0 || strcasecmp(ext, ".wav") == 0;
}

// Strips the directory and extension so the OLED shows a clean title.
static void makeDisplayName(const char* path, char* out, size_t outSize) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  size_t len = strlen(base);
  if (len > 4) len -= 4;
  if (len > outSize - 1) len = outSize - 1;
  memcpy(out, base, len);
  out[len] = '\0';
}

const char* sourceDir() {
  return source == SRC_MUSIC ? MUSIC_DIR : REC_DIR;
}

// Counts playable files in a folder without disturbing the current library.
static int countIn(const char* dirName) {
  File dir = SD.open(dirName);
  if (!dir) return 0;
  int n = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && isAudioName(entry.name())) n++;
    entry.close();
  }
  dir.close();
  return n;
}

// Both folder totals, so the home menu can show them without switching.
void sendSourceInfo() {
  char line[48];
  snprintf(line, sizeof(line), "SRC %d %d %d", source == SRC_MUSIC ? 0 : 1,
           musicCount, recCount);
  sendCommand(line);
}

void setSource(Source next) {
  source = next;
  scanTracks();
  selected = 0;
  sendSourceInfo();
  showStopped();
}

void scanTracks() {
  crumbSet("scanTracks");
  trackCount = 0;

  // /music is the documented home for music, but a card with files loose in
  // the root still works.
  const char* dirName = sourceDir();
  if (source == SRC_MUSIC && !SD.exists(MUSIC_DIR)) dirName = "/";
  File dir = SD.open(dirName);
  if (!dir) {
    Serial.println("Could not open the music directory.");
    return;
  }

  while (trackCount < MAX_TRACKS) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && isAudioName(entry.name())) {
      Track& t = tracks[trackCount];
      // entry.name() is bare on some core versions, a full path on others.
      if (entry.name()[0] == '/') {
        strncpy(t.path, entry.name(), MAX_PATH - 1);
      } else {
        snprintf(t.path, MAX_PATH, "%s%s%s", dirName,
                 strcmp(dirName, "/") == 0 ? "" : "/", entry.name());
      }
      t.path[MAX_PATH - 1] = '\0';
      makeDisplayName(t.path, t.name, MAX_NAME);
      t.dataBytes = entry.size();
      t.dataStart = 0;
      trackCount++;
    }
    entry.close();
  }
  dir.close();

  // Alphabetical, so the running order matches what you see on the PC.
  for (int i = 1; i < trackCount; i++) {
    Track key = tracks[i];
    int j = i - 1;
    while (j >= 0 && strcasecmp(tracks[j].name, key.name) > 0) {
      tracks[j + 1] = tracks[j];
      j--;
    }
    tracks[j + 1] = key;
  }

  if (selected >= trackCount) selected = 0;

  // Keep both folder totals current for the home menu.
  musicCount = SD.exists(MUSIC_DIR) ? countIn(MUSIC_DIR) : countIn("/");
  recCount = SD.exists(REC_DIR) ? countIn(REC_DIR) : 0;

  sendLibraryInfo();
  sendSourceInfo();
  // Deliberately silent: this runs on a one-second poll, and anything printed
  // here scrolls the console forever.
}

// Presence probe. Reading a directory entry forces a real transaction: the FAT
// layer will happily hand back handles it already holds in memory after the
// card has physically gone, so opening alone proves nothing.
bool sdCardPresent() {
  File probe = SD.open(SD.exists("/music") ? "/music" : "/");
  if (!probe) return false;
  File first = probe.openNextFile();
  // Failing to read an entry only means "gone" if we know there was one.
  bool present = static_cast<bool>(first) || trackCount == 0;
  if (first) first.close();
  probe.close();
  return present;
}

void onCardRemoved() {
  Serial.println("SD card removed.");
  SD.end();
  sdReady = false;
  cardMB = 0;
  trackCount = 0;
  selected = 0;
  sendLibraryInfo();
  showStopped();
  sendCommand("MSG card removed");
}

// Polls both directions so a card can be swapped as often as the user likes,
// with no reboot and no menu. Everything here is quiet unless something
// actually changed - it runs once a second forever.
void serviceSd() {
  if (state != PS_STOPPED) return;               // never touch SPI mid-stream
  if (wirelessUploadBusy()) return;              // nor while a file is landing
  if (millis() - lastSdPoll < SD_POLL_MS) return;
  lastSdPoll = millis();

  if (sdReady) {
    if (!sdCardPresent()) {
      onCardRemoved();
      return;
    }
    // Card is in but had nothing on it last time. Look again, and only speak
    // up if something appeared - a card can be written to while it sits in the
    // slot.
    if (trackCount == 0) {
      scanTracks();
      if (trackCount > 0) {
        Serial.print("Found ");
        Serial.print(trackCount);
        Serial.println(" track(s).");
        listTracks();
        showStopped();
      }
    }
    return;
  }

  // No card mounted. A failed attempt is the normal state with an empty slot,
  // so it says nothing at all.
  if (!mountSd()) return;

  sdReady = true;                                // <- without this the mount is
                                                 // retried and re-listed every
                                                 // single second, forever
  Serial.print("SD mounted on CS GPIO ");
  Serial.println(sdCsPin);

  // Overlay first, so it is on screen while the read happens rather than after.
  sendCommand("BUSY Reading SD card");
  scanTracks();

  Serial.print("Found ");
  Serial.print(trackCount);
  Serial.println(" track(s).");
  listTracks();
  showStopped();
  if (trackCount == 0) sendCommand("MSG no music on card");
}

// ============================================================================
//  Display helpers
// ============================================================================
// True while the S3->WROVER direction is carrying raw audio rather than text.
static bool linkCarriesPcm() {
  return state == PS_PLAYING || state == PS_CLOSING;
}

// Everything the player says goes to the WROVER, and the wireless layer picks
// out the lines the app understands.
//
// The guard matters now that commands can arrive from a phone at any moment:
// during playback the link is a raw byte pipe, and a stray line of text would
// be played as noise and then have the audio parsed back as commands. The app
// still gets its telemetry either way - only the wire to the WROVER goes
// quiet.
void sendCommand(const char* command) {
  if (!linkCarriesPcm()) Serial1.println(command);
  wirelessNotify(command);
}

void sendTrackInfo() {
  char line[MAX_NAME + 32];
  if (trackCount == 0) {
    sendCommand("TRACK 0 0 no music on card");
    return;
  }
  snprintf(line, sizeof(line), "TRACK %d %d %s", selected + 1, trackCount,
           tracks[selected].name);
  sendCommand(line);
}

// Everything the home screen shows. Cheap to recompute, sent whenever the
// library changes.
void sendLibraryInfo() {
  uint32_t seconds = 0;
  for (int i = 0; i < trackCount; i++) seconds += tracks[i].dataBytes / STREAM_RATE;
  char line[64];
  snprintf(line, sizeof(line), "INFO %d %lu %lu", trackCount,
           (unsigned long)seconds, (unsigned long)(sdReady ? cardMB : 0));
  sendCommand(line);
}

// Sends the four rows around the selection, scrolled so the selection is
// always on screen. The WROVER draws whatever it is given and keeps no
// playlist of its own.
void sendListWindow() {
  int first = 0;
  if (trackCount > LIST_ROWS) {
    first = selected - LIST_ROWS / 2;
    if (first < 0) first = 0;
    if (first > trackCount - LIST_ROWS) first = trackCount - LIST_ROWS;
  }

  char line[MAX_NAME + 24];
  snprintf(line, sizeof(line), "WIN %d %d", first, selected - first);
  sendCommand(line);

  for (int slot = 0; slot < LIST_ROWS; slot++) {
    int idx = first + slot;
    if (idx < trackCount) {
      snprintf(line, sizeof(line), "ROW %d %s", slot, tracks[idx].name);
    } else {
      snprintf(line, sizeof(line), "ROW %d", slot);   // blank the row
    }
    sendCommand(line);
  }
}

void showStopped() {
  sendTrackInfo();
  sendListWindow();
  sendCommand("STATE STOPPED");
}

int wrapIndex(int i) {
  if (trackCount == 0) return 0;
  while (i < 0) i += trackCount;
  return i % trackCount;
}

// ============================================================================
//  Player - every transition goes through play(), stop() or skip()
// ============================================================================

// Walks a RIFF/WAVE header to the data chunk, so a .wav that is already 8-bit
// mono at STREAM_RATE can be dropped straight onto the card. Nothing here
// resamples.
static bool locateWavData(File& f, uint32_t& dataStart, uint32_t& dataLen) {
  uint8_t hdr[12];
  f.seek(0);
  if (f.read(hdr, 12) != 12) return false;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

  uint32_t pos = 12;
  uint8_t chunk[8];
  while (pos + 8 <= f.size()) {
    f.seek(pos);
    if (f.read(chunk, 8) != 8) return false;
    uint32_t size = static_cast<uint32_t>(chunk[4]) | (static_cast<uint32_t>(chunk[5]) << 8) |
                    (static_cast<uint32_t>(chunk[6]) << 16) | (static_cast<uint32_t>(chunk[7]) << 24);
    if (memcmp(chunk, "data", 4) == 0) {
      dataStart = pos + 8;
      dataLen = size;
      return true;
    }
    pos += 8 + size + (size & 1);
  }
  return false;
}

// Opens a track and starts streaming. Only ever called from a stopped state -
// play() and runNextAction() guarantee that.
void beginTrack(int index) {
  crumbSet("beginTrack");
  if (trackCount == 0) {
    sendCommand("MSG no music on card");
    state = PS_STOPPED;
    return;
  }
  selected = wrapIndex(index);

  if (audioFile) audioFile.close();
  crumbSet("SD.open");
  audioFile = SD.open(tracks[selected].path, FILE_READ);
  if (!audioFile) {
    // A transient SPI hiccup should not end playback. Re-read the card once.
    Serial.print("Open failed, rescanning: ");
    Serial.println(tracks[selected].path);
    scanTracks();
    if (trackCount == 0) {
      sdReady = false;
      sendCommand("MSG card unreadable");
      state = PS_STOPPED;
      showStopped();
      return;
    }
    selected = wrapIndex(selected);
    audioFile = SD.open(tracks[selected].path, FILE_READ);
  }
  if (!audioFile) {
    Serial.print("Could not open ");
    Serial.println(tracks[selected].path);
    sendCommand("MSG open failed");
    state = PS_STOPPED;
    showStopped();
    return;
  }

  uint32_t dataStart = 0;
  uint32_t dataLen = audioFile.size();
  if (locateWavData(audioFile, dataStart, dataLen)) {
    if (dataLen > audioFile.size() - dataStart) dataLen = audioFile.size() - dataStart;
  } else {
    dataStart = 0;
    dataLen = audioFile.size();
  }
  if (dataLen == 0) {
    Serial.println("Empty track, skipping.");
    sendCommand("MSG empty file");
    state = PS_STOPPED;
    showStopped();
    return;
  }
  tracks[selected].dataStart = dataStart;
  tracks[selected].dataBytes = dataLen;
  audioFile.seek(dataStart);

  fileLen = 0;
  filePos = 0;
  sentBytes = 0;
  creditFx = 0;
  // Assume the receiver is already at target, not empty. Starting at 0 puts
  // the rate controller in its "speed up" band before any report arrives.
  wroverLevel = BUF_TARGET_LOW;
  wroverDry = 0;
  wroverGap = 0;
  lastBufMs = millis();
  lastPumpUs = micros();
  stalledSince = 0;
  stallWarned = false;

  // Radios off before a single audio byte moves. Both share the 3.3 V rail
  // with the WROVER, and a sagging rail is how this project learned about
  // brownouts. Wi-Fi is the expensive one; BLE stays up for control.
  if (wirelessWifiActive()) {
    Serial.println("Stopping Wi-Fi before playback.");
    wirelessWifiStop();
  }

  // Display first: once AUDIO_BEGIN goes out the WROVER stops reading text.
  char line[48];
  sendTrackInfo();
  sendCommand("STATE PLAYING");
  snprintf(line, sizeof(line), "AUDIO_BEGIN %d %lu", STREAM_RATE, (unsigned long)dataLen);
  sendCommand(line);
  Serial1.flush();

  state = PS_PLAYING;
  Serial.print("Playing ");
  Serial.print(tracks[selected].name);
  Serial.print("  (");
  Serial.print(dataLen / STREAM_RATE);
  Serial.println("s)");
}

// Sends the end marker and parks what happens next until the WROVER confirms.
void closeStream(NextAction action, int index) {
  crumbSet("closeStream");
  uint8_t marker[STREAM_END_RUN];
  memset(marker, 0x00, sizeof(marker));
  Serial1.write(marker, sizeof(marker));
  Serial1.flush();

  if (audioFile) audioFile.close();
  nextAction = action;
  nextTrack = index;
  closingSince = millis();
  state = PS_CLOSING;
}

void runNextAction() {
  crumbSet("runNextAction");
  NextAction action = nextAction;
  nextAction = NEXT_NONE;
  state = PS_STOPPED;

  if (action == NEXT_PLAY) {
    beginTrack(nextTrack);
  } else {
    showStopped();
  }
}

// --- request queue ----------------------------------------------------------
void requestPlay(int index) {
  request = REQ_PLAY;
  requestIndex = index;
}

// The single point at which user intent becomes a state change.
void applyRequest() {
  Request r = request;
  request = REQ_NONE;
  switch (r) {
    case REQ_PLAY: crumbSet("req:play"); play(requestIndex); break;
    case REQ_STOP: crumbSet("req:stop"); stop();             break;
    case REQ_NEXT: crumbSet("req:next"); skip(+1);           break;
    case REQ_PREV: crumbSet("req:prev"); skip(-1);           break;
    case REQ_UP:
    case REQ_DOWN:
      // Browsing the library. Never changes what is playing.
      crumbSet("req:browse");
      if (trackCount > 0) {
        selected = wrapIndex(selected + (r == REQ_DOWN ? 1 : -1));
        sendTrackInfo();
        sendListWindow();
      }
      break;
    case REQ_REFRESH:
      crumbSet("req:refresh");
      sendLibraryInfo();
      sendSourceInfo();
      sendTrackInfo();
      sendListWindow();
      break;
    case REQ_SRC_MUSIC: crumbSet("req:src");  setSource(SRC_MUSIC); break;
    case REQ_SRC_REC:   crumbSet("req:src");  setSource(SRC_REC);   break;
    case REQ_REC_START: crumbSet("req:rec");  startRecording();     break;
    case REQ_REC_STOP:  crumbSet("req:recx"); stopRecording(true);  break;
    default: break;
  }
}

// --- the three public transitions -------------------------------------------
void play(int index) {
  if (state == PS_RECORDING) stopRecording(true);
  if (trackCount == 0) {
    sendCommand("MSG no music on card");
    return;
  }
  if (state == PS_PLAYING)      closeStream(NEXT_PLAY, index);
  else if (state == PS_CLOSING) { nextAction = NEXT_PLAY; nextTrack = index; }
  else                          beginTrack(index);
}

void stop() {
  if (state == PS_PLAYING)      closeStream(NEXT_STOP, 0);
  else if (state == PS_CLOSING) nextAction = NEXT_STOP;
  else                          showStopped();
}

// While playing, skipping changes track. While stopped, it browses.
void skip(int delta) {
  if (trackCount == 0) return;
  if (state == PS_PLAYING || state == PS_CLOSING) {
    play(wrapIndex(selected + delta));
  } else {
    selected = wrapIndex(selected + delta);
    showStopped();
  }
}

// ============================================================================
//  Streaming
// ============================================================================
uint32_t currentRate() {
  if (millis() - lastBufMs >= BUF_STALE_MS) {
    // No feedback: send at exactly the DAC rate and nothing else. Assuming an
    // empty buffer here is what overflowed the receiver during bring-up.
    stalledSince = 0;
    return STREAM_RATE;
  }
  if (wroverLevel > BUF_STALL) {
    // Normal for a moment, and normal for as long as playback is paused.
    // Persisting without a pause means the receiver has stopped draining.
    if (stalledSince == 0) stalledSince = millis();
    return 0;
  }
  stalledSince = 0;
  stallWarned = false;
  if (wroverLevel < BUF_TARGET_LOW)  return STREAM_RATE + STREAM_RATE / 8;
  if (wroverLevel > BUF_TARGET_HIGH) return STREAM_RATE - STREAM_RATE / 5;
  return STREAM_RATE;
}

bool refillFileBuffer() {
  crumbSet("refill");
  if (!audioFile) return false;                  // never read a closed handle
  uint32_t remaining = tracks[selected].dataBytes > sentBytes
                           ? tracks[selected].dataBytes - sentBytes : 0;
  if (remaining == 0) return false;              // end of track

  size_t want = remaining < FILE_BUF_SIZE ? remaining : FILE_BUF_SIZE;
  int got = audioFile.read(fileBuf, want);
  if (got <= 0) {
    // Either the track ended early or the card was pulled mid-song. Marking it
    // unmounted lets serviceSd() notice and recover rather than retrying a
    // handle that will never read again.
    if (remaining > FILE_BUF_SIZE) {
      Serial.println("Read failed mid-track.");
      sdReady = false;
    }
    return false;
  }

  // 0x00 is reserved for the end-of-stream marker, so it must never appear in
  // audio. Once per block is far cheaper than testing every byte on the way
  // out.
  for (int i = 0; i < got; i++) {
    if (fileBuf[i] == 0x00) fileBuf[i] = 0x01;
  }
  fileLen = static_cast<size_t>(got);
  filePos = 0;
  return true;
}

void pumpAudio() {
  crumbSet("pump");
  if (!audioFile) {                              // defensive: nothing to send
    closeStream(NEXT_STOP, 0);
    return;
  }
  uint32_t now = micros();
  uint32_t elapsed = now - lastPumpUs;
  if (elapsed < PUMP_INTERVAL_US) return;
  lastPumpUs = now;
  if (elapsed > 200000) elapsed = 200000;        // never burst after a stall

  uint32_t rate = currentRate();
  if (rate == 0) {
    creditFx = 0;                                // receiver full; bank nothing
    return;
  }

  creditFx += ((static_cast<uint64_t>(elapsed) * rate) << 16) / 1000000ULL;
  uint32_t due = static_cast<uint32_t>(creditFx >> 16);
  if (due == 0) return;
  if (due > PUMP_MAX_BURST) due = PUMP_MAX_BURST;
  creditFx -= static_cast<uint64_t>(due) << 16;

  while (due > 0) {
    if (filePos >= fileLen && !refillFileBuffer()) {
      // End of track. Advance, or stop at the end of the list rather than
      // looping the library forever.
      int next = selected + 1;
      if (next >= trackCount) {
        Serial.println("End of list.");
        closeStream(NEXT_STOP, 0);
      } else {
        closeStream(NEXT_PLAY, next);
      }
      return;
    }
    size_t chunk = fileLen - filePos;
    if (chunk > due) chunk = due;
    // One call for the whole run. Per-byte writes were most of a CPU core and
    // are what starved the watchdog.
    Serial1.write(&fileBuf[filePos], chunk);
    filePos += chunk;
    sentBytes += chunk;
    due -= static_cast<uint32_t>(chunk);
  }
}

// ============================================================================
//  Protocol from the WROVER
// ============================================================================
void onButton(const char* button) {
  // NEXT / PREV change track and only arrive while something is playing.
  // UP / DOWN move the selection in the library list.
  if (strcmp(button, "NEXT") == 0)        request = REQ_NEXT;
  else if (strcmp(button, "PREV") == 0)   request = REQ_PREV;
  else if (strcmp(button, "DOWN") == 0)   request = REQ_DOWN;
  else if (strcmp(button, "UP") == 0)     request = REQ_UP;
  else if (strcmp(button, "PLAY") == 0)   requestPlay(selected);
  else if (strcmp(button, "STOP") == 0)   request = REQ_STOP;
  else if (strcmp(button, "REFRESH") == 0) request = REQ_REFRESH;
  else if (strcmp(button, "SRC 0") == 0)  request = REQ_SRC_MUSIC;
  else if (strcmp(button, "SRC 1") == 0)  request = REQ_SRC_REC;
  else if (strcmp(button, "REC") == 0)    request = REQ_REC_START;
  else if (strcmp(button, "RECSTOP") == 0) request = REQ_REC_STOP;
  else if (strcmp(button, "BLE_ON") == 0) {
    wirelessBleStart();
    sendSettingsToWrover();
  }
  else if (strcmp(button, "BLE_OFF") == 0) {
    wirelessBleStop();
    sendSettingsToWrover();
  }
  else if (strcmp(button, "WIFI_ON") == 0) {
    if (wirelessWifiStart()) {
      sendSettingsToWrover();
    } else {
      sendCommand("MSG Wi-Fi AP stopped: playing");
    }
  }
  else if (strcmp(button, "WIFI_OFF") == 0) {
    wirelessWifiStop();
    sendSettingsToWrover();
  }
  else if (strcmp(button, "SETTINGS_REQ") == 0) {
    sendSettingsToWrover();
  }
  else if (strcmp(button, "AUDIO_ON") == 0)   setVoiceMode(true);
  else if (strcmp(button, "AUDIO_OFF") == 0)  setVoiceMode(false);
  else if (strcmp(button, "VISION_ON") == 0)  setVisionMode(true);
  else if (strcmp(button, "VISION_OFF") == 0) setVisionMode(false);
  // PAUSE and RESUME are handled entirely on the WROVER; they arrive here for
  // visibility only.
}

void handleWroverLine(const char* line) {
  if (strncmp(line, "BUF ", 4) == 0) {
    // BUF <backlog> <dry> <gap>
    wroverLevel = static_cast<uint32_t>(atoi(line + 4));
    const char* space = strchr(line + 4, ' ');
    if (space) {
      wroverDry = static_cast<uint32_t>(atoi(space + 1));
      const char* space2 = strchr(space + 1, ' ');
      if (space2) wroverGap = static_cast<uint32_t>(atoi(space2 + 1));
    }
    lastBufMs = millis();
    return;                                      // too chatty to echo
  }
  if (strncmp(line, "BTN ", 4) == 0) {
    Serial.print("BUTTON: ");
    Serial.println(line + 4);
    onButton(line + 4);
    return;
  }
  if (strncmp(line, "END", 3) == 0) {
    endReceived = true;                          // acted on in loop()
    return;
  }
  if (strncmp(line, "VOL ", 4) == 0) {
    Serial.print("Volume: ");
    Serial.println(line + 4);
    wirelessNotify(line); // forward to BLE client
    return;
  }
  Serial.print("WROVER says: ");
  Serial.println(line);
}

void handleWroverInput() {
  while (Serial1.available()) {
    char c = static_cast<char>(Serial1.read());
    if (c == '\r' || c == '\n') {
      if (wroverLen > 0) {
        wroverLine[wroverLen] = '\0';
        wroverLen = 0;
        handleWroverLine(wroverLine);
      }
    } else if (c >= 32 && c <= 126 && wroverLen < sizeof(wroverLine) - 1) {
      wroverLine[wroverLen++] = c;
    }
  }
}

// ============================================================================
//  PC console
// ============================================================================
void printHelp() {
  Serial.println("ePod commands:");
  Serial.println("  LIST         - show the tracks on the card");
  Serial.println("  PLAY [n]     - play track n (default: current)");
  Serial.println("  NEXT / PREV  - change track");
  Serial.println("  STOP         - stop playback");
  Serial.println("  RESCAN       - re-read the card");
  Serial.println("  SDINFO       - card status");
  Serial.println("  WHY          - why the board last reset");
  Serial.println("  VOL [0-24]   - volume, 16 = unity");
  Serial.println("  MUSIC / RECORDINGS - choose which folder to browse");
  Serial.println("  RECORD / RECSTOP   - record from the onboard mic");
  Serial.println("  MICGAIN [1-32]     - recording level");
  Serial.println("  WIFI ON / WIFI OFF - Soft-AP upload server (stopped only)");
  Serial.println("The ePodd app speaks the same commands over BLE.");
  Serial.println("Anything else is forwarded to the WROVER (diagnostics).");
  Serial.println("Buttons: LEFT/RIGHT tap = prev/next, hold = volume");
  Serial.println("         MIDDLE tap = play/pause, hold = stop");
}

void listTracks() {
  if (trackCount == 0) {
    Serial.println("No tracks. Put .raw files in /music on the card.");
    return;
  }
  for (int i = 0; i < trackCount; i++) {
    Serial.print(i == selected ? "> " : "  ");
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.print(tracks[i].name);
    Serial.print("  (");
    Serial.print(tracks[i].dataBytes / STREAM_RATE);
    Serial.println("s)");
  }
}

// Re-reads the card and republishes everything that depends on it.
void doRescan() {
  if (!sdReady) {
    sdCsPin = -1;
    sdReady = mountSd();
  }
  if (sdReady) scanTracks();
  listTracks();
  showStopped();
}

// One command parser for every source: the USB console, BLE EP01, anything
// added later. Sources differ only in how the line arrives.
//
// Nothing here does the work directly - commands that change what the player
// is doing go through the request queue, so state still only ever changes at
// one point in loop().
void handleCommandLine(char* line, bool fromApp) {
  for (char* q = line; *q; q++) *q = toupper(*q);

  if (!strcmp(line, "HELP")) {
    printHelp();
  } else if (!strcmp(line, "LIST")) {
    listTracks();
    sendListWindow();                            // the app wants it too
  } else if (!strcmp(line, "INFO")) {
    sendLibraryInfo();
    sendTrackInfo();
    sendCommand("VOL"); // query WROVER for current volume to sync with app
  } else if (!strcmp(line, "WHY")) {
    reportResetReason();
  } else if (!strcmp(line, "RESCAN")) {
    // The app sends this after every upload. Re-reading the card means SPI
    // traffic, which cannot happen while a track is being streamed off it -
    // so it is deferred until playback has actually stopped.
    if (linkCarriesPcm()) {
      request = REQ_STOP;
      rescanPending = true;
    } else {
      doRescan();
    }
  } else if (!strcmp(line, "WIFI ON")) {
    if (wirelessWifiStart()) {
      char note[64];
      snprintf(note, sizeof(note), "MSG wifi ready %s", wirelessApAddress());
      sendCommand(note);
    } else {
      sendCommand("MSG wifi needs playback stopped");
    }
  } else if (!strcmp(line, "WIFI OFF")) {
    wirelessWifiStop();
    sendCommand("MSG wifi off");
  } else if (!strcmp(line, "RECORD")) {
    request = REQ_REC_START;
  } else if (!strcmp(line, "RECSTOP")) {
    request = REQ_REC_STOP;
  } else if (!strcmp(line, "MUSIC")) {
    request = REQ_SRC_MUSIC;
  } else if (!strcmp(line, "RECORDINGS")) {
    request = REQ_SRC_REC;
  } else if (!strncmp(line, "MICGAIN ", 8)) {
    int g = atoi(line + 8);
    if (g >= 1 && g <= 32) {
      micGain = g;
      Serial.print("Mic gain: ");
      Serial.println(micGain);
    } else {
      Serial.println("Mic gain must be 1-32.");
    }
  } else if (!strcmp(line, "VOICEDUMP")) {
    voiceDumpOnce = true;
    Serial.println("Voice: dumping the next inference (feature matrix + internals)");
  } else if (!strcmp(line, "VOICE")) {
    voiceDebug = !voiceDebug;
    Serial.print("Voice debug: ");
    Serial.println(voiceDebug ? "ON - printing level and confidences" : "OFF");
    Serial.print("Voice mode is ");
    Serial.print(voiceMode ? "ON" : "OFF");
    Serial.print(", mic ");
    Serial.print(micReady ? "ready" : "FAILED");
    Serial.print(", classes:");
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      Serial.print(" ");
      Serial.print(ei_classifier_inferencing_categories[i]);
    }
    Serial.println();
  } else if (!strcmp(line, "SDINFO")) {
    Serial.print("Mounted: ");
    Serial.println(sdReady ? "yes" : "no");
    // SD.begin() needs a contiguous block, so the largest free block matters
    // more than the total. A mount that fails with plenty of total heap but a
    // small largest block is fragmentation, not exhaustion.
    Serial.print("Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print("   largest block: ");
    Serial.print(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    Serial.print("   voice task: ");
    Serial.println(voiceTaskHandle ? "running" : "not started");
    Serial.print("CS pin: ");
    Serial.println(sdCsPin);
    if (sdReady) {
      Serial.print("Card size: ");
      Serial.print(cardMB);
      Serial.println(" MB");
    }
    Serial.print("Music: ");
    Serial.print(musicCount);
    Serial.print("   Recordings: ");
    Serial.println(recCount);
    Serial.print("Browsing: ");
    Serial.println(sourceDir());
    Serial.print("Microphone: ");
    Serial.println(micReady ? "ready" : "FAILED");
    Serial.print("BLE: ");
    Serial.print(wirelessBleConnected() ? "connected" : "advertising");
    Serial.print("   Wi-Fi: ");
    Serial.println(wirelessWifiActive() ? wirelessApAddress() : "off");
  } else if (!strcmp(line, "NEXT")) {
    request = REQ_NEXT;
  } else if (!strcmp(line, "PREV")) {
    request = REQ_PREV;
  } else if (!strcmp(line, "STOP")) {
    request = REQ_STOP;
  } else if (!strcmp(line, "PAUSE")) {
    // The WROVER owns pause, and it cannot be reached mid-stream over a link
    // that is carrying PCM. Stopping is the honest equivalent.
    request = REQ_STOP;
  } else if (!strcmp(line, "PLAY")) {
    requestPlay(selected);
  } else if (!strncmp(line, "PLAY ", 5)) {
    int n = atoi(line + 5);
    if (n < 1 || n > trackCount) {
      Serial.print("No such track. There are ");
      Serial.println(trackCount);
    } else {
      requestPlay(n - 1);
    }
  } else if (!strncmp(line, "VOL", 3)) {
    // Volume lives on the WROVER and reaching it means sending text, which is
    // impossible mid-stream: the link is carrying PCM.
    if (state == PS_STOPPED) sendCommand(line);
    else Serial.println("Hold LEFT or RIGHT for volume while playing.");
  } else if (state != PS_STOPPED) {
    // Forwarding raw text mid-stream would be decoded as audio.
    if (!fromApp) Serial.println("Busy playing. STOP first.");
  } else {
    sendCommand(line);                           // diagnostics, straight through
  }
}

void handlePcInput() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (pcLen == 0) continue;
      pcLine[pcLen] = '\0';
      pcLen = 0;
      handleCommandLine(pcLine, false);
    } else if (c == '\b' || c == 127) {
      if (pcLen > 0) pcLen--;
    } else if (c >= 32 && c <= 126 && pcLen < sizeof(pcLine) - 1) {
      pcLine[pcLen++] = c;
    }
  }
}

// Drains commands the app queued from a radio callback.
static void handleAppInput() {
  char line[80];
  while (wirelessNextCommand(line, sizeof(line))) {
    Serial.print("APP: ");
    Serial.println(line);
    handleCommandLine(line, true);
  }
}

// ============================================================================
//  Seam for the wireless layer
// ============================================================================
bool epodCanAcceptUpload() {
  return state == PS_STOPPED && sdReady;
}

const char* epodMusicDir() { return MUSIC_DIR; }

void epodStatusLine(char* out, size_t outSize) {
  uint32_t seconds = 0;
  for (int i = 0; i < trackCount; i++) seconds += tracks[i].dataBytes / STREAM_RATE;
  snprintf(out, outSize, "INFO %d %lu %lu", trackCount, (unsigned long)seconds,
           (unsigned long)(sdReady ? cardMB : 0));
}

// ============================================================================
//  Mode arbitration
//
//  Vision and voice are never on together. They share the one PDM/camera-bearing
//  board, they each want a multi-kilobyte inference stack, and the S3 is not
//  going to run two impulses at 4 slices a second without starving the audio
//  stream. So these two setters are the ONLY way either mode is entered, and
//  each one switches the other off first.
//
//  The normal run is a handover, not a choice: vision watches an empty table,
//  a guest arrives, vision stands down and voice takes over for the menu. Both
//  are also exposed in Settings so a misbehaving model can be killed by hand.
// ============================================================================
void setVoiceMode(bool on) {
  if (on) {
    if (!micReady) { sendCommand("MSG microphone not ready"); return; }
    if (visionMode) setVisionMode(false);
    voiceStartTask();
  }
  voiceMode = on;
  Serial.print("Voice keyword mode: ");
  Serial.println(on ? "ON" : "OFF");
  sendSettingsToWrover();
}

void setVisionMode(bool on) {
  if (on) {
    // While the camera watches, the S3 does nothing else. A 320x320 frame plus
    // inference leaves no room to also be streaming audio off the card, and the
    // table is empty anyway - there is nobody to hear it. Music is what greets
    // the guest at the moment of detection, not what plays to an empty chair.
    if (voiceMode) setVoiceMode(false);
    if (state != PS_STOPPED) request = REQ_STOP;
  }
  visionMode = on;
  Serial.print("Person detection: ");
  Serial.println(on ? "ON" : "OFF");
  sendSettingsToWrover();
}

// Called by the vision task when someone sits down at the table. One shot: the
// whole point is that the camera stops once the guest is seen, so the menu runs
// on voice alone.
void onGuestDetected() {
  if (guestPresent) return;
  guestPresent = true;
  setVisionMode(false);                 // stop watching - they are here
  webEmit("person");                    // eyes open on the web app
  if (sdReady && trackCount > 0 && state == PS_STOPPED) {
    requestPlay(selected);              // soft music while they read
  }
  setVoiceMode(true);                   // hand over to the menu keywords
}

// Table cleared: back to sleep, back to watching.
void onGuestGone() {
  if (!guestPresent) return;
  guestPresent = false;
  setVoiceMode(false);
  request = REQ_STOP;
  webEmit("sleep");
  setVisionMode(true);
}

void sendSettingsToWrover() {
  char buf[128];
  // Field 6: voiceMode (1=Voice AI on, 0=off)
  snprintf(buf, sizeof(buf), "SETTINGS %d %d %s %s %s %d %d",
           wirelessBleActive() ? 1 : 0,
           wirelessWifiActive() ? 1 : 0,
           "ePod-Music",
           "epodmusicpass",
           wirelessApAddress(),
           voiceMode ? 1 : 0,
           visionMode ? 1 : 0);
  sendCommand(buf);
}

// ============================================================================
//  Web API - consumed by webapp/ over the Soft-AP
// ============================================================================
void epodApiState(char* out, size_t outSize) {
  snprintf(out, outSize,
           "{\"seq\":%lu,\"event\":\"%s\",\"person\":%d,\"idle\":%d,"
           "\"vision\":%d,\"voice\":%d,\"playing\":%d,\"tracks\":%d}",
           (unsigned long)webSeq, webEvent,
           guestPresent ? 1 : 0, guestPresent ? 0 : 1,
           visionMode ? 1 : 0, voiceMode ? 1 : 0,
           state == PS_PLAYING ? 1 : 0, trackCount);
}

// Body is a bare word, not JSON - the whole vocabulary is six commands and a
// parser for them would be more code than the feature.
void epodApiCommand(const char* body, char* out, size_t outSize) {
  if      (!strncmp(body, "vision_on",  9))  setVisionMode(true);
  else if (!strncmp(body, "vision_off", 10)) setVisionMode(false);
  else if (!strncmp(body, "voice_on",   8))  setVoiceMode(true);
  else if (!strncmp(body, "voice_off",  9))  setVoiceMode(false);
  else if (!strncmp(body, "wake",       4))  onGuestDetected();
  else if (!strncmp(body, "sleep",      5))  onGuestGone();
  epodApiState(out, outSize);
}

void epodUploadStarted(const char* name) {
  char buf[80];
  snprintf(buf, sizeof(buf), "PROGRESS -1 Recv: %s", name);
  sendCommand(buf);
}

void epodUploadProgress(uint32_t bytes, uint32_t total) {
  char buf[80];
  if (total > 0) {
    int pct = (bytes * 100) / total;
    if (pct > 100) pct = 100;
    snprintf(buf, sizeof(buf), "PROGRESS %d WiFi:%d%%", pct, pct);
  } else {
    snprintf(buf, sizeof(buf), "PROGRESS -1 BLE:%d KB", bytes / 1024);
  }
  sendCommand(buf);
}

void epodUploadFailed() {
  sendCommand("PROGRESS -2");
  sendCommand("MSG Upload failed");
}

void epodUploadFinished(const char* name) {
  sendCommand("PROGRESS -2"); // clear progress box
  // An upload only ever lands in the music folder, so show it there.
  source = SRC_MUSIC;
  scanTracks();
  selected = 0;
  sendSourceInfo();
  showStopped();

  char note[64];
  snprintf(note, sizeof(note), "MSG added %s", name);
  sendCommand(note);
  Serial.print("Library now holds ");
  Serial.print(trackCount);
  Serial.println(" track(s).");
}

// ============================================================================
//  setup / loop
// ============================================================================

// ============================================================================
//  Edge Impulse continuous keyword inferencing task
// ============================================================================
// The noise and unknown classes do the rejecting now, and they do it well -
// 97-100% on a quiet room. These gates were sized for the old two-class model
// that could not say "neither", and at those settings they were throwing away
// genuine commands: an 82% "back" lost to the cooldown, a 56% "back" lost to
// the threshold. Loosened to let the model's own judgement through.
#define VOICE_COOLDOWN_MS   1000
#define VOICE_CONF_THRESHOLD 0.60f

// The impulse carries noise and unknown classes, so the model rejects silence
// and non-command speech on its own. What is left here acts only on the
// classifier's output - never on the audio stream itself, which must reach the
// continuous classifier slice by slice and unbroken.
//
//   margin     - the winner must lead the runner-up, not merely clear the bar
//   agreement  - the same word must win consecutive slices to count

// Both taken from the capture sketch the training set was recorded with, so
// the model hears at inference what it was trained on.
#define VOICE_MIC_AMPLIFY   1         // hardware gain: library default
#define VOICE_GAIN_SHIFT    2         // software gain: VOLUME_GAIN, x4
#define VOICE_MARGIN        0.15f     // top-1 must lead top-2 by this much
// 1, not 2. The window slides 250 ms at a time, so one spoken word already
// wins several consecutive inferences on its own; demanding a second agreeing
// slice only added 250 ms of latency and lost short words whose first slice
// was the strongest. Repeat triggers are the cooldown's job, not this one's.
#define VOICE_AGREE_SLICES  1         // consecutive wins needed to fire


static int16_t ei_audio_buf[EI_CLASSIFIER_SLICE_SIZE];

static int ei_microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&ei_audio_buf[offset], out_ptr, length);
  return 0;
}

static unsigned long lastTriggerMs = 0;
static int agreeIdx = -1;             // word currently building a run
static int agreeRun = 0;              // consecutive slices it has won

// Command labels are looked up by name, never by index. Edge Impulse orders
// categories alphabetically, so retraining with a new keyword renumbers every
// class after it - going from {back,next} to {back,helloworld,next,noise,
// unknown} moved "next" from index 1 to index 2. Hardcoded indices survive
// that change by compiling cleanly and doing the wrong thing.
static int idxBack = -1, idxNext = -1, idxHello = -1;

static int voiceLabelIndex(const char* name) {
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (strcmp(ei_classifier_inferencing_categories[i], name) == 0) return i;
  }
  return -1;
}

// Created on first use, not at boot. The task's stack is ~24 kB of internal
// RAM that the DSP and inference stages need; holding it permanently shrinks
// the heap that SD.begin() draws on when a card is inserted later, which is
// the one thing the player must never lose in exchange for a feature the user
// may never switch on.
TaskHandle_t voiceTaskHandle = NULL;

static void voiceTask(void*);

void voiceStartTask() {
  if (voiceTaskHandle) return;
  if (xTaskCreatePinnedToCore(voiceTask, "voiceTask", 24576, NULL, 1,
                              &voiceTaskHandle, 0) != pdPASS) {
    voiceTaskHandle = NULL;
    Serial.println("Voice: not enough memory to start the keyword task");
    sendCommand("MSG voice needs more memory");
  }
}

static void voiceTask(void*) {
  idxBack  = voiceLabelIndex("back");
  idxNext  = voiceLabelIndex("next");
  idxHello = voiceLabelIndex("helloworld");   // optional: places the order
  if (idxBack < 0 || idxNext < 0) {
    Serial.println("Voice: model has no 'back'/'next' class - task not started");
    voiceTaskHandle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // No I2S channel of its own. PDM receive exists only on I2S0 on the
  // ESP32-S3 - i2s_channel_init_pdm_rx_mode() rejects an I2S1 handle
  // outright - and I2S0 is already the recording microphone, so this task
  // borrows micHandle and gives it back whenever the recorder wants it.
  while (true) {
    bool wanted = voiceMode && micReady && !voiceSuspend && state != PS_RECORDING;

    if (!wanted) {
      if (voiceCapturing) {
        i2s_channel_disable(micHandle);
        voiceCapturing = false;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!voiceCapturing) {
      if (!micConfigure(EI_CLASSIFIER_FREQUENCY, VOICE_MIC_AMPLIFY) ||
          i2s_channel_enable(micHandle) != ESP_OK) {
        Serial.println("Voice: cannot take the microphone");
        voiceMode = false;
        sendCommand("MSG microphone busy");
        sendSettingsToWrover();
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      // The continuous classifier keeps a rolling window across slices, so it
      // has to be reset every time the audio stream restarts - otherwise the
      // first inferences run against whatever was in the window before.
      run_classifier_init();
      voiceCapturing = true;
      Serial.print("Voice: listening at ");
      Serial.print(EI_CLASSIFIER_FREQUENCY);
      Serial.print(" Hz, ");
      Serial.print(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
      Serial.print(" slices/window, classes:");
      for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        Serial.print(" ");
        Serial.print(ei_classifier_inferencing_categories[i]);
      }
      Serial.println();
    }

    // run_classifier_continuous() wants a whole slice. i2s_channel_read()
    // returns short whenever the DMA buffers hold less than that, so fill the
    // slice across several reads instead of classifying a buffer that is
    // mostly the previous slice.
    const size_t needed = EI_CLASSIFIER_SLICE_SIZE * sizeof(int16_t);
    size_t filled = 0;
    while (filled < needed) {
      if (!voiceMode || voiceSuspend || state == PS_RECORDING) break;
      size_t got = 0;
      i2s_channel_read(micHandle, reinterpret_cast<uint8_t*>(ei_audio_buf) + filled,
                       needed - filled, &got, 100);
      if (got == 0) break;
      filled += got;
    }
    if (filled < needed) continue;               // aborted or the mic went quiet

    // The capture sketch did this with an unsigned shift, which wraps on loud
    // input; clipping instead, because reproducing a wraparound would feed the
    // model garbage on exactly the loudest words.
    for (size_t i = 0; i < EI_CLASSIFIER_SLICE_SIZE; i++) {
      int32_t v = static_cast<int32_t>(ei_audio_buf[i]) << VOICE_GAIN_SHIFT;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      ei_audio_buf[i] = static_cast<int16_t>(v);
    }

    // Level of the slice, as RMS. This is also the honest way to tell a dead
    // microphone from a confused model: a mic producing nothing sits at ~0
    // while the classifier still reports confident nonsense.
    uint64_t sumSq = 0;
    for (size_t i = 0; i < EI_CLASSIFIER_SLICE_SIZE; i++) {
      int32_t v = ei_audio_buf[i];
      sumSq += static_cast<uint64_t>(v * v);
    }
    uint32_t rms = static_cast<uint32_t>(sqrt(static_cast<double>(sumSq) /
                                              EI_CLASSIFIER_SLICE_SIZE));

    // The RMS is reported, never used to skip a slice.
    // run_classifier_continuous() keeps a sliding window of MFCCs and drops
    // exactly one slice per call, so it must be fed every slice in order. The
    // earlier version skipped quiet slices, which spliced non-adjacent audio
    // into the window and corrupted the very features a spoken word depends
    // on. Rejecting silence is the "noise" class's job now, not ours.

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &ei_microphone_audio_signal_get_data;

    ei_impulse_result_t result;
    // With debug on, the SDK prints the 637-value feature matrix it is about to
    // classify. Sane features with a dead output isolates the fault to the
    // network; garbage features isolates it to the DSP or the audio feeding it.
    bool dumpNow = voiceDumpOnce;
    voiceDumpOnce = false;
    EI_IMPULSE_ERROR err = run_classifier_continuous(&signal, &result, dumpNow);
    if (err != EI_IMPULSE_OK) {
      Serial.print("Voice: classifier error ");
      Serial.println((int)err);
      continue;
    }

    unsigned long now = millis();

    int bestIdx = -1;
    float bestVal = -1.0f, secondVal = -1.0f;
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      float v = result.classification[i].value;
      if (v > bestVal) { secondVal = bestVal; bestVal = v; bestIdx = i; }
      else if (v > secondVal) { secondVal = v; }
    }

    if (voiceDebug) {
      // Built as one string and written once. Two cores print to this UART -
      // the player loop from core 1, this task from core 0 - and a line made
      // of twenty separate Serial.print() calls interleaves with the other
      // core's output mid-word. One write per line is atomic.
      //
      // Scores are printed as whole percents: %f pulls in float formatting
      // that the nano printf on this target does not reliably provide.
      //
      // nn is the giveaway when something is wrong. run_classifier_continuous()
      // only runs the network once its rolling feature window is full, and
      // until then it returns OK with an all-zero result - so nn 0 ms with
      // every class at 0 means the window is not filling, not that the model
      // is unsure.
      char out[256];
      int n = snprintf(out, sizeof(out), "Voice: rms %lu dsp %lums nn %luus pp %luus |",
                       (unsigned long)rms,
                       (unsigned long)(result.timing.dsp_us / 1000),
                       (unsigned long)result.timing.classification_us,
                       (unsigned long)result.timing.postprocessing_us);
      for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (n < 0 || n >= (int)sizeof(out)) break;
        n += snprintf(out + n, sizeof(out) - n, " %s %d",
                      ei_classifier_inferencing_categories[i],
                      (int)(result.classification[i].value * 100.0f + 0.5f));
      }
      const char* verdict;
      if (bestVal < VOICE_CONF_THRESHOLD)              verdict = "below threshold";
      else if ((bestVal - secondVal) < VOICE_MARGIN)   verdict = "too close to call";
      else if (bestIdx != idxBack && bestIdx != idxNext) verdict = "not a command";
      else if (now - lastTriggerMs < VOICE_COOLDOWN_MS) verdict = "in cooldown";
      else                                             verdict = "counting";
      if (n > 0 && n < (int)sizeof(out)) {
        snprintf(out + n, sizeof(out) - n, " | top %s lead %d -> %s (run %d/%d)\n",
                 ei_classifier_inferencing_categories[bestIdx],
                 (int)((bestVal - secondVal) * 100.0f + 0.5f), verdict,
                 (bestIdx == agreeIdx) ? agreeRun + 1 : 1, VOICE_AGREE_SLICES);
      }
      Serial.print(out);
    }

    // Same order as the verbose line above, so the log always names the gate a
    // word actually died at.
    if (bestVal < VOICE_CONF_THRESHOLD ||
        (bestVal - secondVal) < VOICE_MARGIN) {
      agreeIdx = -1;
      agreeRun = 0;
      continue;
    }
    // noise, unknown and helloworld are deliberate rejections by the model.
    // Discarded here rather than after the agreement count, so a run of noise
    // can never sit in the counter and combine with a later command word.
    if (bestIdx != idxBack && bestIdx != idxNext &&
        !(idxHello >= 0 && bestIdx == idxHello)) {
      agreeIdx = -1;
      agreeRun = 0;
      continue;
    }
    if (now - lastTriggerMs < VOICE_COOLDOWN_MS) continue;

    if (bestIdx == agreeIdx) agreeRun++;
    else { agreeIdx = bestIdx; agreeRun = 1; }
    if (agreeRun < VOICE_AGREE_SLICES) continue;
    agreeRun = 0;
    agreeIdx = -1;

    lastTriggerMs = now;
    // The keywords drive the menu on the web app now, not the player. Music is
    // ambient once a guest sits down and is not something they steer by voice.
    if (bestIdx == idxBack)       webEmit("prev");
    else if (bestIdx == idxNext)  webEmit("next");
    else                          webEmit("order");
  }
}

void setup() {
  Serial.begin(115200);

  // setTxBufferSize() is only honoured before begin(); after, the TX path
  // silently falls back to the 128 byte hardware FIFO.
  Serial1.setTxBufferSize(2048);
  Serial1.begin(UART_BAUD, SERIAL_8N1, RX1_PIN, TX1_PIN);

  delay(1500);
  Serial.println();
  Serial.println("XIAO ESP32-S3 - ePod media core");
  reportResetReason();

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  sdReady = mountSd();
  if (sdReady) {
    Serial.print("SD mounted on CS GPIO ");
    Serial.println(sdCsPin);
    scanTracks();
    Serial.print("Found ");
    Serial.print(trackCount);
    Serial.println(" track(s).");
  } else {
    Serial.println("No SD card yet - insert one at any time.");
  }

  wirelessBegin("ePod");

  micReady = micBegin();
  Serial.print("Microphone: ");
  Serial.println(micReady ? "ready" : "FAILED");

  printHelp();
  listTracks();
  sendSourceInfo();
  showStopped();
  sendSettingsToWrover();
}

void loop() {
  crumbSet("pcInput");
  handlePcInput();
  crumbSet("appInput");
  handleAppInput();
  crumbSet("wireless");
  wirelessService();
  crumbSet("wroverInput");
  handleWroverInput();
  // Exactly one place where the player changes state, and it is never inside a
  // serial read loop.
  if (endReceived) {
    endReceived = false;
    crumbSet("endReceived");
    if (state == PS_CLOSING) runNextAction();
  }
  applyRequest();

  crumbSet("state");

  switch (state) {
    case PS_PLAYING:
      pumpAudio();

      if (millis() - lastHealthMs >= 2000) {
        lastHealthMs = millis();
        Serial.print("HEALTH buf=");
        Serial.print(wroverLevel);
        Serial.print(" dry=");
        Serial.print(wroverDry);
        Serial.print(" gap=");
        Serial.print(wroverGap);
        Serial.print(" at=");
        Serial.print(sentBytes / STREAM_RATE);
        Serial.print("s heap=");
        Serial.println(ESP.getFreeHeap());
      }
      if (stalledSince != 0 && !stallWarned && millis() - stalledSince > 3000) {
        stallWarned = true;
        Serial.println("WARNING: receiver buffer full for 3 s and not draining.");
        Serial.println("  If it is not paused, playback has stalled.");
      }
      break;

    case PS_CLOSING:
      // The WROVER should answer with END. If it does not, carry on anyway
      // rather than leaving the player wedged.
      if (millis() - closingSince > CLOSING_TIMEOUT_MS) {
        Serial.println("No END from the WROVER; continuing.");
        runNextAction();
      }
      break;

    case PS_RECORDING:
      serviceRecording();
      break;

    case PS_STOPPED:
      if (rescanPending) {
        rescanPending = false;
        doRescan();
      }
      serviceSd();
      break;
  }

  crumbSet("idle");
  // Always yield. Without this the loop task never sleeps, the idle task never
  // runs, and the task watchdog (5 s, panic-on-timeout) reboots the board with
  // no serial output at all. Batching in pumpAudio() is what makes this safe.
  delay(1);
}
