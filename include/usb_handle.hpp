#pragma once
#include <libusb-1.0/libusb.h>

#include <memory>
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

    // Allow USB to call the private constructor.
    friend class USB;

    /**
     * Dispatch back to callback fn
     */
    static void control_transfer_handler(libusb_transfer* transfer);

    static void interrupt_transfer_handler(libusb_transfer* transfer);

    struct libusb_device_handle_deleter {
        void operator()(libusb_device_handle* const handle) const noexcept;
    };

   public:
    std::unique_ptr<libusb_device_handle, libusb_device_handle_deleter> handle;

    // TODO can we get rid of this?
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
