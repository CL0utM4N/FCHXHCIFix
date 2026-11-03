//
//  FCHXHCIFix.cpp
//
//  Makes USB usable on AMD FCH xHCI controllers whose interrupts never reach macOS.
//
//  On the ASUS VivoBook 15 D509BA (A9-9425 Stoney Ridge), the FCH xHCI (1022:7914) completes transfers but none of its
//  interrupts arrive, with plain MSI and with MSI-X alike, although the controller sends them (IMAN.IP keeps clearing,
//  as MSI does on send) and the GPU's MSIs to the same address do arrive. AppleUSBXHCI then only services the
//  controller from its 2 s fallback timer, so every transfer waits up to 2 s: USB storage crawls at ~130 KB/s and
//  input devices lag. Signalling only when USBSTS.EINT was set did not help (the controller raises EINT just as
//  rarely); signalling continuously does.
//
//  This driver attaches to the controller in its own IOMatchCategory, next to AppleUSBXHCIPCI. A thread waits for
//  AppleUSBXHCIPCI, finds its interrupt source (the IOFilterInterruptEventSource on the driver's work loop whose
//  provider is the controller), and signals it every poll (~2.5 ms), which runs AppleUSBXHCI's interrupt handler on its
//  work loop exactly as a delivered interrupt would; the handler returns quickly when the event rings are empty.
//  USBSTS.EINT is cleared (RW1C) when seen. The state is published in the controller's "FCHXHCIFix-state" property.
//
//  Sleep: AppleUSBXHCIPCI takes its interrupt source off the work loop around sleep and adds a new one on wake. The
//  old source, still retained here, then has no work loop, and signalling it dereferenced null (panic in
//  IOFilterInterruptEventSource::signalInterrupt after a wake). So the signalling runs inside the work loop's gate,
//  only for sources still on its event chain, and the sources are collected again when they are gone.
//
//  Boot-args: -fchxhcioff      do nothing
//             fchxhcimode=1    signal only when USBSTS.EINT is set (diagnosis; doesn't fix the D509BA)
//
//  It needs no hooks: everything goes through IOKit's public interfaces, except the work loop's event chain, which is
//  read through the IOWorkLoop / IOEventSource layout from the SDK headers.
//
//  FCHHubSleep, at the end of this file: the webcam on the FCH's internal USB 2 hub waking the machine from S3.
//

#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include <string.h>

class FCHXHCIFix : public IOService {
	OSDeclareDefaultStructors(FCHXHCIFix)

public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;

private:
	static void forwardThread(void *arg, wait_result_t);
	static IOReturn collectSources(OSObject *owner, void *loop, void *, void *, void *);
	static IOReturn signalSources(OSObject *owner, void *loop, void *signalled, void *, void *);
	static IOReturn listSources(OSObject *owner, void *loop, void *, void *, void *);
	void publish(const char *what);
	void fail(const char *what);

	IOPCIDevice *pci {nullptr};
	IOFilterInterruptEventSource *sources[4] {};
	uint32_t sourceCount {0}, forwards {0}, polls {0}, mode {2}, recollects {0};
	volatile bool stopping {false};
};

OSDefineMetaClassAndStructors(FCHXHCIFix, IOService)

namespace {
	// The event chain is protected; these read it through the kernel's own layout.
	struct WorkLoopPeek : IOWorkLoop {
		static IOEventSource *head(IOWorkLoop *loop) { return static_cast<WorkLoopPeek *>(loop)->eventChain; }
	};
	struct EventSourcePeek : IOEventSource {
		static IOEventSource *next(IOEventSource *source) { return static_cast<EventSourcePeek *>(source)->eventChainNext; }
	};
}

bool FCHXHCIFix::start(IOService *provider) {
	char arg[8];
	if (PE_parse_boot_argn("-fchxhcioff", arg, sizeof(arg)))
		return false;
	pci = OSDynamicCast(IOPCIDevice, provider);
	if (!pci || !IOService::start(provider))
		return false;
	if (!PE_parse_boot_argn("fchxhcimode", &mode, sizeof(mode)) || !mode)
		mode = 2;
	pci->retain();
	retain();  // held by the thread
	thread_t thread;
	if (kernel_thread_start(forwardThread, this, &thread) != KERN_SUCCESS) {
		release();
		publish("thread FAILED");
		return true;
	}
	thread_deallocate(thread);
	publish("waiting for AppleUSBXHCIPCI");
	return true;
}

void FCHXHCIFix::stop(IOService *provider) {
	stopping = true;
	IOService::stop(provider);
}

void FCHXHCIFix::publish(const char *what) {
	char state[160];
	snprintf(state, sizeof(state), "%u sources, %u forwards, %u polls, %u re-collected; %s", sourceCount,
		__atomic_load_n(&forwards, __ATOMIC_RELAXED), __atomic_load_n(&polls, __ATOMIC_RELAXED), recollects, what);
	pci->setProperty("FCHXHCIFix-state", state);
}

// Why it isn't forwarding, on the verbose screen too (the property can't be read during an install).
void FCHXHCIFix::fail(const char *what) {
	publish(what);
	IOLog("FCHXHCIFix: not forwarding: %s\n", what);
}

// Collect the driver's filter interrupt sources on the controller, inside the work loop's gate.
IOReturn FCHXHCIFix::collectSources(OSObject *owner, void *arg, void *, void *, void *) {
	auto *self = static_cast<FCHXHCIFix *>(owner);
	auto *loop = static_cast<IOWorkLoop *>(arg);
	for (auto *source = WorkLoopPeek::head(loop); source && self->sourceCount < sizeof(self->sources) / sizeof(self->sources[0]);
		 source = EventSourcePeek::next(source)) {
		auto *filter = OSDynamicCast(IOFilterInterruptEventSource, source);
		if (filter && filter->getProvider() == self->pci) {
			filter->retain();
			self->sources[self->sourceCount++] = filter;
		}
	}
	return kIOReturnSuccess;
}

// The event sources on the work loop, for the log when none could be used.
IOReturn FCHXHCIFix::listSources(OSObject *owner, void *arg, void *, void *, void *) {
	auto *self = static_cast<FCHXHCIFix *>(owner);
	auto *loop = static_cast<IOWorkLoop *>(arg);
	for (auto *source = WorkLoopPeek::head(loop); source; source = EventSourcePeek::next(source)) {
		auto *interrupt = OSDynamicCast(IOInterruptEventSource, source);
		IOLog("FCHXHCIFix: work loop source %s%s\n", source->getMetaClass()->getClassName(),
			interrupt ? (interrupt->getProvider() == self->pci ? " (on the controller)" : " (other provider)") : "");
	}
	return kIOReturnSuccess;
}

// Inside the work loop's gate: drop the sources no longer on the loop's event chain (removed for sleep), collect the
// current ones when none are left, and signal the enabled ones. *signalled = how many were signalled.
IOReturn FCHXHCIFix::signalSources(OSObject *owner, void *arg, void *signalled, void *, void *) {
	auto *self = static_cast<FCHXHCIFix *>(owner);
	auto *loop = static_cast<IOWorkLoop *>(arg);
	uint32_t kept = 0, count = 0;
	for (uint32_t i = 0; i < self->sourceCount; i++) {
		auto *source = self->sources[i];
		bool onLoop = false;
		for (auto *e = WorkLoopPeek::head(loop); e && !onLoop; e = EventSourcePeek::next(e))
			onLoop = e == source;
		if (onLoop)
			self->sources[kept++] = source;
		else
			source->release();
	}
	for (uint32_t i = kept; i < self->sourceCount; i++)
		self->sources[i] = nullptr;
	self->sourceCount = kept;
	if (!kept) {
		collectSources(owner, loop, nullptr, nullptr, nullptr);
		if (self->sourceCount)
			self->recollects++;
	}
	for (uint32_t i = 0; i < self->sourceCount; i++) {
		auto *source = self->sources[i];
		if (source->getWorkLoop() == loop && source->isEnabled()) {
			source->signalInterrupt();
			count++;
		}
	}
	*static_cast<uint32_t *>(signalled) = count;
	return kIOReturnSuccess;
}

void FCHXHCIFix::forwardThread(void *arg, wait_result_t) {
	auto *self = static_cast<FCHXHCIFix *>(arg);
	IOService *driver = nullptr;
	IOWorkLoop *loop = nullptr;
	IOMemoryMap *map = nullptr;

	// AppleUSBXHCIPCI attaches to the same controller in the default category; give it up to 5 min.
	for (int i = 0; i < 3000 && !driver && !self->stopping; i++) {
		if (auto *clients = self->pci->getClientIterator()) {
			while (auto *client = OSDynamicCast(IOService, clients->getNextObject())) {
				if (!strcmp(client->getMetaClass()->getClassName(), "AppleUSBXHCIPCI")) {
					driver = client;
					driver->retain();
					break;
				}
			}
			clients->release();
		}
		if (!driver)
			IOSleep(100);
	}
	if (!driver) {
		self->fail("AppleUSBXHCIPCI never attached");
		goto done;
	}
	loop = driver->getWorkLoop();
	if (loop) {
		// The source is added during start(), or later on some machines (Dell Inspiron 3475: no forwarding, and none of
		// these states shown): keep looking for as long as for the driver.
		for (int i = 0; i < 3000 && !self->sourceCount && !self->stopping; i++) {
			loop->runAction(collectSources, self, loop);
			if (!self->sourceCount)
				IOSleep(100);
		}
	}
	if (!self->sourceCount) {
		// What is on the loop instead (class, and whether its provider is the controller).
		if (loop)
			loop->runAction(listSources, self, loop);
		self->fail("no interrupt source on AppleUSBXHCIPCI's work loop");
		goto done;
	}
	map = self->pci->mapDeviceMemoryWithIndex(0);
	if (!map) {
		self->fail("BAR0 map FAILED");
		goto done;
	}
	IOLog("FCHXHCIFix: forwarding interrupts to AppleUSBXHCIPCI (%u sources, mode %u)\n", self->sourceCount, self->mode);
	{
		auto *base = reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());
		uint64_t length = map->getLength();
		auto r32 = [&](uint32_t off) { return off + 4 <= length ? *reinterpret_cast<volatile uint32_t *>(base + off) : ~0u; };
		while (!self->stopping) {
			uint32_t cap = r32(0);
			if (cap == ~0u) {  // powered down (D3) or gone
				IOSleep(10);
				continue;
			}
			uint32_t usbsts = (cap & 0xFF) + 4;
			uint32_t sts = r32(usbsts);
			uint32_t polls = __atomic_add_fetch(&self->polls, 1, __ATOMIC_RELAXED);
			bool eint = sts != ~0u && (sts & 8);
			if (eint)
				*reinterpret_cast<volatile uint32_t *>(base + usbsts) = 8;
			if (eint || self->mode >= 2) {
				uint32_t signalled = 0;
				loop->runAction(signalSources, self, loop, &signalled);
				if (signalled)
					__atomic_fetch_add(&self->forwards, 1, __ATOMIC_RELAXED);
			}
			if (polls % 1000 == 1)
				self->publish(self->mode >= 2 ? "running, mode 2 (every poll)" : "running, mode 1 (on EINT)");
			IOSleep(1);
		}
	}

done:
	if (map)
		map->release();
	for (uint32_t i = 0; i < self->sourceCount; i++)
		self->sources[i]->release();
	self->sourceCount = 0;
	if (driver)
		driver->release();
	self->pci->release();
	self->release();
}

//
//  FCHHubSleep: the webcam behind the FCH's internal USB 2 hub wakes the machine from S3.
//
//  The FCH's EHCI controller (EHC1) has an internal USB 2 hub on root port 1 (AMD 0438:7900, GHUB in the DSDT). On the
//  Dell Inspiron 3475 its four ports carry the USB 2 ports (keyboard, mouse), the Bluetooth module and the webcam; on
//  the ASUS VivoBook 15 D509BA the webcam and Bluetooth. The webcam's power rail is off in S3, so the camera drops off
//  the hub as the machine goes to sleep; the suspended hub reports that port change as a remote wake, and EHC1 wakes
//  the machine at once into a dark wake, every sleep (Dell, Tahoe: the webcam's port 1 -> 7 connects and the hub's
//  root port 0 -> 6 remote wakes over 6 sleeps; keyboard, mouse and Bluetooth unchanged). Turning EHC1's wake off
//  (_PRW) stops it but also stops the keyboard waking the machine and USB 2 devices being seen when plugged in.
//
//  This attaches to the hub's IOUSBHostDevice in its own IOMatchCategory, next to AppleUSB20Hub. Before sleep it
//  switches off power to the hub ports whose device has a video-class (0x0E) interface (ClearPortFeature PORT_POWER), so
//  nothing changes on the hub during S3, and switches them on again on wake, when the camera enumerates as it does
//  after every wake anyway. The hub stays armed for wake: keyboards and mice on its ports still wake the machine. Only
//  when the hub switches port power per port (wHubCharacteristics bits 1:0 = 01); a hub that switches all ports together
//  is left alone. The requests go to the hub's default endpoint on behalf of AppleUSB20Hub's session (the device allows
//  one client session; its work loop serialises the requests with the hub driver's own).
//  State in the hub's "FCHXHCIFix-sleep" property and the kernel log ("FCHXHCIFix: hub ...").
//
//  Boot-arg: -fchcamwake   leave the webcam's port powered during sleep
//
class FCHHubSleep : public IOService {
	OSDeclareDefaultStructors(FCHHubSleep)

public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;

private:
	static IOReturn powerMessage(void *target, void *, UInt32 type, IOService *, void *, vm_size_t);
	IOService *hubDriver();
	IOReturn hubRequest(uint8_t type, uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t length);
	void checkHub();
	void cameraPortsOff();
	void portsOn(const char *why);
	void publish(const char *what);

	IOUSBHostDevice *hub {nullptr};
	IONotifier *notifier {nullptr};
	int perPort {-1};  // -1 not read yet, 0 ganged or no switching, 1 per-port power switching
	uint8_t ports {0};
	uint32_t offMask {0}, sleeps {0}, offs {0};
};

OSDefineMetaClassAndStructors(FCHHubSleep, IOService)

bool FCHHubSleep::start(IOService *provider) {
	char arg[8];
	if (PE_parse_boot_argn("-fchxhcioff", arg, sizeof(arg)) || PE_parse_boot_argn("-fchcamwake", arg, sizeof(arg)))
		return false;
	hub = OSDynamicCast(IOUSBHostDevice, provider);
	if (!hub || !IOService::start(provider))
		return false;
	hub->retain();
	if (auto *root = getPMRootDomain())
		notifier = root->registerInterest(gIOGeneralInterest, powerMessage, this);
	publish(notifier ? "waiting for sleep" : "no sleep notifications");
	return true;
}

void FCHHubSleep::stop(IOService *provider) {
	if (notifier) {
		notifier->remove();
		notifier = nullptr;
	}
	if (hub) {
		hub->release();
		hub = nullptr;
	}
	IOService::stop(provider);
}

void FCHHubSleep::publish(const char *what) {
	char state[200];
	snprintf(state, sizeof(state), "%u ports, per-port power %s, %u sleeps, %u camera ports switched off; %s", ports,
		perPort < 0 ? "unknown" : perPort ? "yes" : "no", sleeps, offs, what);
	hub->setProperty("FCHXHCIFix-sleep", state);
}

// The hub driver holding the device's session (AppleUSB20Hub).
IOService *FCHHubSleep::hubDriver() {
	IOService *found = nullptr;
	if (auto *clients = hub->getClientIterator()) {
		while (auto *client = OSDynamicCast(IOService, clients->getNextObject())) {
			if (client != this && hub->isOpen(client)) {
				found = client;
				break;
			}
		}
		clients->release();
	}
	return found;
}

IOReturn FCHHubSleep::hubRequest(uint8_t type, uint8_t request, uint16_t value, uint16_t index, void *data,
	uint16_t length) {
	auto *driver = hubDriver();
	if (!driver)
		return kIOReturnNotOpen;
	StandardUSB::DeviceRequest req {};
	req.bmRequestType = type;
	req.bRequest = request;
	req.wValue = value;
	req.wIndex = index;
	req.wLength = length;
	uint32_t done = 0;
	return hub->deviceRequest(driver, req, data, done, 1000);
}

// GET_DESCRIPTOR (hub, 0x29): bNbrPorts, wHubCharacteristics (bits 1:0 = 01: per-port power switching).
void FCHHubSleep::checkHub() {
	uint8_t desc[9] {};
	IOReturn ret = hubRequest(0xA0, 6, 0x2900, 0, desc, sizeof(desc));
	if (ret != kIOReturnSuccess) {
		IOLog("FCHXHCIFix: hub descriptor read failed (%x)\n", ret);
		return;
	}
	ports = desc[2];
	uint16_t chars = static_cast<uint16_t>(desc[3] | (desc[4] << 8));
	perPort = (chars & 3) == 1;
	IOLog("FCHXHCIFix: hub %u ports, wHubCharacteristics %04x: %s\n", ports, chars,
		perPort ? "per-port power, the webcam's port goes off during sleep" : "no per-port power, left alone");
}

void FCHHubSleep::cameraPortsOff() {
	auto *driver = hubDriver();
	auto *portIter = driver ? driver->getChildIterator(gIOServicePlane) : nullptr;
	if (!portIter)
		return;
	while (auto *port = OSDynamicCast(IOService, portIter->getNextObject())) {
		auto *num = OSDynamicCast(OSData, port->getProperty("port"));
		if (!num || num->getLength() < 1)
			continue;
		uint8_t n = *static_cast<const uint8_t *>(num->getBytesNoCopy());
		bool camera = false;
		if (auto *devIter = port->getChildIterator(gIOServicePlane)) {
			while (auto *dev = OSDynamicCast(IOService, devIter->getNextObject())) {
				if (auto *ifIter = dev->getChildIterator(gIOServicePlane)) {
					while (auto *intf = OSDynamicCast(IOService, ifIter->getNextObject())) {
						auto *cls = OSDynamicCast(OSNumber, intf->getProperty("bInterfaceClass"));
						camera |= cls && cls->unsigned8BitValue() == 0x0E;
					}
					ifIter->release();
				}
			}
			devIter->release();
		}
		if (!camera || n < 1 || n > 31)
			continue;
		IOReturn ret = hubRequest(0x23, 1, 8, n, nullptr, 0);  // ClearPortFeature(PORT_POWER)
		IOLog("FCHXHCIFix: sleep: webcam on hub port %u, power off: %x\n", n, ret);
		if (ret == kIOReturnSuccess) {
			offMask |= 1U << n;
			offs++;
		}
	}
	portIter->release();
}

void FCHHubSleep::portsOn(const char *why) {
	for (uint8_t n = 1; n < 32 && offMask; n++) {
		if (!(offMask & (1U << n)))
			continue;
		IOReturn ret = hubRequest(0x23, 3, 8, n, nullptr, 0);  // SetPortFeature(PORT_POWER)
		uint8_t status[4] {};
		hubRequest(0xA3, 0, 0, n, status, sizeof(status));     // GetPortStatus: bit 8 PORT_POWER
		IOLog("FCHXHCIFix: %s: hub port %u power on: %x, status %02x%02x\n", why, n, ret, status[1], status[0]);
		offMask &= ~(1U << n);
	}
}

IOReturn FCHHubSleep::powerMessage(void *target, void *, UInt32 type, IOService *, void *, vm_size_t) {
	auto *self = static_cast<FCHHubSleep *>(target);
	switch (type) {
		case kIOMessageSystemWillSleep:
			self->sleeps++;
			if (self->perPort < 0)
				self->checkHub();
			if (self->perPort == 1)
				self->cameraPortsOff();
			self->publish("asleep");
			break;
		case kIOMessageSystemWillNotSleep:
			self->portsOn("sleep cancelled");
			self->publish("awake");
			break;
		case kIOMessageSystemHasPoweredOn:
			self->portsOn("wake");
			self->publish("awake");
			break;
		default:
			break;
	}
	return kIOReturnSuccess;
}
