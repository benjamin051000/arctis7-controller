#ifndef __NEW_HEADSET_H__
#define __NEW_HEADSET_H__

#include <libusb-1.0/libusb.h>

#include <functional>

#include "Packets.h"
#include "usb.hpp"

class Headset {
   private:
    USBHandle handle;

    std::function<void(bool)> connected_callback = nullptr;
    std::function<void(int)> battery_callback = nullptr;

    /**
     * @brief Callback function for usb interrupts
     */
    static void handle_interrupt(libusb_transfer* transfer);

   public:
    static const unsigned char endpoint =
        3 | LIBUSB_ENDPOINT_IN;      // Arctis 7 headset HID endpoint
    static const int interface = 5;  // USB interface used by Arctis 7

    Headset(USBHandle&& handle);

    /**
     * @brief This function sets up the interrupt input transfers required to
     *         read data from the device. As such, it must be called before data
     *         is read to/from the device
     */
    // void start_interrupt_listener();

    /**
     * @brief Configures whether or not the transmitter LED should blink when
     *        the headset is off
     * @param enable   Blink when true, off when false
     */
    void set_blink_transmitter_led(bool enable);

    /**
     * @brief Configures how quickly the headset should turn off after a period
     *        of inactivity
     * @param minutes   The number of minutes before the headset turns off
     */
    void set_inactivity_shutoff(uint8_t minutes);

    /**
     * @brief Configures the sidetone of the headset
     * @param enabled   If sidetone is enabled
     * @param intensity The intensity of the sidetone feedback
     */
    void set_mic_sidetone(bool enabled, MicSidetone::IntensityValues intensity);

    /**
     * @brief Configures the volume of the microphone
     * @param volume    The percentage of the max microphone volume
     */
    void set_mic_volume(uint8_t volume);

    // TODO: Not yet supported
    // void sound_setting() {};

    /**
     * @brief Set the callback function used when a connection packet is
     * received
     * @param callback  The function to be called.
     *                  Input parameter bool is true when connected
     */
    void set_connection_callback(std::function<void(bool)> callback);

    /**
     * @brief Set the callback function used when a battery packet is received
     * @param callback  The function to be called.
     *                  Input parameter int is the battery status
     */
    void set_battery_callback(std::function<void(int)> callback);

    /**
     * @brief Is the headset connected to the receiver
     * @returns true if connected, false otherwise
     */
    bool get_connection();

    /**
     * @brief Gets the status of the battery
     * @returns The battery state of charge as a percentage
     */
    uint8_t get_battery();
};

#endif  // __NEW_HEADSET_H__
