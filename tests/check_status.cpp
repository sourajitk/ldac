#include <windows.h>
#include <bluetoothapis.h>
#include <iostream>
#include <iomanip>

#pragma comment(lib, "bthprops.lib")

int main() {
    BLUETOOTH_DEVICE_SEARCH_PARAMS sp = { sizeof(sp) };
    sp.fReturnAuthenticated = TRUE;
    sp.fReturnRemembered = TRUE;
    sp.fReturnUnknown = TRUE;
    sp.fReturnConnected = TRUE;

    BLUETOOTH_DEVICE_INFO di = { sizeof(di) };
    HANDLE hRadio = NULL;
    BLUETOOTH_FIND_RADIO_PARAMS rp = { sizeof(rp) };
    HBLUETOOTH_RADIO_FIND hrf = BluetoothFindFirstRadio(&rp, &hRadio);
    if (!hrf) {
        std::cout << "No Bluetooth radio found!\n";
        return 1;
    }
    sp.hRadio = hRadio;

    HBLUETOOTH_DEVICE_FIND hdf = BluetoothFindFirstDevice(&sp, &di);
    if (hdf) {
        do {
            std::wcout << L"Device: " << di.szName << L"\n";
            std::cout << "  Address: " << std::hex << di.Address.ullLong << std::dec << "\n";
            std::cout << "  Connected: " << di.fConnected << "\n";
            std::cout << "  Remembered: " << di.fRemembered << "\n";
            std::cout << "  Authenticated: " << di.fAuthenticated << "\n";
        } while (BluetoothFindNextDevice(hdf, &di));
        BluetoothFindDeviceClose(hdf);
    }
    CloseHandle(hRadio);
    BluetoothFindRadioClose(hrf);
    return 0;
}
