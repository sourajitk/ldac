/*
 * Sony LDAC Background Streaming Driver Service
 * Windows Service Architecture for Automatic Hi-Res Audio Routing
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#include "../../include/bth_manager.h"
#include "../../include/wasapi_streamer.h"

#define SERVICE_NAME TEXT("LdacAudioService")

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::atomic<bool>     g_IsRunning(false);

static void ReadConfig(int *bitrate, int *sample_rate, bool *abr, bool *autoconnect) {
    *bitrate = 990;
    *sample_rate = 96000;
    *abr = true;
    *autoconnect = true;

    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\LDAC-Windows"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, sz = sizeof(DWORD);
        if (RegQueryValueEx(hKey, TEXT("PreferredBitrate"), NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS) *bitrate = (int)val;
        if (RegQueryValueEx(hKey, TEXT("PreferredSampleRate"), NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS) *sample_rate = (int)val;
        if (RegQueryValueEx(hKey, TEXT("AdaptiveBitrate"), NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS) *abr = (val != 0);
        if (RegQueryValueEx(hKey, TEXT("AutoConnectSony"), NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS) *autoconnect = (val != 0);
        RegCloseKey(hKey);
    }
}

static void ServiceWorker() {
    int bitrate = 990;
    int sample_rate = 96000;
    bool abr = true;
    bool autoconnect = true;
    ReadConfig(&bitrate, &sample_rate, &abr, &autoconnect);

    int eqmid = LDACBT_EQMID_HQ;
    if (bitrate == 660) eqmid = LDACBT_EQMID_SQ;
    else if (bitrate == 330) eqmid = LDACBT_EQMID_MQ;

    HBTH_SESSION active_session = NULL;
    uint64_t connected_addr = 0;

    while (g_IsRunning) {
        if (!active_session) {
            auto devices = bth_enumerate_audio_devices();
            for (const auto& dev : devices) {
                if (dev.is_connected && dev.is_sony_device) {
                    active_session = bth_stream_session_create(dev.bth_addr, sample_rate, eqmid);
                    if (active_session) {
                        if (bth_stream_session_start(active_session)) {
                            connected_addr = dev.bth_addr;
                            break;
                        } else {
                            bth_stream_session_destroy(active_session);
                            active_session = NULL;
                        }
                    }
                }
            }
        } else {
            /* Verify device remains connected */
            auto devices = bth_enumerate_audio_devices();
            bool still_connected = false;
            for (const auto& dev : devices) {
                if (dev.bth_addr == connected_addr && dev.is_connected) {
                    still_connected = true;
                    break;
                }
            }
            if (!still_connected) {
                bth_stream_session_destroy(active_session);
                active_session = NULL;
                connected_addr = 0;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (active_session) {
        bth_stream_session_destroy(active_session);
        active_session = NULL;
    }
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_IsRunning = false;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            SetEvent(g_ServiceStopEvent);
            break;
        default:
            break;
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    (void)argc; (void)argv;
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_IsRunning = true;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    std::thread worker(ServiceWorker);
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    g_IsRunning = false;
    if (worker.joinable()) worker.join();

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--service") {
        SERVICE_TABLE_ENTRY ServiceTable[] = {
            { (LPTSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
            { NULL, NULL }
        };
        StartServiceCtrlDispatcher(ServiceTable);
        return 0;
    }

    /* Standalone / interactive daemon mode */
    std::cout << "Sony LDAC Audio Driver Daemon running in interactive mode...\n";
    std::cout << "Press Ctrl+C to terminate.\n\n";

    g_IsRunning = true;
    ServiceWorker();
    return 0;
}
