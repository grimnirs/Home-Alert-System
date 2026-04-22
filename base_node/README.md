# Home Security Base Node — Zephyr Firmware

## Project Structure

```
base_node/
├── CMakeLists.txt              # Top-level build file
├── prj.conf                    # Zephyr project configuration
├── app.overlay                 # Devicetree overlay (UART + LED)
├── dts/
│   └── bindings/
│       └── custom,sensor-node.yaml   # Custom DT binding
├── drivers/
│   └── sensor_node/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       ├── sensor_node.c       # Driver core: UART reception + frame parsing + Sensor API
│       └── sensor_node.h       # Custom channel definitions + frame format constants
└── src/
    └── main.c                  # Application layer: read sensors + LED alerting
```

## How It Works

1. The **Sensor Node** periodically sends a **10-byte UART frame**:
   ```
   | 0xAA | Temp_H | Temp_L | Gas_H | Gas_L | Sound | Dist_H | Dist_L | Alarm | 0x55 |
   ```

2. The **Base Node driver** (sensor_node.c) receives bytes via UART interrupt and uses a state machine to synchronise on the `0xAA` start marker and `0x55` end marker.

3. The application layer uses the standard Zephyr Sensor API:
   - `sensor_sample_fetch()` — waits for and parses a complete frame
   - `sensor_channel_get()` — retrieves temperature / gas / sound / distance / alarm status

4. LED blink patterns are driven by the `alarm_status` bitfield:
   - bit 0 (motion): 1 blink
   - bit 1 (gas): 2 blinks
   - bit 2 (sound): 3 blinks

## Build & Flash

```bash
# Pico 2 (RP2350)
west build -b rpi_pico2/rp2350 base_node -- -DDTC_OVERLAY_FILE=app.overlay

# Original Pico (RP2040)
west build -b rpi_pico base_node -- -DDTC_OVERLAY_FILE=app.overlay

# Flash (hold BOOTSEL while plugging in via USB)
west flash
# Or manually copy build/zephyr/zephyr.uf2 to the Pico's USB mass storage
```

## Things You May Need to Change

1. **UART pins**: Verify that the `uart0` TX/RX pins in `app.overlay` match your wiring.
2. **Baud rate**: `current-speed = <9600>;` must match the Sensor Node.
3. **LED pin**: `GP25` is the onboard LED on the Pico. Change the GPIO number in the overlay if using an external LED.
4. **Temperature scaling**: The driver assumes the Sensor Node sends `temp × 100` (e.g. 2350 = 23.50 °C). If your Sensor Node uses a different scale, update the division in `sensor_node_channel_get()`.

## Sensor Node Requirements

When sending frames, the Sensor Node must ensure:
- Multi-byte fields are packed in **big-endian** order (high byte first)
- Temperature is `int16_t`; other fields are `uint16_t` / `uint8_t`
- `alarm_status` uses a bitfield: `BIT(0)=motion, BIT(1)=gas, BIT(2)=sound`
- Baud rate and 8N1 settings match the Base Node
