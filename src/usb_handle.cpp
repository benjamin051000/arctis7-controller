#include "usb_handle.hpp"

#include <cstdio>

USBHandle::USBHandle(libusb_context* ctx, libusb_device* const dev,
                     const int interface)
    : ctx(ctx), interface(interface) {
    auto err = libusb_open(dev, &handle);
    if (err != LIBUSB_SUCCESS) {
        throw libusb_error(err);
    }

    // Apparently detaching the USB device's kernel driver is a required step on
    // Linux.
    if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
        if (libusb_kernel_driver_active(handle, interface)) {
            err = libusb_detach_kernel_driver(handle, interface);
            if (err != LIBUSB_SUCCESS) {
                libusb_close(handle);
                throw libusb_error(err);
            }
            detachedKernelDriver = true;
        }
    }

    err = libusb_claim_interface(handle, interface);
    if (err != LIBUSB_SUCCESS) {
        if (detachedKernelDriver) {
            libusb_attach_kernel_driver(handle, interface);
        }
        libusb_close(handle);
        throw libusb_error(err);
    }
}

USBHandle::USBHandle(USBHandle&& other) noexcept
    : ctx(other.ctx), interface(other.interface), handle(other.handle) {
    other.handle = nullptr;
}

USBHandle::~USBHandle() {
    puts("~USBHandle");
    {
        std::lock_guard<std::mutex> lock(active_transfers_mutex);
        for (auto i : active_transfers) {
            libusb_cancel_transfer(i);
        }
    }

    while (true) {
        {
            std::lock_guard<std::mutex> lock(active_transfers_mutex);
            if (active_transfers.empty()) {
                break;
            }
        }

        if (libusb_handle_events_completed(ctx, nullptr) != LIBUSB_SUCCESS) {
            puts("Uh oh, something bad happened.");
        }
    }

    libusb_release_interface(handle, interface);
    puts("1");

    if (detachedKernelDriver) {
        puts("2");
        libusb_attach_kernel_driver(handle, interface);
        puts("3");
    }

    // BUG this line hangs.
    libusb_close(handle);
    puts("4");
}

void USBHandle::submit_control_transfer(Packet* const request,
                                        const unsigned int timeout,
                                        void (*callback)(void)) {
    libusb_transfer* transfer = libusb_alloc_transfer(0);
    if (transfer == nullptr) {
        // TODO what should we do here?
        throw libusb_error(LIBUSB_ERROR_OTHER);
    }

    {
        std::lock_guard<std::mutex> lock(active_transfers_mutex);
        active_transfers.insert(transfer);
    }
    callback_map[transfer] = callback;

    uint8_t* buffer = new uint8_t[sizeof(*request) + 8]();
    libusb_fill_control_setup(buffer,
                              (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS |
                               LIBUSB_RECIPIENT_INTERFACE),
                              LIBUSB_REQUEST_SET_CONFIGURATION,
                              0x0206,  // Not sure what this means
                              0x0005,  // Not sure what this means
                              31);
    libusb_fill_control_transfer(transfer, handle, buffer,
                                 USBHandle::control_transfer_handler, this,
                                 timeout);
    std::copy(reinterpret_cast<uint8_t*>(request),
              reinterpret_cast<uint8_t*>(request) + sizeof(*request),
              libusb_control_transfer_get_data(transfer));

    int ret = libusb_submit_transfer(transfer);
    if (ret != LIBUSB_SUCCESS) {
        throw libusb_error(ret);
    }
}

void USBHandle::control_transfer_handler(libusb_transfer* transfer) {
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        // In the event of transfer cancellation, the headset object could have
        // been cleaned up, so don't try to access its memory
        const auto handle = static_cast<USBHandle*>(transfer->user_data);

        {
            std::lock_guard<std::mutex> lock(handle->active_transfers_mutex);
            // TODO what if it isn't there? Does this throw? Or do nothing?
            handle->active_transfers.erase(transfer);
        }

        const auto callback = handle->callback_map[transfer];
        callback();

    } else if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        // TODO it's probably still in the map. How do we access it from here
        // and erase it? Or do you not need to? Why do we just throw here?
        throw libusb_transfer_status(transfer->status);
    }

    delete transfer->buffer;  // TODO what is in buffer?
    libusb_free_transfer(transfer);
}

void USBHandle::interrupt_transfer_handler(libusb_transfer* transfer) {
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED &&
        transfer->actual_length > 0) {
        const auto handle = static_cast<USBHandle*>(transfer->user_data);

        // TODO when isn't it sizeof(Packet)?
        // TODO Packet is coupled to Arctis. Make more generic?
        if (transfer->actual_length == sizeof(Packet)) {
            const auto callback = handle->callback_map[transfer];
            callback();
            // BEFORE...
            //
            // if (packet->command == Packet::battery) {
            //     if (headset->battery_callback != nullptr) {
            //         headset->battery_callback(reinterpret_cast<Battery*>(packet)->get_charge());
            //     }
            // } else if (packet->command == Packet::connection) {
            //     if (headset->connected_callback != nullptr) {
            //         headset->connected_callback(reinterpret_cast<Connection*>(packet)->is_connected());
            //     }
            // }
        }

        // Resumbit transfer to continue listening for interrupts
        const auto err = libusb_submit_transfer(transfer);
        if (err != LIBUSB_SUCCESS) {
            throw libusb_error(err);
        }
    } else if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
        delete transfer->buffer;
        libusb_free_transfer(transfer);
    } else {
        throw libusb_transfer_status(transfer->status);
    }
}

void USBHandle::start_interrupt_listener(const unsigned char endpoint) {
    // TODO if this is slow, do it in the constructor.
    const auto device = libusb_get_device(handle);

    const auto buffer_size = libusb_get_max_packet_size(device, endpoint) * 2;

    uint8_t* interrupt_buffer = new uint8_t[buffer_size];

    libusb_transfer* transfer = libusb_alloc_transfer(0);
    if (transfer == nullptr) {
        throw libusb_error(LIBUSB_ERROR_OTHER);
    }

    {
        std::lock_guard<std::mutex> lock(active_transfers_mutex);
        active_transfers.insert(transfer);
    }

    libusb_fill_interrupt_transfer(transfer, handle, endpoint, interrupt_buffer,
                                   buffer_size, interrupt_transfer_handler,
                                   this,
                                   0  // unlimited
    );

    const auto err = libusb_submit_transfer(transfer);
    if (err != LIBUSB_SUCCESS) {
        throw libusb_error(err);
    }
}
