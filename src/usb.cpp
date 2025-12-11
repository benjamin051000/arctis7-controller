#include "usb.hpp"

#include <bits/types/struct_timeval.h>
#include <libusb-1.0/libusb.h>
#include <sys/poll.h>
#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "TimevalDurationCast.h"

void USB::libusb_context_deleter::operator()(
    libusb_context* const ctx) const noexcept {
    // NOTE: ctx is generally non-null. However, it's possible
    // to manually set it to nullptr and then manually call the deleter.
    // So, check that it's non-null here just to be extra safe.
    if (ctx) {
        libusb_exit(ctx);
    }
}

USB::USB() {
    libusb_context* temp;
    const auto err = libusb_init(&temp);
    if (err != LIBUSB_SUCCESS) {
        throw libusb_error(err);
    }
    ctx.reset(temp);

    libusb_set_option(ctx.get(), LIBUSB_OPTION_LOG_LEVEL,
                      LIBUSB_LOG_LEVEL_DEBUG);
}

USBHandle USB::find_device(const uint16_t idVendor, const uint16_t idProduct,
                           const uint8_t iManufacturer,
                           const uint8_t iProduct) const {
    libusb_device** devices = nullptr;

    const auto list_size = libusb_get_device_list(ctx.get(), &devices);

    if (list_size < 0) {
        libusb_free_device_list(devices, true);
        throw libusb_error(list_size);
    }

    for (auto i = 0; i < list_size; i++) {
        libusb_device_descriptor desc;
        [[maybe_unused]] const auto err =
            libusb_get_device_descriptor(devices[i], &desc);

        // Note since libusb-1.0.16, LIBUSBX_API_VERSION >= 0x01000102, this
        // function always succeeds.
#if LIBUSBX_API_VERSION < 0x01000102
        if (err != LIBUSB_SUCCESS) {
            libusb_free_device_list(devices, true);
            throw libusb_error(err);
        }
#endif

        if (desc.idVendor == idVendor && desc.idProduct == idProduct &&
            desc.iManufacturer == iManufacturer && desc.iProduct == iProduct) {
            // First, allocate this one
            const auto cheat = 5;  // TODO get this value from the descriptor.
            try {
                USBHandle handle(ctx.get(), devices[i], cheat);

                // unref_devices is true because constructing the
                // USBHandle adds a refcount to the device we want.
                libusb_free_device_list(devices, true);

                return handle;

            } catch (const libusb_error& error) {
                libusb_free_device_list(devices, true);
                throw error;
            }
        }
    }

    libusb_free_device_list(devices, true);
    throw libusb_error(LIBUSB_ERROR_NOT_FOUND);
}

std::vector<pollfd> USB::get_pollfds() {
    const libusb_pollfd** usb_pollfds = libusb_get_pollfds(ctx.get());

    std::vector<pollfd> pollfds;
    for (auto it = usb_pollfds; *it != nullptr; it++) {
        pollfds.emplace_back(pollfd{(*it)->fd, (*it)->events, 0});
    }

    libusb_free_pollfds(usb_pollfds);

    pollfds.emplace_back(pollfd{fileno(stdin), POLLIN, 0});

    return pollfds;
}

std::chrono::milliseconds USB::get_next_timeout() {
    timeval tv;
    libusb_get_next_timeout(ctx.get(), &tv);
    auto period = std::chrono::duration_cast<std::chrono::milliseconds>(tv);
    return period;
}

void USB::handle_events_timeout() {
    timeval zero{.tv_sec = 0, .tv_usec = 0};
    libusb_handle_events_timeout(ctx.get(), &zero);
}

void USB::print_packet(const Packet* const packet) noexcept {
    const auto buf = reinterpret_cast<const uint8_t*>(packet);
    for (long unsigned i = 0; i < sizeof(*packet); i++) {
        printf("%02x ", buf[i]);
    }
    printf("\n");
}
