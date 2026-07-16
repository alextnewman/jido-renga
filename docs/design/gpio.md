<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# GPIO architecture

Jidō Renga provides a small Haiku-native GPIO service rather than importing
Linux gpiolib or pinctrl abstractions. The service separates firmware
connections, electrical policy, controller ownership, and event delivery:

```text
ACPI consumer
  -> gpio::Pin
       -> GPIO bus manager
            -> gpio::Controller
                 -> BayTrailController
```

The public headers are `<common/GpioTypes.h>` and `<common/Gpio.h>`.
`gpio::Pin` is a move-only RAII handle: acquisition exclusively claims one
line, destruction stops event delivery and releases it, and controller details
never escape into the consumer.

## Consumer contract

An ACPI consumer acquires a line by GPIO-resource index and pin index:

```cpp
gpio::module_info* gpio;
get_module(B_GPIO_MODULE_NAME, reinterpret_cast<module_info**>(&gpio));

gpio::Pin detect;
detect.AcquireAcpi(gpio, consumerNode, 0);
detect.Watch({gpio::Edge::Both, 200000}, HandleEdge, context);
```

GPIO-resource indices count only `GpioIo` and `GpioInt` descriptors. They do
not count I2C, memory, or ordinary interrupt resources that appear earlier in
the same `_CRS`.

The bus manager:

- resolves the resource source to a registered firmware controller;
- preserves ACPI access restrictions and active-low metadata;
- translates ACPI pull policy into the controller-neutral `gpio::Bias`;
- exclusively claims the line;
- reapplies the firmware bias when a watch configures the line as input;
- rejects writes to input-only or interrupt resources.

The module reference must outlive every `gpio::Pin` created from it. Providers
used by a BSP are marked `B_KEEP_DRIVER_LOADED`; consumers still release all
pins before dropping the GPIO module.

## Bay Trail controller

`byt_gpio` is installed at `add-ons/kernel/drivers/gpio/byt_gpio` and binds
ACPI HID `INT33FC` or `INT33B2` with UIDs 1 through 3. It is a controller
driver, not a child-enumerating bus: Haiku's generic ACPI probe searches the
`drivers` class but does not search a `busses/gpio` class.

| UID | Community | Pins | Shared GSI |
|---|---|---:|---:|
| 1 | SCORE | 102 | 49 |
| 2 | NCORE | 28 | 48 |
| 3 | SUS | 44 | 50 |

Logical GPIO numbers do not map linearly to register pads. The controller uses
the silicon community maps before applying the 16-byte pad stride. For Winky,
SCORE GPIO 14 maps to pad 40 and GPIO 15 maps to pad 84.

At initialization the controller follows the Bay Trail hardware contract:
firmware-owned direct-IRQ pins are left alone, stale ordinary GPIO trigger
configuration is disabled, and write-one-to-clear status words are drained
before the shared handler is installed. Direct-IRQ lines cannot be watched
through this API.

## Interrupt and debounce model

There is no periodic polling.

The shared-GSI ISR intersects status with an atomic watched-line mask,
acknowledges only those bits, queues the affected lines, and wakes one worker.
The worker waits until each line's debounce deadline, reads the stable level,
applies edge filtering, and invokes callbacks outside interrupt context.

`StopWatching()` clears the hardware trigger before removing the callback and
waits for an in-flight dispatch to finish. A callback may stop or release its
own pin; the worker completes its local dispatch after the callback returns.

## Winky jack lines

Coreboot describes two codec GPIO resources on `\_SB.GPSC`:

| Resource | SCORE pin | Polarity | Use |
|---|---:|---|---|
| 0 | 14 | active high | headphone present |
| 1 | 15 | active low | headset microphone present |

Linux's live IRQ domain reports them as `BYT-GPIO 14 hp` and
`BYT-GPIO 15 mic`, both delivered through SCORE GSI 49. The audio driver uses
both-edge subscriptions with 200 ms software debounce.

## Validation

Host policy tests cover community maps, register offsets, mux quirks, trigger
encoding, direction, and pull strength. Cross-build validation covers the GPIO
bus manager, `byt_gpio`, and its `byt_max98090` consumer. Winky hardware
validation confirms reliable physical edge delivery and speaker/headphone
switching on both jack insertion and removal.
