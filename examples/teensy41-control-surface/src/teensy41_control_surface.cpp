// midi2cpp / teensy41-control-surface: Arduino IDE backend implementation

#include "teensy41_control_surface.h"
#include <usb_midi2.h>

namespace teensy41 {

void Backend::begin(uint16_t jrHeartbeatMs)
{
	dev_.setWriteFn([](const uint32_t *words, size_t count) {
		usbMIDI2.write(words, (uint8_t)count);
	});
	dev_.setNowFn([]() -> uint32_t { return millis(); });
	dev_.setMounted(true); // Teensy native USB is always-on once enumerated
	dev_.begin();
	if (jrHeartbeatMs > 0) {
		dev_.enableJRHeartbeat(jrHeartbeatMs);
	}
}

void Backend::task()
{
	dev_.setAltSetting(usbMIDI2.altSetting());
	uint32_t words[4];
	uint8_t  count;
	while (usbMIDI2.read(words, &count)) {
		dev_.feedRx(words, count);
	}
	dev_.task();
}

} // namespace teensy41
