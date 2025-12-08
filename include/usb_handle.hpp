#pragma once
#include <libusb-1.0/libusb.h>

#include <mutex>
#include <set>
#include <unordered_map>

#include "Packets.h"

class USBHandle {
    libusb_context* const ctx;
    const int interface;
    std::mutex active_transfers_mutex;
    std::set<libusb_transfer*> active_transfers;
    std::unordered_map<libusb_transfer*, void (*)(void)> callback_map;

    USBHandle(libusb_context* ctx, libusb_device* const dev,
              const int interface);
    friend class USB;

    /**
     * Dispatch back to callback fn
     */
    static void control_transfer_handler(libusb_transfer* transfer);

    static void interrupt_transfer_handler(libusb_transfer* transfer);

   public:
    libusb_device_handle* handle = nullptr;

    USBHandle(USBHandle&&) noexcept;

    ~USBHandle();

    /**
     * Allocate, fill out, and submit a transfer.
     */
    void submit_control_transfer(Packet* const request,
                                 const unsigned int timeout,
                                 void (*callback)(void));

    void start_interrupt_listener(const unsigned char endpoint);
};
