# **Comprehensive Evaluation of Android Studio Project Templates and Development Specification for the e-Pod Companion Application**

## **Architectural Context and System Requirements**

The e-Pod zero-waste wireless audio player requires a companion Android mobile application to serve as an out-of-band management interface and binary file streaming client1. Operating within a 28-day build window, the system links a mobile handheld device directly to an ESP32-S3 microcontroller core (ReSpeaker Lite) integrated with an SPI MicroSD card module, an SSD1306 OLED display, an onboard 5W Class-D amplifier, and a salvaged laptop speaker1. Rather than utilizing traditional physical media connectors or cloud-mediated APIs, the system relies exclusively on Bluetooth Low Energy (BLE 5.0) for remote telemetry mirroring, media playback control, and chunked binary file transfer1.

\+---------------------------------------------------------------------------------------+  
|                                    Android Mobile App                                 |  
|                                                                                       |  
|  \+---------------------------------------------------------------------------------+  |  
|  |                           Presentation Layer (Compose UI)                       |  |  
|  |   \[Scan Screen\]    \[Now Playing Remote\]    \[File Transfer\]    \[Device Status\]   |  |  
|  \+---------------------------------------------------------------------------------+  |  
|                                          ^                                            |  
|                                          | (StateFlow / UI Events)                    |  
|                                          v                                            |  
|  \+---------------------------------------------------------------------------------+  |  
|  |                            Domain Layer (ViewModels)                            |  |  
|  \+---------------------------------------------------------------------------------+  |  
|                                          ^                                            |  
|                                          | (Suspend Functions / Flow)                 |  
|                                          v                                            |  
|  \+---------------------------------------------------------------------------------+  |  
|  |            BLE Transport Layer (Nordic Android-BLE-Library / ble-ktx)           |  |  
|  \+---------------------------------------------------------------------------------+  |  
\+------------------------------------------|--------------------------------------------+  
                                           |  
                                           | (BLE 5.0 GATT / MTU 517 Chunks)  
                                           v  
\+---------------------------------------------------------------------------------------+  
|                                  e-Pod Hardware Player                                |  
|                                                                                       |  
|  \+---------------------------------------------------------------------------------+  |  
|  |                  Core MCU: ReSpeaker Lite (XIAO ESP32-S3)                       |  |  
|  |                                                                                 |  |  
|  |   \+-------------------+     \+--------------------+     \+--------------------+   |  |  
|  |   | SPI MicroSD Card  |     | Onboard 5W Amp     |     | I2C SSD1306 OLED   |   |  |  
|  |   | (Audio Storage)   |     | & Laptop Speaker   |     | (Display UI)       |   |  |  
|  |   \+-------------------+     \+--------------------+     \+--------------------+   |  |  
|  \+---------------------------------------------------------------------------------+  |  
\+---------------------------------------------------------------------------------------+

Developing this application requires leveraging an AI pair-programming environment such as Antigravity1. Selecting an appropriate baseline template within Android Studio is essential to minimize architectural debt, eliminate unnecessary user interface boilerplate, and facilitate code generation1. The mobile client must implement Nordic Semiconductor’s Android-BLE-Library (specifically the modern ble-ktx coroutines module) to abstract Android’s native asynchronous Bluetooth Low Energy callbacks into reactive streams1.

## **Comparative Analysis of Android Studio Project Templates**

Modern Android Studio releases provide several starter templates designed for different application paradigms. Evaluating these templates against the operational requirements of the e-Pod client reveals significant trade-offs in structural complexity, UI framework alignment, and compatibility with AI-assisted code generation platforms1.

| Android Studio Template | UI Framework Architecture | Baseline Code Overhead | Suitability for e-Pod | Strategic Rationale and Technical Assessment |
| :---- | :---- | :---- | :---- | :---- |
| **Empty Activity** | Jetpack Compose (Material 3\) | Minimal | **Optimal (Recommended)** | Generates a clean, modern single-activity architecture configured for Jetpack Compose and ComponentActivity1. Eliminates legacy XML layouts, simplifying AI code generation while aligning natively with Kotlin StateFlow streams1. |
| **No Activity** | None (Raw Project Structure) | Zero UI Baseline | **High (Alternative)** | Configures Gradle scripts, dependency targets, and the manifest without generating UI components. Requires the AI workspace to construct the directory layout, DI modules, and activities from scratch. |
| **Empty Views Activity** | Legacy XML ViewBinding | Moderate | **Sub-optimal** | Configures legacy XML view hierarchies, ViewBinding classes, and AppCompatActivity. Adds layout boilerplate that increases friction when binding asynchronous BLE state streams1. |
| **Bottom Navigation Views Activity** | Legacy XML Views & Fragments | High | **Not Recommended** | Pre-configures a multi-fragment architecture using BottomNavigationView and NavController tied to XML resource graphs. Overcomplicates state synchronization across BLE connection lifecycles1. |
| **Navigation Drawer Views Activity** | Legacy XML Views & Drawer | High | **Not Recommended** | Designed for complex multi-tier enterprise applications utilizing drawer layouts. The e-Pod application requires a focused four-screen dashboard where persistent side drawers are redundant1. |
| **Native C++** | Android NDK / C++ CMake | Extremely High | **Unnecessary** | Configures CMake toolchains and Java Native Interface (JNI) stubs. Because hardware operations are executed on the ESP32-S3 microcontroller, native C++ compilation on the mobile client is redundant1. |

The evaluation demonstrates that the **Empty Activity** (Jetpack Compose) template represents the definitive structural foundation for the e-Pod mobile application1. Declarative UI frameworks natively observe asynchronous data streams emitted by Kotlin StateFlow primitives without requiring explicit view invalidation or adapter binding code2. Pre-configured templates utilizing XML layouts introduce legacy view management classes that complicate state propagation from background Bluetooth services3.  
By adopting the **Empty Activity** starter template, developers establish a lightweight structure that allows AI agents to output clean, single-activity Compose architectures without refactoring pre-generated layout files1.

## **BLE Protocol Architecture and Data Streaming Specification**

The mobile application operates as a GATT Central device communicating with the e-Pod peripheral hardware1. The custom primary GATT service configured on the ESP32-S3 exposes dedicated characteristics designed for remote control execution, hardware telemetry observation, and high-throughput binary file streaming1.

| Service / Characteristic Name | Assigned UUID | GATT Property Type | Data Frame & Protocol Payload |
| :---- | :---- | :---- | :---- |
| **e-Pod System Service** | 0000EPOD-0000-1000-8000-00805F9B34FB | Primary Service | Main service container for all e-Pod hardware features1. |
| **Remote Control Characteristic** | 0000EP01-0000-1000-8000-00805F9B34FB | Write Without Response | Single-byte command enumerations: 0x01 Play, 0x02 Pause, 0x03 Next Track, 0x04 Prev Track, 0x05 Vol Up, 0x06 Vol Down1. |
| **Device Telemetry Characteristic** | 0000EP02-0000-1000-8000-00805F9B34FB | Read / Notify | Structured byte array payload: Bytes 0–1: Track ID, Byte 2: Playback State, Byte 3: Battery %, Bytes 4–5: Free MicroSD Space (MB), Bytes 6+: Track Name (UTF-8)1. |
| **File Transfer Characteristic** | 0000EP03-0000-1000-8000-00805F9B34FB | Write Without Response | Sequenced binary chunk payload: Bytes 0–1: Sequence Index, Bytes 2–3: Data Length, Bytes 4+: Raw Audio Payload1. |

Streaming audio files over Bluetooth Low Energy without cloud infrastructure mandates maximizing transmission throughput1. Default BLE GATT transactions are constrained by a legacy ATT\_MTU size of 23 bytes, which yields a net application payload of only 20 bytes per transaction after subtracting standard 3-byte GATT headers.  
By invoking MTU negotiation via Nordic’s library immediately upon link establishment, the application requests an expanded ATT\_MTU of up to 517 bytes1. The maximum effective payload size per packet is defined by the formula:  
![][image1]  
![][image2]  
Negotiating connection intervals down to 15 milliseconds increases net data throughput from approximately 1.3 Kilobytes per second to over 30 Kilobytes per second1. This operational enhancement reduces file synchronization latency for standard audio files from minutes down to short operational windows, enabling efficient cable-free song transfers1.

## **Production-Ready Antigravity AI Prompting Specification**

To generate the mobile application architecture within the Antigravity workspace, the following system prompt must be supplied to the AI engine. The prompt encodes all operational parameters, runtime permission workflows, state structures, and UI layout requirements1.  
You are an expert Principal Android Mobility Engineer specializing in Kotlin, Jetpack Compose, Coroutines, StateFlow, and Bluetooth Low Energy (BLE) architecture.  
Your task is to develop the complete, production-ready companion Android application for "e-Pod" — a zero-waste wireless audio player powered by a Seeed ReSpeaker Lite (ESP32-S3) core running custom firmware.

### **1\. TECHNICAL STACK REQUIREMENTS**

* Primary Language: 100% Modern Kotlin  
* Minimum SDK: 26 (Android 8.0) | Target SDK: 34 (Android 14\)  
* UI Architecture: Single-Activity using Jetpack Compose with Material 3 Design  
* Async & State Management: Kotlin Coroutines, StateFlow, SharedFlow, ViewModel lifecycle models  
* BLE Protocol Library: Nordic Semiconductor's Android-BLE-Library (com.github.NordicSemiconductor:Android-BLE-Library:2.7.2) and ble-ktx extensions module (com.github.NordicSemiconductor:Android-BLE-Library:ble-ktx:2.7.2)  
* Architecture Pattern: MVVM with Repository Pattern and clean separation of concerns

### **2\. PERMISSIONS & ANDROID 12+ (API 31+) COMPLIANCE**

Implement a robust permission manager handling runtime checks.  
In AndroidManifest.xml:

* Declare android.permission.BLUETOOTH\_SCAN with android:usesPermissionFlags="neverForLocation"  
* Declare android.permission.BLUETOOTH\_CONNECT  
* Declare android.permission.BLUETOOTH\_ADVERTISE  
* Declare android.permission.READ\_EXTERNAL\_STORAGE (for API \< 33\) and android.permission.READ\_MEDIA\_AUDIO (for API 33+)

Implement logic in Jetpack Compose using RememberLauncherForActivityResult to request BLUETOOTH\_SCAN and BLUETOOTH\_CONNECT dynamically on application launch before scanning.

### **3\. BLE TRANSPORT & PROTOCOL SPECIFICATION**

Construct a dedicated BLE Manager class extending no.nordicsemi.android.ble.BleManager.  
GATT Protocol Configuration:

* Base Service UUID: "0000EPOD-0000-1000-8000-00805F9B34FB"  
* Control Characteristic UUID: "0000EP01-0000-1000-8000-00805F9B34FB" (Write Without Response)  
* Telemetry Characteristic UUID: "0000EP02-0000-1000-8000-00805F9B34FB" (Read \+ Notify)  
* File Sync Characteristic UUID: "0000EP03-0000-1000-8000-00805F9B34FB" (Write Without Response)

Connection Routine Requirements:

> 1. Implement isRequiredServiceSupported(gatt: BluetoothGatt) to validate all required characteristics exist.  
> 2. In initialize(), automatically request an expanded MTU by executing requestMtu(517).enqueue().  
> 3. Enable notifications on the Telemetry Characteristic. Parse incoming data packets:  
   * Byte 0-1: Track ID (UInt16)  
   * Byte 2: Playback State (0=Stopped, 1=Playing, 2=Paused)  
   * Byte 3: Battery Level (UInt8 0-100%)  
   * Byte 4-5: Available MicroSD Space in MB  
   * Bytes 6+: Track Name (UTF-8 String)  
> 4. Implement reliable connection handling: set retry(3, 100\) for connection establishment and handle drop events by emitting a Reconnecting state flow instead of crashing or hanging.

File Sync Protocol Implementation:

* Allow picking MP3/WAV files via Storage Access Framework (SAF) using ActivityResultContracts.GetContent().  
* Read file input stream and slice into chunks sized (Negotiated\_MTU \- 7\) bytes.  
* Prefix each outgoing packet with a 4-byte header: \[2 bytes Sequence Number (Big Endian)\] \+ \[2 bytes Chunk Length (Big Endian)\].  
* Stream chunks sequentially over the File Sync Characteristic using suspend functions, tracking transmission progress via a StateFlow\<Float\> progress indicator.

### **4\. SCREEN IMPLEMENTATION SPECIFICATIONS (JETPACK COMPOSE)**

Create a clean tabbed or bottom-navigation layout utilizing Material 3 Scaffold and NavigationBar exposing 4 screens:  
Screen 1: Scan & Connect Screen

* Displays status indicator (Disconnected, Scanning, Connecting, Connected).  
* FloatingActionButton / Button to trigger BLE scanning filtered by Service UUID 0000EPOD-0000-1000-8000-00805F9B34FB.  
* LazyColumn displaying scanned e-Pod devices showing device name, MAC address, and RSSI strength with a "Connect" button.

Screen 2: Now Playing / Remote Control Screen

* Centered track card displaying current song title mirrored from hardware telemetry.  
* Playback controls: Play, Pause, Next Track, Previous Track buttons triggering byte commands to the Control Characteristic (0x01=Play, 0x02=Pause, 0x03=Next, 0x04=Prev).  
* Volume slider sending volume scale values (0x05 \+ Vol Level) to the device.

Screen 3: Library & BLE File Transfer Screen

* File picker trigger to select audio files from device storage.  
* File metadata preview (File name, size in MB, estimated transfer time).  
* Transfer progress bar linked to StateFlow\<Float\> showing real-time upload status and speed.  
* Upload queue list showing completed and pending track transfers.

Screen 4: Device Status Screen

* Battery percentage display with visual battery status bar.  
* Storage health metrics: MicroSD total/used/free space rendered via dynamic progress graphics.  
* Hardware telemetry log: Displays connection RSSI, negotiated MTU size, and raw BLE service state details for debugging.

### **5\. CODE OUTPUT FORMAT REQUIREMENTS**

Provide full, clean, un-truncated Kotlin source code partitioned cleanly into logical files:

* MainActivity.kt  
* permissions/PermissionHandler.kt  
* ble/EPodBleManager.kt  
* ble/EPodProtocol.kt  
* viewmodels/EPodViewModel.kt  
* ui/screens/ScanScreen.kt  
* ui/screens/RemoteScreen.kt  
* ui/screens/TransferScreen.kt  
* ui/screens/StatusScreen.kt  
* ui/components/CommonUi.kt

Ensure all imports are explicitly stated, asynchronous workflows leverage Coroutine scopes correctly, and UI components handle connection states gracefully without freezing.

## **Implementation Schedule and Technical Risk Mitigation**

The development roadmap for the e-Pod companion application takes place between Day 12 and Day 17 of the overall project build plan1. The engineering tasks are structured to ensure software components align with hardware bring-up milestones1.

| Operational Timeline | Target Subsystem Module | Core Implementation Objectives | Verification Criteria |
| :---- | :---- | :---- | :---- |
| **Day 12** \[cite: 1\] | **Project Setup & BLE Scanner** | Initialize Android Studio project with Empty Activity. Configure runtime permissions for Android 12+1. Implement BLE device discovery routines1. | Scans and filters nearby e-Pod devices emitting the designated UUID1. |
| **Day 13** \[cite: 1\] | **GATT Connection & Control** | Implement EPodBleManager using Nordic ble-ktx coroutines3. Bind play, pause, next, and previous triggers to control characteristics1. | Phone buttons directly alter playback state on the physical ESP32-S3 hardware1. |
| **Day 14** \[cite: 1\] | **Telemetry Parsing Engine** | Subscribe to GATT telemetry notifications1. Parse track metadata, battery state, and available storage into reactive StateFlow models1. | App UI dynamically mirrors hardware SSD1306 OLED display text1. |
| **Day 15** \[cite: 1\] | **Chunked BLE Streaming** | Integrate Storage Access Framework (SAF) URI resolution1. Configure MTU 517 negotiation and chunked binary write streams1. | Audio files picked on the mobile device land on the MicroSD card and play back1. |
| **Day 16** \[cite: 1\] | **UI State Polish** | Refine Jetpack Compose dashboard views1. Add animated file transfer meters, battery bars, and storage capacity indicators1. | App provides a unified, responsive interface across all operational screens1. |
| **Day 17** \[cite: 1\] | **End-to-End Stress Validation** | Execute systematic connectivity, range, and stress tests across full scan-connect-transfer loops1. | Zero application crashes or GATT drops across 10 consecutive transfer cycles1. |

### **Addressing Android GATT Error 133 Exceptions**

A primary failure mode in native Android BLE development involves silent connection failures returning error code GATT 1334. This condition arises when connection attempts conflict with internal stack state caching or background radio activity6. Standard native Android API methods lack internal retry queues, causing immediate disconnection failures3.  
Nordic Semiconductor's library resolves this instability by embedding explicit connection retries and queue management directly into the connection pipeline3. Configuring connection requests with .retry(3, 100\) forces the execution stack to attempt three sequential re-connections at 100-millisecond backoff intervals before throwing an exception4. Additionally, disabling native background auto-connect parameters (useAutoConnect(false)) during active scanning prevents Android's internal GATT stack from stalling when pairing with hardware peripherals2.

### **Hardware Storage Contention and Streaming Controls**

Concurrent processing on the ESP32-S3 core introduces operational risks during file transfers1. If binary data streams over BLE outpace the microcontroller's SPI bus write speeds to the MicroSD card, incoming buffer overflows can cause frame corruption or trigger audio stuttering during real-time playback1.  
To mitigate hardware bus contention, the mobile application's streaming engine leverages explicit sequence headers combined with flow control1. By prepending packet chunk numbers to outgoing data payloads, the ESP32-S3 firmware validates frame integrity before committing data blocks to storage1. Incorporating micro-delays between packet bursts ensures continuous audio rendering alongside concurrent file writes, delivering a reliable wireless media streaming experience1.

## **Strategic Conclusions**

> 1. **Adopt the Empty Activity Starter Template**: Initializing the project using Jetpack Compose provides a clean slate without legacy XML layout overhead1. This design choice simplifies state binding and provides an ideal foundation for AI-assisted code generation in Antigravity1.  
> 2. **Leverage Nordic Semiconductor's BLE Infrastructure**: Integrating com.github.NordicSemiconductor:Android-BLE-Library (ble-ktx) abstracts native BLE callback complexities into clean Kotlin Coroutines and reactive StateFlow structures2. This architecture simplifies queueing, packet splitting, and MTU 517 throughput negotiation1.  
> 3. **Execute System Integration per the Schedule**: Adhering strictly to the Day 12–17 mobile application roadmap ensures that software streaming services, GATT controls, and telemetry views align with hardware milestones ahead of event-day demonstrations1.

#### **Works cited**

> 1. e-Pod\_Build\_and\_Launch\_Plan.pdf  
> 2. nordicsemi/Kotlin-BLE-Library \- GitHub, [https://github.com/nordicsemi/Kotlin-BLE-Library](https://github.com/nordicsemi/Kotlin-BLE-Library)  
> 3. nordicsemi/Android-BLE-Library \- GitHub, [https://github.com/nordicsemi/Android-BLE-Library](https://github.com/nordicsemi/Android-BLE-Library)  
> 4. Android-BLE-Library/USAGE.md at main \- GitHub, [https://github.com/NordicSemiconductor/Android-BLE-Library/blob/main/USAGE.md](https://github.com/NordicSemiconductor/Android-BLE-Library/blob/main/USAGE.md)  
> 5. Android Bluetooth Low Energy (BLE) Resources \- Gist \- GitHub, [https://gist.github.com/ajaypro/6a717d292cf892e1bfd733673d0581b5](https://gist.github.com/ajaypro/6a717d292cf892e1bfd733673d0581b5)  
> 6. Android Bluetooth Low Energy (BLE) Resources \- GitHub Gist, [https://gist.github.com/stkent/a7f0d6b868e805da326b112d60a9f59b](https://gist.github.com/stkent/a7f0d6b868e805da326b112d60a9f59b)

[image1]: <data:image/png;base64,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>

[image2]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAlIAAABNCAYAAABtwIoUAAAQjUlEQVR4Xu2dC6htRRnHv6jAKK3MniZamT3U7KFJD/Wm2YPIojKLNCR7ExmKlQV6JYKsrCzTEOVaYk/LwtKo0EOJRYGWWEYPyEjDQqOo6F3r1+zv7tlz1tp3n+vZ+5xz/f1gOHevNWvN+5v/fDN73wgRERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERGRhXGPLmzqwstG4cVd2K2O0LFrF14U4ziETXWEVeJxMZlGG/btwj23xp4Pr+jC+7twfhee39ybF5T7VV34aBee2NzbqNynCzu1F+X/0IcZY3dvb9xFuFeUsfX2LpwS9pP1Ttr/M6LYRRFpuHcXPtuFX3fhv6OwJSYFy/5duKG6f0eUZ1abN0bJB+8nnT+PPmf4dxf+2oWTY36C6tYYl/Mdzb15Qbkz3Rc29xbJ4V14axf268JDR3+v68Lz6kgVu8SwYf1kF26OIhBbQcwkymS6CF7ehZ/GuD8d3IW7TcRYLCxczotSN9TxWvHaLny3C4+Oko+nduFdXTigjlTx+Ch9oQ/q86VR6vkfXfhMFx4+EWOSB3XhO1HaYymK6Jb1S9p/2pZxJCJTYKD8J8pgOba5B1/pwpmx8omIgTg04faBYWeieUlznZXRVVHyt5L3rZT3xmKFFFCnl8TaCinSThGZ4YMxKVoRIngS6AsIWwRTC+L8W7H8XRl+1IUHbI09P8j3ZV3YO0r9MrmTPvlfaR9eLY6KUm9rLaTo2227kK+6XhC7eGevr+K0EJ/6RBRRNp65oAu/78KBVbw+DorVF1LP7sKH2ouyKmCP+/rA9mA7yQ4Lk+JeXfhllAHTeg24vz3i4g1RDOaspJDqExV5j/yxup8HOclsT1nvDNRvX5kXBWmvZFKjfvqEFFuVV3Th/s11hM0iRUwKw5OqaykI1qKe6a9f7MJfYn0Iqb6264M+sRT9kyjb0Vxvt8F59z+bay1PidUXUuRn1nLJysjxtBrYTrLDkh0bNz2rU7bQarZnosfz8P2Yj5BaTQNco5CajaHJmNXmllgumOhPrTifJ/QVtpD2rK7RrggZJvFFc2QXjogdS0j9IPrr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>