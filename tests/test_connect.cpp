#include <winsock2.h>
#include <ws2bth.h>
#include <iostream>

int main() {
    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_L2CAP);
    SOCKADDR_BTH sa = {0};
    sa.addressFamily = AF_BTH;
    sa.btAddr = 0x8099e7959343ULL;
    sa.port = 0x0019; // BT_PSM_AVDTP
    std::cout << "Connecting to 80:99:E7:95:93:43 PSM 0x0019...\n";
    int r = connect(s, (SOCKADDR*)&sa, sizeof(sa));
    if (r == 0) {
        std::cout << "SUCCESS: Connected!\n";
    } else {
        int err = WSAGetLastError();
        std::cout << "FAILED: Error code = " << err << "\n";
    }
    closesocket(s);
    WSACleanup();
    return 0;
}
