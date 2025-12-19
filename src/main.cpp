#include <libusb-1.0/libusb.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "NewHeadset.h"
#include "usb.hpp"

void sleep(const int duration_s) {
    std::this_thread::sleep_for(std::chrono::seconds(duration_s));
}

int main() {
    // for (auto i = 0; i < 10; i++) {
    USB usb;
    auto handle = usb.find_device(0x1038, 0x12ad, 4, 5);
    Headset headset(std::move(handle));
    // }

    return 0;
}
