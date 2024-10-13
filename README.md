# README

## Overview

The **MonadCount Sniffer** is an ESP32-based application designed to capture Wi-Fi packets and Channel State Information
(CSI) data, write the captured data to an SD card, and broadcast BLE advertisements using NimBLE. The application 
operates in two main phases:

1. **Management Phase**: Connects to a specified Wi-Fi network to synchronize time using SNTP.
2. **Sniffer Phase**: Initializes Wi-Fi in promiscuous mode to capture packets and CSI data, writes the data to the SD card, and starts BLE advertising.


## Components

### 1. **`main` Application**

- **File**: `main.c`
- **Description**: Entry point of the application. Initializes shared resources, NVS, Bluetooth, and manages the transition between the management and sniffer phases.

### 2. **`bluetooth` Component**

- **Purpose**: Handles BLE advertisement broadcasting using NimBLE.
- **Files**:
    - `bluetooth.c`: Contains the implementation for BLE advertisement initialization and broadcasting.
    - `include/bluetooth.h`: Header file with function declarations.
- **Key Functions**:
    - `init_advertisement_data()`: Sets up the BLE advertisement data fields.
    - `start_advertising()`: Configures and starts BLE advertising.
    - `bleprph_on_reset()`: Callback for BLE host reset events.
    - `bleprph_on_sync()`: Callback for BLE host synchronization.
    - `bleprph_host_task()`: Main task for the NimBLE host.

### 3. **`sniffer` Component**

- **Purpose**: Captures Wi-Fi packets and CSI data, writes data to the SD card, and manages channel hopping.
- **Files**:
    - `sniffer.c`: Contains functions for Wi-Fi initialization in promiscuous mode, packet and CSI data callbacks, SD card writing tasks, and channel hopping.
    - `include/sniffer.h`: Header file with function declarations and data structures.
- **Key Functions**:
    - `sniffer_wifi_init()`: Initializes Wi-Fi for packet and CSI capturing.
    - `sniffer_wifi_deinit()`: Deinitializes Wi-Fi and cleans up resources.
    - `wifi_promiscuous_rx_cb()`: Callback for received Wi-Fi packets in promiscuous mode.
    - `wifi_csi_rx_cb()`: Callback for received CSI data.
    - `sdcard_writer_task()`: Task that writes captured packet data to the SD card.
    - `csi_writer_task()`: Task that writes captured CSI data to the SD card.
    - `channel_hop_task()`: Task that periodically changes the Wi-Fi channel.

### 4. **`management` Component**

- **Purpose**: Manages Wi-Fi connectivity for time synchronization during the management phase.
- **Files**:
    - `management.c`: Contains functions to initialize Wi-Fi in station mode, connect to an access point, synchronize time using SNTP, and deinitialize Wi-Fi after synchronization.
    - `include/management.h`: Header file with function declarations.
- **Key Functions**:
    - `management_wifi_init()`: Initializes Wi-Fi for the management phase.
    - `management_obtain_time()`: Synchronizes the system time using SNTP.
    - `management_wifi_deinit()`: Deinitializes Wi-Fi after time synchronization.

### 5. **`shared` Component**

- **Purpose**: Provides shared resources and definitions used across different components.
- **Files**:
    - `shared.c`: Contains shared variables and functions, such as mutex initialization.
    - `include/shared.h`: Header file with shared definitions and external variable declarations.
- **Key Variables**:
    - `SemaphoreHandle_t data_mutex`: Mutex used to protect shared data.

## Build and Flash Instructions

### Prerequisites

- **Hardware**:
    - ESP32 development board.
    - SD card connected via SPI interface.
- **Software**:
    - ESP-IDF version 5.x installed.
    - Necessary environment setup for ESP-IDF development.

## Application Workflow

1. **Initialization**:

- The application starts and initializes NVS and the shared data mutex.
- Wi-Fi is initialized in station mode to prepare for the management phase.

2. **Management Phase**:

- The ESP32 connects to a specified Wi-Fi network.
- The system time is synchronized using SNTP.
- Wi-Fi is deinitialized after time synchronization.

3. **Sniffer Phase**:

- Wi-Fi is reinitialized in promiscuous mode for packet capturing.
- Packet and CSI data callbacks are registered.
- SD card is initialized, and writer tasks are started to save captured data.
- A channel hopping task is started to periodically change Wi-Fi channels.
- BLE advertisements are started using NimBLE.

4. **Data Capturing**:

- The application captures Wi-Fi packets and CSI data.
- Captured data is written to files on the SD card (`capture.bin` and `csi.bin`).

5. **Cleanup**:

- On application termination, resources are cleaned up, including Wi-Fi deinitialization and SD card unmounting.

## Configuration Details

- **Wi-Fi Settings**:
- Wi-Fi SSID and password are configured in `menuconfig`.
- Wi-Fi operates in station mode during the management phase and in promiscuous mode during the sniffer phase.

- **SD Card Configuration**:
- The SD card is connected via SPI interface.
- SPI pins (MISO, MOSI, CLK, CS) are defined in `sniffer.c`.
- The SD card is mounted at `/sdcard`.

- **BLE Advertisement**:
- BLE uses NimBLE stack for low memory footprint.
- The device advertises with the name "MONAD".
- BLE advertisement data can be customized in `bluetooth.c`.

- **Channel Hopping**:
- The application hops through Wi-Fi channels 1 to 13.
- The channel switch interval is defined by `WIFI_CHANNEL_SWITCH_INTERVAL_MS`.

## Notes and Considerations

- **IRAM Usage**:
- The application may encounter IRAM overflow issues due to the combined size of the Wi-Fi and BLE stacks.
- Optimizations may be required, such as disabling unused features or adjusting configurations.

- **Thread Safety**:
- Mutexes are used to protect shared resources.
- Callbacks are designed to be minimal to prevent blocking.

- **Data Integrity**:
- The writer tasks periodically flush and close the files to ensure data integrity.
- This helps prevent data loss in case of power failure.

- **Future Refactoring**:
- Plans to separate CSI and packet capturing into separate modules for easier management.
- Potential to disable features dynamically to address memory constraints.

## Troubleshooting

- **Build Errors**:
- Ensure all components are included correctly in `CMakeLists.txt`.
- Verify that the ESP-IDF version is compatible.

- **IRAM Overflow**:
- Try disabling features like CSI capturing or BLE advertisements to reduce IRAM usage.
- Adjust compiler optimization settings in `menuconfig`.

- **SD Card Issues**:
- Check the SPI connections and pin configurations.
- Ensure the SD card is formatted correctly.

## License

This project is licensed under the [MIT License](LICENSE).

---

