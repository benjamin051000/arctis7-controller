#include <libusb-1.0/libusb.h>

#include "NewHeadset.h"
// #include <pulse/something.h>

#include <poll.h>
#include <stdio.h>

#include <iostream>

#include "usb.hpp"

int main() {
    // TODO: This error handling just exits when anything bad happens.
    //		 It should be a little more robust. (Get rid of try-throw-catch)
    try {
        USB usb;  // WARNING: This MUST outlive all USBHandle objects!
                  // TODO unless maybe this fn could return a reference, and USB
                  // manages them all? That might be the move actually
        auto handle = usb.find_device(0x1038, 0x12ad, 4, 5);

        // libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL,
        // LIBUSB_LOG_LEVEL_DEBUG);

        Headset headset(std::move(handle));

        auto fds = usb.get_pollfds();

        bool connected = false;
        int charge = 0;
        bool connected_updated = false;
        bool charge_updated = false;

        headset.set_battery_callback([&charge, &charge_updated](int soc) {
            charge = soc;
            charge_updated = true;
        });
        headset.set_connection_callback(
            [&connected, &connected_updated](bool con) {
                connected = con;
                connected_updated = true;
            });

        std::chrono::time_point start = std::chrono::steady_clock::now();
        std::chrono::duration poll_period = std::chrono::seconds(2);
        std::chrono::time_point poll_timeout = start + poll_period;
        int timeout_ms = 0;

        puts("c -> connection");
        puts("b -> battery");
        puts("q -> quit");
        puts("----------");
        bool should_exit = false;
        while (!should_exit) {
            // Calculate the next timeout (from libusb or our periodic polling)
            const auto libusb_period = usb.get_next_timeout();
            const auto now = std::chrono::steady_clock::now();
            const auto libusb_timeout = libusb_period + now;
            if (libusb_timeout < poll_timeout) {
                timeout_ms = static_cast<int>(libusb_period.count());
            } else {
                timeout_ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        poll_timeout - now)
                        .count());
            }
            if (timeout_ms < 0) {
                timeout_ms = 0;
            }

            int ready_fds = poll(fds.data(), fds.size(), timeout_ms);

            if (ready_fds > 0) {
                for (auto fd : fds) {
                    if (fd.fd == fileno(stdin) && fd.revents == POLLIN) {
                        std::string input;
                        getline(std::cin, input);

                        if (input == "q") {
                            should_exit = true;
                        } else if (input == "c") {
                            headset.get_connection();
                        } else if (input == "b") {
                            headset.get_battery();
                        }

                    } else if (fd.revents != 0) {
                        // BUG this is throwing
                        usb.handle_events_timeout();
                    }
                }
            } else if (ready_fds == 0) {  // timeout
                if (now > poll_timeout) {
                    poll_timeout += poll_period;
                    headset.get_battery();
                    headset.get_connection();
                }
                if (now > libusb_timeout) {
                    usb.handle_events_timeout();
                }
            }

            // Make use of any new data we may have received.
            if (charge_updated) {
                printf("Charge: %d\n", charge);
                charge_updated = false;
            }
            if (connected_updated) {
                printf("Connected %s\n", connected ? "true" : "false");
                connected_updated = false;
            }
        }  // end of while

        // Clean up any transfers that have been cancelled when Headset was
        // destroyed
        while (poll(fds.data(), fds.size(), 100)) {
            usb.handle_events_timeout();
        }

    }  // end of try
    catch (const libusb_error& e) {
        std::cerr << "Error: " << libusb_strerror(e) << std::endl;
        return 1;
    } catch (const libusb_transfer_status& e) {
        std::cerr << "Error: " << libusb_error_name(e) << std::endl;
        return 2;
    }

    return 0;
}
