#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define USB_STREAM SerialUSB
#define LORA_STREAM Serial2

static const uint32_t USB_BAUD = 115200;
static const uint32_t LORA_BAUD = 57600;

// --- CONFIGURATION LORA DE L'EQUIPE ---
static const char* SHARED_TOKEN = "SODAQ2026";
static const char* RADIO_FREQ = "868100000";
static const char* RADIO_SF = "sf7";
static const char* RADIO_BW = "125";
static const char* RADIO_CR = "4/5";
static const char* RADIO_PWR = "14";
static const char* RADIO_PRLEN = "8";
static const char* RADIO_SYNC = "12";
static const char* RADIO_WDT = "3000";

// --- FONCTIONS UTILITAIRES DE L'EQUIPE ---
static bool readSerialLine(HardwareSerial& serial, char* out, size_t outSize, uint32_t timeoutMs) {
    uint32_t start = millis();
    size_t index = 0;
    while ((millis() - start) < timeoutMs) {
        while (serial.available() > 0) {
            char c = (char)serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                if (index >= outSize) index = outSize - 1;
                out[index] = '\0';
                return true;
            }
            if (index < outSize - 1) out[index++] = c;
        }
    }
    if (outSize > 0) out[0] = '\0';
    return false;
}

static void writeCommand(const char* cmd) {
    LORA_STREAM.print(cmd);
    LORA_STREAM.print("\r\n");
}

static bool waitLineEquals(const char* expected, uint32_t timeoutMs) {
    char buf[320];
    while (readSerialLine(LORA_STREAM, buf, sizeof(buf), timeoutMs)) {
        if (buf[0] == '\0') continue;
        if (strcmp(buf, expected) == 0) return true;
    }
    return false;
}

static bool sendCommandExpectOk(const char* cmd, uint32_t timeoutMs) {
    writeCommand(cmd);
    return waitLineEquals("ok", timeoutMs);
}

static bool pauseMac(void) {
    writeCommand("mac pause");
    char buf[320];
    if (!readSerialLine(LORA_STREAM, buf, sizeof(buf), 1000)) return false;
    if (buf[0] == '\0') return false;
    for (size_t i = 0; buf[i] != '\0'; i++) {
        if (buf[i] < '0' || buf[i] > '9') return false;
    }
    return true;
}

static void bytesToHex(const uint8_t* in, size_t len, char* out, size_t outMax) {
    static const char* hex = "0123456789ABCDEF";
    size_t need = len * 2 + 1;
    if (outMax < need) {
        if (outMax > 0) out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static bool radioConfig(void) {
    writeCommand("sys reset");
    delay(500);
    char dump[320];
    while (readSerialLine(LORA_STREAM, dump, sizeof(dump), 100)) {}

    if (!pauseMac()) return false;
    if (!sendCommandExpectOk("radio set mod lora", 1000)) return false;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "radio set freq %s", RADIO_FREQ);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set sf %s", RADIO_SF);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set bw %s", RADIO_BW);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set cr %s", RADIO_CR);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set pwr %s", RADIO_PWR);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set prlen %s", RADIO_PRLEN);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    if (!sendCommandExpectOk("radio set crc on", 1000)) return false;
    if (!sendCommandExpectOk("radio set iqi off", 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set wdt %s", RADIO_WDT);
    if (!sendCommandExpectOk(cmd, 1000)) return false;
    snprintf(cmd, sizeof(cmd), "radio set sync %s", RADIO_SYNC);
    if (!sendCommandExpectOk(cmd, 1000)) return false;

    return true;
}

static bool radioSendText(const char* text) {
    uint8_t data[128];
    size_t len = strlen(text);
    if (len == 0 || len > sizeof(data)) return false;
    memcpy(data, text, len);

    char hex[257];
    bytesToHex(data, len, hex, sizeof(hex));

    if (!pauseMac()) return false;

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "radio tx %s", hex);
    writeCommand(cmd);

    if (!waitLineEquals("ok", 1000)) return false;

    char result[320];
    if (!readSerialLine(LORA_STREAM, result, sizeof(result), 5000)) return false;

    return strcmp(result, "radio_tx_ok") == 0;
}

// --- FONCTIONS DE PROTOCOLE DE L'EQUIPE ---
// On utilisera principalement sendFind pour signaler la découverte de la mine
bool sendFind(uint16_t playerId, uint16_t mineId, bool isFind) {
    char payload[96];
    snprintf(
        payload, sizeof(payload), "FD;%u;%u;%u;%s",
        playerId, mineId, isFind ? 1 : 0, SHARED_TOKEN
    );
    return radioSendText(payload);
}

// ==========================================
// PROGRAMME PRINCIPAL
// ==========================================

void setup() {
    USB_STREAM.begin(USB_BAUD);
    LORA_STREAM.begin(LORA_BAUD);
    delay(1500);

    // Configuration initiale du module LoRa
    if (!radioConfig()) {
        USB_STREAM.println("❌ LORA_CONFIG_FAIL");
        return;
    }
    
    USB_STREAM.println("✅ SENDER_READY : En attente du signal de l'IA (Python)...");
}

void loop() {
    // On vérifie en permanence si le PC (Python) nous envoie quelque chose via le câble USB
    if (USB_STREAM.available() > 0) {
        String msg = USB_STREAM.readStringUntil('\n'); // Lit jusqu'au saut de ligne
        msg.trim(); // Nettoie les espaces et les \r cachés

        // Si l'IA Python crie "DETECTED"
        if (msg == "DETECTED") {
            USB_STREAM.println("🚨 Ordre reçu depuis le PC : Mine détectée ! Envoi réseau...");
            
            // On déclenche la trame d'équipe ! 
            // NOTE : Tu peux changer les ID (joueur=1, mine=10) selon vos règles de projet.
            bool ok = sendFind(1, 10, true);
            
            if (ok) {
                USB_STREAM.println("✅ Trame LoRa 'FD' (Find) envoyée avec succès.");
            } else {
                USB_STREAM.println("❌ Erreur lors de l'envoi LoRa.");
            }
        }
    }
}