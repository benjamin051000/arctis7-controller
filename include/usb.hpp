#pragma once
#include <sys/time.h>
#include <cstdint>
#include <libusb-1.0/libusb.h>
#include <poll.h>
#include <vector>
#include "usb_handle.hpp"


class USB {
	libusb_context* ctx;

public:
	USB();
	~USB();

	USBHandle find_device(
		const uint16_t idVendor,
		const uint16_t idProduct,
		const uint8_t iManufacturer,
		const uint8_t iProduct
	) const;

	std::vector<pollfd> get_pollfds();
	timeval get_next_timeout();
	void handle_events_timeout();
};

