#pragma once

#include <stddef.h>

/** Uma rede Wi-Fi conhecida (Spec 01 §10.1). */
typedef struct {
    char ssid[33];
    char psk[65];
} wifi_profile_t;

#define WIFI_PROFILE_MAX 8

/**
 * Carrega perfis de `/sdcard/wifi.cfg` (JSON, nome FAT 8.3).
 * Fallback: rede única do Kconfig se o arquivo estiver ausente/vazio.
 * @return número de redes carregadas (0 = sem credencial).
 */
int wifi_profiles_load(wifi_profile_t *out, size_t max_out);

/** SSID da última conexão bem-sucedida (string vazia se nenhuma). */
const char *wifi_profiles_last_ssid(void);

void wifi_profiles_set_last_ssid(const char *ssid);
