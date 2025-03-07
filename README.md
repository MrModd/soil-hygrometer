# Soil Hygrometer
Zigbee soil hygrometer with an ESP32-C6

This is an Arduino IDE sketch.

## Hardware requirements

* [Seed Studio XIAO-ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
* Soil Moisture Sensor Hygrometer Module V1.2
* A lithium ion battery

The hygrometer sensor is wired to the analog port A0.
A resistive divider is connected between the two poles of the battery.
The middle point is then connected to the analog port A5 to measure
the voltage og the battery.

## Software tweaks

The hygrometer sensor measures the variation of the reactance between
the plates of the capacitor. You should review the values read when
the sensor is dry and when it's immerged in water. The values are defined
as the variables `air_value` and `water_value`.

The Arduino IDE should be configured with these parameters:
* Board: XIAO_ESP32C6
* Erase All Flash Before Sketch Update: Enabled
* Partition Scheme: Zigbee 4MB with spiff
* Zigbee Mode: Zigbee ED (end device)
