#include "NewHeadset.h"

#include <cstdio>

Headset::Headset(USBHandle&& handle) : handle(std::move(handle)) {
    this->handle.start_interrupt_listener(endpoint);
}

void Headset::set_blink_transmitter_led(bool enable) {
    BlinkTransmitterLED request(enable);
    handle.submit_control_transfer(
        &request, 0, []() { puts("Finished BlinkTransmitterLED request."); });
}

void Headset::set_inactivity_shutoff(uint8_t minutes) {
    InactivityShutoff request(minutes);
    handle.submit_control_transfer(
        &request, 0, []() { puts("Finished InactivityShutoff request."); });
}

void Headset::set_mic_sidetone(bool enabled,
                               MicSidetone::IntensityValues intensity) {
    if (!enabled) {
        intensity = MicSidetone::disabled;
    }
    MicSidetone request(enabled, intensity);
    handle.submit_control_transfer(
        &request, 0, []() { puts("Finished MicSidetone request."); });
}

void Headset::set_mic_volume(uint8_t volume) {
    if (volume > 100) {
        volume = 100;
    }
    MicVolume request(volume);
    handle.submit_control_transfer(
        &request, 0, []() { puts("Finished MicVolume request."); });
}

void Headset::set_connection_callback(std::function<void(bool)> callback) {
    connected_callback = callback;
}

void Headset::set_battery_callback(std::function<void(int)> callback) {
    battery_callback = callback;
}

bool Headset::get_connection() {
    Connection request;

    // TODO: setup a callback function to place the data somewhere

    handle.submit_control_transfer(
        &request, 0, []() { puts("Finished Connection request."); });

    return false;
}

uint8_t Headset::get_battery() {
    Battery request;

    // TODO: setup a callback function to place the data somewhere

    handle.submit_control_transfer(&request, 0,
                                   []() { puts("Finished Battery request."); });

    return 0;
}
