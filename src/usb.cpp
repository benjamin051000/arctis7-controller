#include "usb.hpp"
#include <bits/types/struct_timeval.h>
#include <cstdint>
#include <libusb-1.0/libusb.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <vector>


USB::USB() {
	const auto err = libusb_init(&ctx);
	if (err != LIBUSB_SUCCESS) {
		throw libusb_error(err);
	}
}

USB::~USB() {
	puts("~USB");
	libusb_exit(ctx);
}

USBHandle USB::find_device(
	const uint16_t idVendor,
	const uint16_t idProduct,
	const uint8_t iManufacturer,
	const uint8_t iProduct
) const {

	libusb_device** devices = nullptr;

	const auto list_size = libusb_get_device_list(ctx, &devices);
	
	if (list_size < 0) {
		libusb_free_device_list(devices, true);
		throw libusb_error(list_size);
	}

	for (auto i = 0; i < list_size; i++) {
		libusb_device_descriptor desc;
		const auto err = libusb_get_device_descriptor(devices[i], &desc);
		if (err != LIBUSB_SUCCESS) {
			libusb_free_device_list(devices, true);
			throw libusb_error(err);
		}

		if(
			desc.idVendor == idVendor && 
			desc.idProduct == idProduct &&
			desc.iManufacturer == iManufacturer &&
			desc.iProduct == iProduct
		 ) {
			
			// First, allocate this one
			const auto cheat = 5; // TODO get this value from the descriptor.
			USBHandle handle(ctx, devices[i], cheat); 

			// Then, free the list.
			libusb_free_device_list(devices, true);

			// handle still has a ref to the underlying object.
			return handle;
		}

	}

	libusb_free_device_list(devices, true);
	throw libusb_error(LIBUSB_ERROR_NOT_FOUND);
}

std::vector<pollfd> USB::get_pollfds() {
	const libusb_pollfd** usb_pollfds = libusb_get_pollfds(ctx);
	
	std::vector<pollfd> pollfds;
	for(auto it = usb_pollfds; *it != nullptr; it++) {
		pollfds.emplace_back(pollfd{(*it)->fd, (*it)->events, 0});
	}

	libusb_free_pollfds(usb_pollfds);

	pollfds.emplace_back(pollfd{fileno(stdin), POLLIN, 0});

	return pollfds;
}

timeval USB::get_next_timeout() {
	timeval tv;
	libusb_get_next_timeout(ctx, &tv);
	return tv;
}

void USB::handle_events_timeout() {
	timeval zero{.tv_sec=0, .tv_usec=0};
	libusb_handle_events_timeout(ctx, &zero);
}
