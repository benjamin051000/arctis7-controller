#pragma once
#include <mutex>
#include <sys/time.h>
#include <cstdint>
#include <libusb-1.0/libusb.h>
#include <poll.h>
#include <set>
#include <unordered_map>
#include <vector>
#include "Packets.h"


class USBHandle {
	libusb_context *const ctx;
	const int interface;
	bool detachedKernelDriver = false;
	std::mutex active_transfers_mutex;
	std::set<libusb_transfer*> active_transfers;
	std::unordered_map<libusb_transfer*, void(*)(void)> callback_map;

	USBHandle(libusb_context* ctx, libusb_device *const dev, const int interface);
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
	void submit_control_transfer(
		Packet *const request,
		const int timeout,
		void(*callback)(void)	
	);

	void start_interrupt_listener(const unsigned char endpoint);
};


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

