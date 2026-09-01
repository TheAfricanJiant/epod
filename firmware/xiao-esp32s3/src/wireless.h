// ============================================================================
//  Smart ePod - wireless layer (BLE control + Wi-Fi file upload)
//
//  Everything the ePodd companion app talks to lives behind this interface, so
//  the player itself does not have to know whether a command arrived over USB
//  serial, BLE, or not at all.
//
//  Two transports, deliberately different jobs:
//
//    BLE   - always on, tiny, for control and telemetry. ASCII lines in on
//            EP01, ASCII lines out on EP02, bulk file bytes on EP03.
//    Wi-Fi - off until asked for, and only while stopped. A Soft-AP with an
//            HTTP POST endpoint, which is far faster than BLE for whole
//            tracks.
//
//  THE RULE THAT MATTERS: neither radio may be transmitting while audio is
//  streaming. Both share the 3.3 V rail with the WROOM, and this project has
//  already been bitten once by a rail that sagged (see TROUBLESHOOTING item
//  14). Wi-Fi is therefore forced off before playback starts.
// ============================================================================
#pragma once
#include <Arduino.h>

// --- lifecycle --------------------------------------------------------------
void wirelessBegin(const char* deviceName);
void wirelessService();                      // call once per loop pass

// --- Wi-Fi ------------------------------------------------------------------
bool wirelessWifiStart();                    // Soft-AP + HTTP server
void wirelessWifiStop();
bool wirelessWifiActive();
const char* wirelessApAddress();

// --- BLE --------------------------------------------------------------------
bool wirelessBleConnected();
bool wirelessBleStart();
void wirelessBleStop();
bool wirelessBleActive();

// --- telemetry out ----------------------------------------------------------
// Mirrors one ASCII line to the app. Safe to call for every line the player
// produces; the ones the app does not care about are filtered here.
void wirelessNotify(const char* line);

// --- commands in ------------------------------------------------------------
// Pops one queued ASCII command from the app. Commands are queued rather than
// executed inside the radio callback: those run on another task, and the
// player must only ever change state from the main loop.
bool wirelessNextCommand(char* out, size_t outSize);

// --- uploads ----------------------------------------------------------------
bool wirelessUploadBusy();

// ============================================================================
//  Provided by main.cpp - the seam between the radios and the player
// ============================================================================
bool epodCanAcceptUpload();                  // true only while stopped
void epodUploadFinished(const char* name);   // rescan and report
void epodStatusLine(char* out, size_t outSize);
const char* epodMusicDir();
void epodUploadStarted(const char* name);
void epodUploadProgress(uint32_t bytes, uint32_t total);
void epodUploadFailed();

// --- signage web API --------------------------------------------------------
void epodApiState(char* out, size_t outSize);
void epodApiCommand(const char* body, char* out, size_t outSize);
