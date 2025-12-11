#include "usb_handle.hpp"

#include <libusb-1.0/libusb.h>

#include <cstdint>
#include <cstdio>

void USBHandle::libusb_device_handle_deleter::operator()(
    libusb_device_handle* const handle) const noexcept {
    if (handle) {
        libusb_close(handle);
    }
}

USBHandle::USBHandle(libusb_context *const ctx, libusb_device* const dev,
                     const int interface)
    : ctx(ctx), interface(interface) {
    int err;

    {
        libusb_device_handle* temp_handle;
        err = libusb_open(dev, &temp_handle);
        if (err != LIBUSB_SUCCESS) {
            throw libusb_error(err);
        }
        handle.reset(temp_handle);
    }

    // Apparently detaching the USB device's kernel driver is a required step on
    // Linux.
    err = libusb_set_auto_detach_kernel_driver(handle.get(), true);
    if (err != LIBUSB_SUCCESS && err != LIBUSB_ERROR_NOT_SUPPORTED) {
        throw libusb_error(err);
    }

    err = libusb_claim_interface(handle.get(), this->interface);
    if (err != LIBUSB_SUCCESS) {
        throw libusb_error(err);
    }
}

USBHandle::USBHandle(USBHandle&& other) noexcept
    : ctx(other.ctx),
      interface(other.interface),
      active_transfers_mutex(),
      active_transfers(std::move(other.active_transfers)),
      callback_map(std::move(other.callback_map)),
      handle(std::move(other.handle)) {}

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

    libusb_release_interface(handle.get(), interface);
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
    const uint8_t bmRequestType =
        static_cast<uint8_t>(LIBUSB_ENDPOINT_OUT) |
        static_cast<uint8_t>(LIBUSB_REQUEST_TYPE_CLASS) |
        static_cast<uint8_t>(LIBUSB_RECIPIENT_INTERFACE);
    libusb_fill_control_setup(buffer, bmRequestType,
                              LIBUSB_REQUEST_SET_CONFIGURATION,
                              0x0206,  // Not sure what this means
                              0x0005,  // Not sure what this means
                              31);
    libusb_fill_control_transfer(transfer, handle.get(), buffer,
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
    puts("interrupt_transfer_handler()");
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
        puts("cancelling...");
        delete transfer->buffer;
        libusb_free_transfer(transfer);
    } else {
        throw libusb_transfer_status(transfer->status);
    }
    puts("interrupt_transfer_handler() done.");
}

void USBHandle::start_interrupt_listener(const unsigned char endpoint) {
    // TODO if this is slow, do it in the constructor.
    const auto device = libusb_get_device(handle.get());

    const auto buffer_size =
        libusb_get_max_packet_size(device, endpoint) * 2;  // TODO why *2?

    uint8_t* interrupt_buffer = new uint8_t[buffer_size];

    libusb_transfer* transfer = libusb_alloc_transfer(0);
    if (transfer == nullptr) {
        throw libusb_error(LIBUSB_ERROR_OTHER);
    }

    {
        std::lock_guard<std::mutex> lock(active_transfers_mutex);
        active_transfers.insert(transfer);
    }

    libusb_fill_interrupt_transfer(transfer, handle.get(), endpoint,
                                   interrupt_buffer, buffer_size,
                                   interrupt_transfer_handler, this,
                                   0  // unlimited
    );

    const auto err = libusb_submit_transfer(transfer);
    if (err != LIBUSB_SUCCESS) {
        throw libusb_error(err);
    }
}
