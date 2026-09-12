/*
 * Bluetooth A2DP & LDAC Connection Manager Implementation
 * AVDTP Handshake, Vendor Codec Negotiation, and High-Speed L2CAP Streaming
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <setupapi.h>
#include <devguid.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <iostream>

#include "../../include/bth_manager.h"
#include "../../include/wasapi_streamer.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bthprops.lib")
#pragma comment(lib, "setupapi.lib")

struct BTH_STREAM_SESSION {
    uint64_t bth_addr;
    int sample_rate;
    int eqmid;

    SOCKET sock_sig;
    SOCKET sock_media;
    uint8_t seid;
    bool is_connected;
    int last_error;

    HLDAC_CTX codec_ctx;
    HWASAPI_CAPTURE capture_engine;

    std::atomic<bool> is_running;
    std::thread stream_thread;

    std::mutex pcm_mutex;
    std::vector<int32_t> pcm_queue;

    std::atomic<uint32_t> packets_sent;
    std::atomic<uint32_t> packets_lost;
    std::atomic<uint32_t> current_bitrate_kbps;
    std::atomic<float> buffer_latency_ms;
};

uint64_t bth_parse_address(const std::string& addr_str) {
    uint64_t addr = 0;
    unsigned int bytes[6] = {0};
    if (sscanf_s(addr_str.c_str(), "%x:%x:%x:%x:%x:%x",
                 &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6 ||
        sscanf_s(addr_str.c_str(), "%x-%x-%x-%x-%x-%x",
                 &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            addr = (addr << 8) | (bytes[i] & 0xFF);
        }
    }
    return addr;
}

std::string bth_format_address(uint64_t addr) {
    std::ostringstream oss;
    for (int i = 5; i >= 0; i--) {
        unsigned int b = (addr >> (i * 8)) & 0xFF;
        oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << b;
        if (i > 0) oss << ":";
    }
    return oss.str();
}

std::vector<BTH_DEVICE_ENTRY> bth_enumerate_audio_devices() {
    std::vector<BTH_DEVICE_ENTRY> devices;

    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = { sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS) };
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnUnknown = TRUE;
    searchParams.fReturnConnected = TRUE;

    BLUETOOTH_DEVICE_INFO devInfo = { sizeof(BLUETOOTH_DEVICE_INFO) };

    HANDLE hRadio = NULL;
    BLUETOOTH_FIND_RADIO_PARAMS radioParams = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
    HBLUETOOTH_RADIO_FIND hFindRadio = BluetoothFindFirstRadio(&radioParams, &hRadio);

    if (hFindRadio) {
        searchParams.hRadio = hRadio;
        HBLUETOOTH_DEVICE_FIND hFindDev = BluetoothFindFirstDevice(&searchParams, &devInfo);

        if (hFindDev) {
            do {
                BTH_DEVICE_ENTRY entry;
                entry.bth_addr = devInfo.Address.ullLong;
                entry.name = devInfo.szName;
                entry.addr_str = bth_format_address(entry.bth_addr);
                entry.is_connected = devInfo.fConnected != FALSE;
                entry.is_remembered = devInfo.fRemembered != FALSE;

                /* Identify Sony audio devices */
                std::wstring lower_name = entry.name;
                for (auto &c : lower_name) c = towlower(c);

                entry.is_sony_device = (lower_name.find(L"sony") != std::wstring::npos ||
                                        lower_name.find(L"wh-1000") != std::wstring::npos ||
                                        lower_name.find(L"wf-1000") != std::wstring::npos ||
                                        lower_name.find(L"linkbuds") != std::wstring::npos ||
                                        lower_name.find(L"wi-1000") != std::wstring::npos ||
                                        lower_name.find(L"srs-") != std::wstring::npos ||
                                        lower_name.find(L"walkman") != std::wstring::npos ||
                                        lower_name.find(L"fiio") != std::wstring::npos ||
                                        lower_name.find(L"topping") != std::wstring::npos);

                devices.push_back(entry);
            } while (BluetoothFindNextDevice(hFindDev, &devInfo));

            BluetoothFindDeviceClose(hFindDev);
        }

        CloseHandle(hRadio);
        BluetoothFindRadioClose(hFindRadio);
    }

    /* Fallback / Complement with SetupAPI enumeration */
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_BLUETOOTH, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devData = { sizeof(SP_DEVINFO_DATA) };
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
            wchar_t devName[256] = {0};
            wchar_t instId[512] = {0};

            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_FRIENDLYNAME,
                                              NULL, (PBYTE)devName, sizeof(devName), NULL);
            SetupDiGetDeviceInstanceIdW(hDevInfo, &devData, instId, sizeof(instId)/sizeof(wchar_t), NULL);

            std::wstring instStr = instId;
            size_t devPos = instStr.find(L"DEV_");
            if (devPos != std::wstring::npos && devPos + 16 <= instStr.size()) {
                std::wstring hexAddr = instStr.substr(devPos + 4, 12);
                uint64_t addr = 0;
                swscanf_s(hexAddr.c_str(), L"%llx", &addr);

                if (addr != 0) {
                    bool exists = false;
                    for (auto &d : devices) {
                        if (d.bth_addr == addr) {
                            exists = true;
                            if (d.name.empty() && wcslen(devName) > 0) d.name = devName;
                            break;
                        }
                    }
                    if (!exists && wcslen(devName) > 0) {
                        BTH_DEVICE_ENTRY entry;
                        entry.bth_addr = addr;
                        entry.name = devName;
                        entry.addr_str = bth_format_address(addr);
                        entry.is_connected = true;
                        entry.is_remembered = true;
                        std::wstring lower_name = entry.name;
                        for (auto &c : lower_name) c = towlower(c);
                        entry.is_sony_device = (lower_name.find(L"sony") != std::wstring::npos ||
                                                lower_name.find(L"wh-1000") != std::wstring::npos ||
                                                lower_name.find(L"wf-1000") != std::wstring::npos);
                        devices.push_back(entry);
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    return devices;
}

static void OnAudioCapture(const void *p_pcm, int samples_per_ch, int channels, int sample_rate, int bits_per_sample, void *user_data) {
    (void)channels; (void)sample_rate; (void)bits_per_sample;
    BTH_STREAM_SESSION *session = (BTH_STREAM_SESSION*)user_data;
    if (!session || !session->is_running) return;

    std::lock_guard<std::mutex> lock(session->pcm_mutex);
    const int32_t *p_samples = (const int32_t*)p_pcm;
    session->pcm_queue.insert(session->pcm_queue.end(), p_samples, p_samples + (samples_per_ch * 2));
}

static void StreamWorker(BTH_STREAM_SESSION *session) {
    std::vector<uint8_t> packet_buf(1500);
    std::vector<int32_t> pcm_chunk(256 * 2);

    int sample_rate = session->sample_rate;
    int samples_per_frame = (sample_rate >= 88200) ? 256 : 128;
    int chunk_size = samples_per_frame * 2; /* Stereo samples */

    auto frame_duration = std::chrono::microseconds((samples_per_frame * 1000000) / sample_rate);
    auto next_frame_time = std::chrono::steady_clock::now();

    while (session->is_running) {
        bool has_audio = false;

        {
            std::lock_guard<std::mutex> lock(session->pcm_mutex);
            if (session->pcm_queue.size() >= (size_t)chunk_size) {
                memcpy(pcm_chunk.data(), session->pcm_queue.data(), chunk_size * sizeof(int32_t));
                session->pcm_queue.erase(session->pcm_queue.begin(), session->pcm_queue.begin() + chunk_size);
                has_audio = true;

                float queue_ms = (float)session->pcm_queue.size() / (float)(sample_rate * 2) * 1000.0f;
                session->buffer_latency_ms.store(queue_ms);
            }
        }

        if (has_audio) {
            int samples_consumed = 0;
            int packet_size = 0;

            int res = ldac_codec_encode_packet(
                session->codec_ctx,
                pcm_chunk.data(),
                samples_per_frame,
                packet_buf.data(),
                (int)packet_buf.size(),
                &samples_consumed,
                &packet_size
            );

            if (res == 0 && packet_size > 0) {
                if (session->is_connected && (session->sock_media != INVALID_SOCKET || session->sock_sig != INVALID_SOCKET)) {
                    SOCKET target_sock = (session->sock_media != INVALID_SOCKET) ? session->sock_media : session->sock_sig;
                    int sent = send(target_sock, (const char*)packet_buf.data(), packet_size, 0);
                    if (sent > 0) {
                        session->packets_sent++;
                    } else {
                        session->packets_lost++;
                    }
                }
            }
        }

        session->current_bitrate_kbps.store((uint32_t)(ldac_codec_get_bitrate(session->codec_ctx)));

        next_frame_time += frame_duration;
        std::this_thread::sleep_until(next_frame_time);
    }
}

HBTH_SESSION bth_stream_session_create(uint64_t bth_addr, int sample_rate, int eqmid) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    BTH_STREAM_SESSION *session = new BTH_STREAM_SESSION();
    session->bth_addr = bth_addr;
    session->sample_rate = sample_rate > 0 ? sample_rate : 96000;
    session->eqmid = eqmid >= 0 ? eqmid : LDACBT_EQMID_HQ;
    session->sock_sig = INVALID_SOCKET;
    session->sock_media = INVALID_SOCKET;
    session->seid = 0;
    session->is_connected = false;
    session->last_error = 0;
    session->is_running = false;
    session->packets_sent = 0;
    session->packets_lost = 0;
    session->current_bitrate_kbps = 990;
    session->buffer_latency_ms = 0.0f;

    LDAC_CONFIG cfg = {0};
    cfg.sample_rate = session->sample_rate;
    cfg.channel_mode = LDACBT_CHANNEL_MODE_STEREO;
    cfg.eqmid = session->eqmid;
    cfg.sample_format = LDACBT_SMPL_FMT_S24;
    cfg.mtu = 679;

    session->codec_ctx = ldac_codec_create(&cfg);
    if (!session->codec_ctx) {
        delete session;
        return NULL;
    }

    session->capture_engine = wasapi_capture_create(session->sample_rate, 2, 24);
    return session;
}

bool bth_stream_session_start(HBTH_SESSION session) {
    if (!session || session->is_running) return false;

    /* Create Bluetooth L2CAP Socket */
    session->sock_sig = socket(AF_BTH, SOCK_STREAM, BTHPROTO_L2CAP);
    if (session->sock_sig == INVALID_SOCKET) {
        std::cerr << "Warning: socket(AF_BTH, SOCK_STREAM, BTHPROTO_L2CAP) failed: "
                  << WSAGetLastError() << "\n";
    } else {
        SOCKADDR_BTH sa = {0};
        sa.addressFamily = AF_BTH;
        sa.btAddr = session->bth_addr;
        sa.port = BT_PSM_AVDTP; /* 0x0019 */

        int res = connect(session->sock_sig, (SOCKADDR*)&sa, sizeof(sa));
        if (res == 0) {
            session->is_connected = true;

            /* AVDTP Handshake:
             * 1. AVDTP_DISCOVER (Query Endpoints)
             * 2. AVDTP_SET_CONFIGURATION (Select Sony LDAC 0x012D / 0x00AA)
             * 3. AVDTP_OPEN & AVDTP_START
             */
            uint8_t discover_cmd[] = { 0x10, 0x01 }; /* Label 1, Single, Command: DISCOVER */
            send(session->sock_sig, (const char*)discover_cmd, sizeof(discover_cmd), 0);

            uint8_t recv_buf[512] = {0};
            int n = recv(session->sock_sig, (char*)recv_buf, sizeof(recv_buf), 0);
            if (n >= 2 && (recv_buf[0] & 0x03) == 0x02) { /* Response Accept */
                for (int i = 2; i + 1 < n; i += 2) {
                    uint8_t seid = recv_buf[i] >> 2;
                    uint8_t in_use = (recv_buf[i] >> 1) & 0x01;
                    uint8_t media_type = (recv_buf[i + 1] >> 4) & 0x0F;
                    uint8_t tsep = (recv_buf[i + 1] >> 3) & 0x01;

                    if (media_type == 0 && tsep == 1) { /* Audio Sink */
                        session->seid = seid;
                        break;
                    }
                }
            }

            if (session->seid == 0) session->seid = 1;

            /* Set Configuration for Sony LDAC Vendor Codec */
            uint8_t set_cfg_cmd[] = {
                0x20, 0x03,                                /* Label 2, Command: SET_CONFIGURATION */
                (uint8_t)(session->seid << 2),             /* ACP SEID */
                0x04,                                      /* INT SEID (0x01 << 2) */
                0x01, 0x00,                                /* Media Transport */
                0x07, 0x0A,                                /* Media Codec (Length = 10 bytes) */
                0x00,                                      /* Audio */
                0xFF,                                      /* Vendor Specific */
                0x2D, 0x01, 0x00, 0x00,                    /* Vendor ID: Sony Corporation (0x0000012D) */
                0xAA, 0x00,                                /* Codec ID: LDAC (0x00AA) */
                (uint8_t)((session->sample_rate == 96000) ? 0x04 : 0x10), /* Sample Rate: 96k (0x04) or 48k (0x10) */
                0x01                                       /* Channel Mode: Stereo (0x01) */
            };
            send(session->sock_sig, (const char*)set_cfg_cmd, sizeof(set_cfg_cmd), 0);
            recv(session->sock_sig, (char*)recv_buf, sizeof(recv_buf), 0);

            /* Open Stream */
            uint8_t open_cmd[] = { 0x30, 0x06, (uint8_t)(session->seid << 2) };
            send(session->sock_sig, (const char*)open_cmd, sizeof(open_cmd), 0);
            recv(session->sock_sig, (char*)recv_buf, sizeof(recv_buf), 0);

            /* Start Stream */
            uint8_t start_cmd[] = { 0x40, 0x07, (uint8_t)(session->seid << 2) };
            send(session->sock_sig, (const char*)start_cmd, sizeof(start_cmd), 0);
            recv(session->sock_sig, (char*)recv_buf, sizeof(recv_buf), 0);
        } else {
            session->last_error = WSAGetLastError();
            session->is_connected = false;
            closesocket(session->sock_sig);
            session->sock_sig = INVALID_SOCKET;
            std::cerr << "Notice: Direct L2CAP connect returned " << session->last_error
                      << " (Windows BthA2dp.sys locks user-mode L2CAP; kernel filter driver required)\n";
        }
    }

    session->is_running = true;

    /* Start WASAPI Loopback Capture */
    if (session->capture_engine) {
        wasapi_capture_start(session->capture_engine, OnAudioCapture, session);
    }

    /* Start High-Priority Streaming Thread */
    session->stream_thread = std::thread(StreamWorker, session);
    return true;
}

void bth_stream_session_stop(HBTH_SESSION session) {
    if (!session || !session->is_running) return;

    session->is_running = false;

    if (session->capture_engine) {
        wasapi_capture_stop(session->capture_engine);
    }

    if (session->stream_thread.joinable()) {
        session->stream_thread.join();
    }

    if (session->sock_sig != INVALID_SOCKET && session->is_connected && session->seid > 0) {
        uint8_t close_cmd[] = { 0x50, 0x08, (uint8_t)(session->seid << 2) };
        send(session->sock_sig, (const char*)close_cmd, sizeof(close_cmd), 0);
        closesocket(session->sock_sig);
        session->sock_sig = INVALID_SOCKET;
    }
    if (session->sock_media != INVALID_SOCKET) {
        closesocket(session->sock_media);
        session->sock_media = INVALID_SOCKET;
    }

    session->is_connected = false;
}

void bth_stream_session_destroy(HBTH_SESSION session) {
    if (!session) return;
    bth_stream_session_stop(session);

    if (session->codec_ctx) {
        ldac_codec_destroy(session->codec_ctx);
        session->codec_ctx = NULL;
    }
    if (session->capture_engine) {
        wasapi_capture_destroy(session->capture_engine);
        session->capture_engine = NULL;
    }

    delete session;
}

void bth_stream_session_set_bitrate_mode(HBTH_SESSION session, int eqmid) {
    if (!session || !session->codec_ctx) return;
    session->eqmid = eqmid;
    ldac_codec_set_bitrate_mode(session->codec_ctx, eqmid);
}

BTH_STREAM_STATS bth_stream_session_get_stats(HBTH_SESSION session) {
    BTH_STREAM_STATS stats = {0};
    if (!session) return stats;

    stats.packets_sent = session->packets_sent.load();
    stats.packets_lost = session->packets_lost.load();
    stats.current_bitrate_kbps = session->current_bitrate_kbps.load();
    stats.buffer_latency_ms = session->buffer_latency_ms.load();
    stats.is_streaming = session->is_running.load();
    stats.is_connected = session->is_connected;
    stats.last_error = session->last_error;

    if (session->capture_engine) {
        wasapi_capture_get_levels(session->capture_engine, &stats.peak_left, &stats.peak_right);
    }
    return stats;
}

BTH_CODEC_PROBE_RESULT bth_probe_device_codecs(uint64_t bth_addr) {
    BTH_CODEC_PROBE_RESULT res = {0};
    res.can_connect = false;
    res.has_sony_ldac = false;
    res.supports_96khz = false;
    res.supports_88khz = false;
    res.supports_48khz = false;
    res.supports_44khz = false;
    res.is_streaming_ldac = false;
    res.current_bitrate_kbps = 990;

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_L2CAP);
    if (s != INVALID_SOCKET) {
        SOCKADDR_BTH sa = {0};
        sa.addressFamily = AF_BTH;
        sa.btAddr = bth_addr;
        sa.port = BT_PSM_AVDTP;

        DWORD timeout = 2000; /* 2s timeout */
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        if (connect(s, (SOCKADDR*)&sa, sizeof(sa)) == 0) {
            res.can_connect = true;

            /* AVDTP Discover */
            uint8_t discover_cmd[] = { 0x10, 0x01 };
            send(s, (const char*)discover_cmd, sizeof(discover_cmd), 0);

            uint8_t recv_buf[512] = {0};
            int n = recv(s, (char*)recv_buf, sizeof(recv_buf), 0);
            if (n >= 2 && (recv_buf[0] & 0x03) == 0x02) {
                for (int i = 2; i + 1 < n; i += 2) {
                    uint8_t seid = recv_buf[i] >> 2;
                    uint8_t media_type = (recv_buf[i + 1] >> 4) & 0x0F;
                    uint8_t tsep = (recv_buf[i + 1] >> 3) & 0x01;

                    if (media_type == 0 && tsep == 1) {
                        /* Get Capabilities for this endpoint */
                        uint8_t get_caps_cmd[] = { 0x20, 0x02, (uint8_t)(seid << 2) };
                        send(s, (const char*)get_caps_cmd, sizeof(get_caps_cmd), 0);

                        uint8_t caps_buf[512] = {0};
                        int caps_len = recv(s, (char*)caps_buf, sizeof(caps_buf), 0);
                        if (caps_len >= 3 && (caps_buf[0] & 0x03) == 0x02) {
                            /* Parse capabilities elements */
                            int idx = 2;
                            while (idx + 1 < caps_len) {
                                uint8_t cat = caps_buf[idx];
                                uint8_t len = caps_buf[idx + 1];
                                idx += 2;
                                if (idx + len > caps_len) break;

                                if (cat == 0x07 && len >= 2) { /* Media Codec */
                                    uint8_t codec_type = caps_buf[idx + 1];
                                    if (codec_type == 0x00) {
                                        res.supported_codecs.push_back("SBC (Mandatory Baseline)");
                                    } else if (codec_type == 0x02) {
                                        res.supported_codecs.push_back("AAC (MPEG Advanced Audio)");
                                    } else if (codec_type == 0xFF && len >= 8) {
                                        uint32_t vendor_id = caps_buf[idx + 2] | (caps_buf[idx + 3] << 8) |
                                                            (caps_buf[idx + 4] << 16) | (caps_buf[idx + 5] << 24);
                                        uint16_t codec_id = caps_buf[idx + 6] | (caps_buf[idx + 7] << 8);

                                        if (vendor_id == 0x0000012D && codec_id == 0x00AA) {
                                            res.has_sony_ldac = true;
                                            res.supported_codecs.push_back("SONY LDAC (High-Resolution 990 kbps / 96kHz)");

                                            if (len >= 10) {
                                                uint8_t sf_mask = caps_buf[idx + 8];
                                                if (sf_mask & 0x04) res.supports_96khz = true;
                                                if (sf_mask & 0x08) res.supports_88khz = true;
                                                if (sf_mask & 0x10) res.supports_48khz = true;
                                                if (sf_mask & 0x20) res.supports_44khz = true;
                                            }
                                        }
                                    }
                                }
                                idx += len;
                            }
                        }
                    }
                }
            }

            closesocket(s);
        }
    }

    /* Check service status */
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenService(hSCM, TEXT("LdacAudioService"), SERVICE_QUERY_STATUS);
        if (hService) {
            SERVICE_STATUS_PROCESS ssp;
            DWORD bytesNeeded;
            if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
                if (ssp.dwCurrentState == SERVICE_RUNNING) {
                    res.is_streaming_ldac = true;
                }
            }
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }

    /* Check Registry for active preference */
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\LDAC-Windows"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD br = 0, sz = sizeof(DWORD);
        if (RegQueryValueEx(hKey, TEXT("PreferredBitrate"), NULL, NULL, (LPBYTE)&br, &sz) == ERROR_SUCCESS && br > 0) {
            res.current_bitrate_kbps = br;
        }
        RegCloseKey(hKey);
    }

    return res;
}
