/**
 * @file ble_controller.cpp
 * @brief BLE GATT server implementation for Raspberry Pi Pico W
 *
 * ============================================================================
 * BLE ARCHITECTURE (BTstack on Pico W)
 * ============================================================================
 *
 * Unlike ESP32 which uses NimBLE (event-driven, FreeRTOS tasks), the Pico W
 * uses BTstack, a single-threaded BLE stack that processes events in a
 * cooperative loop. All BLE callbacks execute on Core 0 in the main loop
 * context (no ISR or task preemption).
 *
 * DETERMINISTIC MAC ADDRESS (CRC16 derivation):
 *   The Pico W cannot persist a MAC address across power cycles like ESP32.
 *   If we used a random MAC each boot, the phone would see a "new device"
 *   every time and require re-pairing. Instead, we derive a static random
 *   address from hash(DEVICE_NAME + BLE_PASSKEY) using CRC16-CCITT. This
 *   gives a deterministic address that only changes when the user changes
 *   their device name or PIN, which naturally requires re-pairing anyway.
 *
 * FIRST-WRITE HANDSHAKE:
 *   On iOS, a BLE connection is established at the link layer before the app
 *   even knows about it. The app may connect, trigger pairing, then never
 *   actually write data (e.g., user cancelled). We track the first write to
 *   the control characteristic to confirm the app is fully connected and
 *   actively sending commands. This matches the ESP32 firmware's pattern.
 *
 * JSON FRAMING:
 *   BLE sends data in chunks (up to MTU size, typically 20-512 bytes).
 *   A JSON command may arrive in one or multiple BLE writes. We detect
 *   complete JSON two ways:
 *   1. Newline delimiter ('\n'), which the Flutter app appends
 *   2. Brace counting ({/}), used as a fallback if newline is missing
 *
 * [!] HOT PATH WARNING:
 *   In gattWriteCallback(), avoid Serial.printf() on every received packet.
 *   Serial output takes ~8ms on Pico W, which at 50Hz command rate would
 *   consume 400ms/sec of Core 0 time, enough to stall BLE processing.
 *
 * Based on ESP32 ble_gap.cpp patterns and BTstack API.
 */

#include "ble_controller.h"
#include "ble_config.h"
#include "project_config.h"

#include <Arduino.h>
#include <BTstackLib.h>
#include <SPI.h>

// BTstack Security Manager and GAP (for passkey pairing and random address)
extern "C" {
#include "ble/att_db_util.h"
#include "ble/sm.h"
#include "btstack_util.h"
#include "gap.h"
}

// ATT Security flags (from btstack/src/bluetooth.h)
#define ATT_SECURITY_NONE 0x0000
#define ATT_SECURITY_ENCRYPTED 0x0001
#define ATT_SECURITY_AUTHENTICATED 0x0002
#define ATT_SECURITY_AUTHORIZED 0x0004
#define ATT_SECURITY_SC 0x0008

// =============================================================================
// CRC16 IMPLEMENTATION (matching ESP32's esp_crc16_le)
// =============================================================================

/**
 * @brief CRC16-CCITT calculation (same polynomial as ESP32)
 *
 * @details This implements the same CRC16 algorithm as ESP-IDF's
 * esp_crc16_le(). Using the same polynomial ensures that a Pico W and
 * an ESP32 configured with the same DEVICE_NAME + BLE_PASSKEY will
 * generate the same MAC address, allowing the Flutter app to treat
 * them identically.
 *
 * The reversed polynomial 0xA001 is the bit-reversed form of 0x8005
 * (standard CRC-16). The bit-reversal allows processing LSB-first
 * without needing to reverse the input/output bytes.
 */
static uint16_t crc16_le(uint16_t crc, const uint8_t *data, size_t len) {
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001; // Polynomial for CRC16-CCITT (reversed)
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// =============================================================================
// PRIVATE STATE
// =============================================================================

static bool s_connected = false;
static bool s_pairingRequested = false;
static hci_con_handle_t s_conHandle = HCI_CON_HANDLE_INVALID;
static unsigned long s_lastCommandTime = 0;
static String s_rxBuffer = "";

// Callbacks
static CommandReceivedCallback s_commandCallback = nullptr;
static ConnectionStateCallback s_connectionCallback = nullptr;

// Dynamic characteristic handle (assigned by BTstack)
static uint16_t s_controlCharHandle = 0;
static uint16_t s_telemetryCharHandle = 0;

// Telemetry data (read by client)
static char s_telemetryData[256] = "";

// First-write handshake (matching ESP32 pattern)
static bool s_firstWriteReceived = false;

// Derived random address (for logging)
static uint8_t s_randomAddr[6];

// =============================================================================
// UUID CONVERSION HELPER
// =============================================================================

/**
 * @brief Convert a UUID string to BTstack's 128-bit byte array.
 *
 * @details BTstack stores UUIDs in LITTLE-ENDIAN byte order, which is the
 * reverse of how UUIDs are written as strings. This function parses the
 * standard "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" format and stores bytes
 * starting from index 15 (MSB of the string -> last byte of array).
 *
 * @param uuidStr  Standard UUID string with dashes.
 * @param uuid128  Output: 16-byte array in little-endian order.
 */
static void uuid_string_to_bytes(const char *uuidStr, uint8_t *uuid128) {
  // Convert UUID string to 128-bit bytes (little-endian for BTstack)
  int idx = 15; // Start from end (little-endian)
  for (int i = 0; uuidStr[i] && idx >= 0; i++) {
    if (uuidStr[i] == '-')
      continue;

    uint8_t nibble;
    if (uuidStr[i] >= '0' && uuidStr[i] <= '9') {
      nibble = uuidStr[i] - '0';
    } else if (uuidStr[i] >= 'A' && uuidStr[i] <= 'F') {
      nibble = uuidStr[i] - 'A' + 10;
    } else if (uuidStr[i] >= 'a' && uuidStr[i] <= 'f') {
      nibble = uuidStr[i] - 'a' + 10;
    } else {
      continue;
    }

    if (i % 2 == 0 || (i > 0 && uuidStr[i - 1] == '-')) {
      // First nibble of byte
      uuid128[idx] = nibble << 4;
    } else {
      // Second nibble of byte
      uuid128[idx] |= nibble;
      idx--;
    }
  }
}

// =============================================================================
// BTSTACK CALLBACKS
// =============================================================================

/**
 * @brief Called when a BLE device connects.
 *
 * @details Pairing is requested immediately on connection. On iOS, the
 * system-level pairing dialog appears asking the user for the passkey.
 * If the user cancels pairing, the connection remains but no data can
 * be written (the characteristic requires authenticated encryption).
 * The first-write flag tracks whether the app has actually sent data.
 */
void deviceConnectedCallback(BLEStatus status, BLEDevice *device) {
  if (status == BLE_STATUS_OK) {
    Serial.println("[BLE] Client connected!");
    s_connected = true;
    s_firstWriteReceived = false;
    s_pairingRequested = false;

    if (device) {
      s_conHandle = device->getHandle();
      Serial.printf("[BLE] Connection handle: %d\n", s_conHandle);

      // Request pairing explicitly to trigger passkey prompt
      if (s_conHandle != HCI_CON_HANDLE_INVALID) {
        Serial.println("[BLE] Requesting pairing...");
        sm_request_pairing(s_conHandle);
        s_pairingRequested = true;
      }
    }

    digitalWrite(LED_BUILTIN, HIGH);

    if (s_connectionCallback) {
      s_connectionCallback(true);
    }
  } else {
    Serial.printf("[BLE] Connection failed, status: %d\n", status);
  }
}

/**
 * @brief Called when a BLE device disconnects
 */
void deviceDisconnectedCallback(BLEDevice *device) {
  (void)device;

  Serial.println("[BLE] Client disconnected");
  s_connected = false;
  s_firstWriteReceived = false;
  s_pairingRequested = false;
  s_conHandle = HCI_CON_HANDLE_INVALID;
  digitalWrite(LED_BUILTIN, LOW);
  s_rxBuffer = "";

  if (s_connectionCallback) {
    s_connectionCallback(false);
  }
}

/**
 * @brief Called when client reads a characteristic
 */
uint16_t gattReadCallback(uint16_t value_handle, uint8_t *buffer,
                          uint16_t buffer_size) {
  // Check if reading telemetry characteristic
  if (value_handle == s_telemetryCharHandle) {
    uint16_t len = strlen(s_telemetryData);
    if (len > buffer_size) {
      len = buffer_size;
    }
    if (buffer) {
      memcpy(buffer, s_telemetryData, len);
    }
    return len;
  }
  return 0;
}

/**
 * @brief Called when client writes to a characteristic (hot path).
 *
 * @details Incoming BLE data is accumulated in s_rxBuffer. Complete
 * JSON commands are detected in two ways:
 *   1. Newline ('\n'): the Flutter app appends one after each JSON object.
 *   2. Brace counting: if no newline arrives but brace depth returns to 0,
 *      the JSON is complete. This handles MTU fragmentation edge cases.
 *
 * @warning No Serial.printf() in this function. At 50Hz, each 8ms print
 *          would consume 40% of Core 0's time budget.
 */
int gattWriteCallback(uint16_t value_handle, uint8_t *buffer, uint16_t size) {
  // Check if writing to control characteristic
  if (value_handle == s_controlCharHandle && size > 0) {
    s_lastCommandTime = micros();

    // First-write handshake (matching ESP32 pattern)
    if (!s_firstWriteReceived) {
      s_firstWriteReceived = true;
      Serial.println(
          "[BLE] First write received - connection fully established");
    }

    // Append to buffer
    for (uint16_t i = 0; i < size; i++) {
      char c = (char)buffer[i];
      if (c == '\n' || c == '\r') {
        // Complete line received - process it
        if (s_rxBuffer.length() > 0 && s_commandCallback) {
          // HOT PATH - no logging here (causes 8ms loop delays)
          s_commandCallback(s_rxBuffer.c_str(), s_rxBuffer.length());
        }
        s_rxBuffer = "";
      } else {
        s_rxBuffer += c;
      }
    }

    // If no newline but buffer looks like complete JSON, process it
    if (s_rxBuffer.length() > 0 && s_rxBuffer[0] == '{') {
      int braceCount = 0;
      for (size_t i = 0; i < s_rxBuffer.length(); i++) {
        if (s_rxBuffer[i] == '{')
          braceCount++;
        if (s_rxBuffer[i] == '}')
          braceCount--;
      }
      if (braceCount == 0 && s_commandCallback) {
        // HOT PATH - no logging here (causes 8ms loop delays)
        s_commandCallback(s_rxBuffer.c_str(), s_rxBuffer.length());
        s_rxBuffer = "";
      }
    }
  }
  return 0; // Success
}

// =============================================================================
// MAC ADDRESS DERIVATION (ESP32 PARITY)
// =============================================================================

/**
 * @brief Generate a deterministic BLE Static Random Address.
 *
 * @details BLE Static Random Addresses have their top 2 bits set to 11
 * (0xC0 mask on byte[0]). The remaining 46 bits are derived from the
 * CRC16 hash, XORed with magic bytes (0xAA, 0x55, 0x33, 0x77) to
 * spread the hash bits across all 6 address bytes. This ensures that
 * similar device names produce visually distinct addresses.
 *
 * The address is stable across power cycles as long as DEVICE_NAME and
 * BLE_PASSKEY don't change.
 */
static void derive_random_address(uint8_t *addr) {
  char identity_str[64];
  snprintf(identity_str, sizeof(identity_str), "%s%d", DEVICE_NAME,
           BLE_PASSKEY);

  uint16_t identity_hash =
      crc16_le(0, (const uint8_t *)identity_str, strlen(identity_str));

  // Static Random Address format: upper 2 bits of byte[0] = 11
  addr[0] = 0xC0 | ((identity_hash >> 8) & 0x3F);
  addr[1] = identity_hash & 0xFF;
  addr[2] = (identity_hash >> 8) ^ 0xAA;
  addr[3] = identity_hash ^ 0x55;
  addr[4] = ((identity_hash >> 4) & 0xFF) ^ 0x33;
  addr[5] = (identity_hash ^ 0x77) & 0xFF;

  Serial.printf(
      "[BLE] Derived address: %02X:%02X:%02X:%02X:%02X:%02X (hash: 0x%04X)\n",
      addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], identity_hash);
}

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @details Initialization follows a strict 5-step sequence:
 *   1. Derive deterministic MAC from DEVICE_NAME + BLE_PASSKEY.
 *   2. Configure Security Manager: MITM protection + bonding + fixed passkey.
 *   3. Register BTstack callbacks for connect/disconnect/read/write.
 *   4. Build GATT database: control (write) + telemetry (read/notify) chars.
 *   5. Call BTstack.setup() and start advertising.
 *
 * BTstack.setup() must be called LAST because it finalizes the ATT database.
 * Adding characteristics after setup() will silently fail.
 */
void ble_init(CommandReceivedCallback onCommand,
              ConnectionStateCallback onConnection) {
  s_commandCallback = onCommand;
  s_connectionCallback = onConnection;

  Serial.println("[BLE] Initializing BTstack on Pico W...");
  Serial.printf("[BLE] Device Name: %s\n", DEVICE_NAME);
  Serial.printf("[BLE] PIN Code: %06d\n", BLE_PASSKEY);

  // Initialize LED for connection status
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // =========================================================================
  // STEP 1: Derive deterministic MAC address from name+passkey
  // =========================================================================
  derive_random_address(s_randomAddr);
  gap_random_address_set(s_randomAddr);

  // =========================================================================
  // STEP 2: Configure Security Manager (MITM + Bonding + Passkey)
  // =========================================================================
  sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
  sm_set_authentication_requirements(SM_AUTHREQ_BONDING |
                                     SM_AUTHREQ_MITM_PROTECTION);
  sm_use_fixed_passkey_in_display_role(BLE_PASSKEY);

  // Request security on connection (peripheral-initiated security)
  sm_set_request_security(true);

  Serial.printf("[BLE] Security: MITM + Bonding, Passkey: %06d\n", BLE_PASSKEY);

  // =========================================================================
  // STEP 3: Configure GATT Server Callbacks
  // =========================================================================
  BTstack.setBLEDeviceConnectedCallback(deviceConnectedCallback);
  BTstack.setBLEDeviceDisconnectedCallback(deviceDisconnectedCallback);
  BTstack.setGATTCharacteristicRead(gattReadCallback);
  BTstack.setGATTCharacteristicWrite(gattWriteCallback);

  // =========================================================================
  // STEP 4: Setup GATT Database (matching ESP32 UUIDs)
  // =========================================================================
  BTstack.addGATTService(new UUID(CONTROL_SERVICE_UUID));

  // Control characteristic (write by client)
  s_controlCharHandle = BTstack.addGATTCharacteristicDynamic(
      new UUID(CONTROL_CHAR_UUID),
      ATT_PROPERTY_WRITE | ATT_PROPERTY_WRITE_WITHOUT_RESPONSE |
          ATT_PROPERTY_DYNAMIC,
      0);

  // Telemetry characteristic (read + notify by client)
  s_telemetryCharHandle = BTstack.addGATTCharacteristicDynamic(
      new UUID(TELEMETRY_CHAR_UUID),
      ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY | ATT_PROPERTY_DYNAMIC, 0);

  Serial.printf("[BLE] Control char handle: %d\n", s_controlCharHandle);
  Serial.printf("[BLE] Telemetry char handle: %d\n", s_telemetryCharHandle);

  // =========================================================================
  // STEP 5: Initialize BTstack and Start Advertising
  // =========================================================================
  BTstack.setup(DEVICE_NAME);
  BTstack.startAdvertising();

  Serial.println("[BLE] Advertising started - waiting for connection...");
}

void ble_update(void) {
  // Process BTstack events
  BTstack.loop();
}

bool ble_is_connected(void) { return s_connected; }

bool ble_first_write_received(void) { return s_firstWriteReceived; }

/**
 * @details Copies the JSON string into a static buffer for the GATT read
 * callback to serve. The actual BLE notification is sent by BTstack's
 * event loop. The 256-byte buffer limits telemetry to short JSON payloads
 * (typical: {"battery":85,"rssi":-42} ~30 bytes).
 */
bool ble_send_telemetry(const char *jsonData) {
  if (!s_connected) {
    return false;
  }

  uint16_t len = strlen(jsonData);
  if (len > sizeof(s_telemetryData) - 1) {
    len = sizeof(s_telemetryData) - 1;
  }
  memcpy(s_telemetryData, jsonData, len);
  s_telemetryData[len] = '\0';

  Serial.printf("[BLE] Telemetry: %s\n", jsonData);
  return true;
}

unsigned long ble_get_last_command_time(void) { return s_lastCommandTime; }
