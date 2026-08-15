/*
 * Bluetooth A2DP & LDAC Connection Manager Header
 */

#ifndef _BTH_MANAGER_H_
#define _BTH_MANAGER_H_

#include <vector>
#include <string>
#include <functional>
#include <stdint.h>
#include <winsock2.h>
#include <ws2bth.h>

#include "../include/a2dp_ldac.h"
#include "../include/ldac_codec.h"

struct BTH_DEVICE_ENTRY {
    uint64_t bth_addr;
    std::wstring name;
    std::string addr_str;
    bool is_connected;
    bool is_remembered;
    bool is_sony_device;
};

typedef struct BTH_STREAM_SESSION* HBTH_SESSION;

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_lost;
    uint32_t current_bitrate_kbps;
    float    buffer_latency_ms;
    float    peak_left;
    float    peak_right;
    bool     is_streaming;
    bool     is_connected;
    int      last_error;
} BTH_STREAM_STATS;

/* Enumerate Bluetooth devices */
std::vector<BTH_DEVICE_ENTRY> bth_enumerate_audio_devices();

/* Helper to parse BD_ADDR from string "XX:XX:XX:XX:XX:XX" */
uint64_t bth_parse_address(const std::string& addr_str);
std::string bth_format_address(uint64_t addr);

struct BTH_CODEC_PROBE_RESULT {
    bool can_connect;
    std::vector<std::string> supported_codecs;
    bool has_sony_ldac;
    bool supports_96khz;
    bool supports_88khz;
    bool supports_48khz;
    bool supports_44khz;
    bool is_streaming_ldac;
    uint32_t current_bitrate_kbps;
    std::string details;
};

/* Session Management */
HBTH_SESSION bth_stream_session_create(uint64_t bth_addr, int sample_rate, int eqmid);
bool         bth_stream_session_start(HBTH_SESSION session);
void         bth_stream_session_stop(HBTH_SESSION session);
void         bth_stream_session_destroy(HBTH_SESSION session);
void         bth_stream_session_set_bitrate_mode(HBTH_SESSION session, int eqmid);
BTH_STREAM_STATS bth_stream_session_get_stats(HBTH_SESSION session);

/* Codec Probe */
BTH_CODEC_PROBE_RESULT bth_probe_device_codecs(uint64_t bth_addr);

#endif /* _BTH_MANAGER_H_ */
