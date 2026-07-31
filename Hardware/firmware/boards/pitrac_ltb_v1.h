// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// PiTrac "The Second Board To Rule Them All" â€” Rev V1 board header
//
// MCU: Raspberry Pi RP2354B, QFN-80.
//   - RP2350B core  -> 48 GPIOs, so PICO_RP2350A must be 0.
//   - "54" suffix   -> 2 MB stacked internal flash. QSPI SD0-3/SCLK are NOT bonded out
//                      (confirmed in the netlist); QSPI_SS is, and drives SW1 for BOOTSEL.
//
// Build with: -DPICO_BOARD=pitrac_ltb_v1 -DPICO_PLATFORM=rp2350

#ifndef _BOARDS_PITRAC_LTB_V1_H
#define _BOARDS_PITRAC_LTB_V1_H

// --- RP2350B, not RP2350A: 80-pin package, GPIO 0..47 -----------------------
#ifndef PICO_RP2350A
#define PICO_RP2350A 0
#endif

// --- Flash ------------------------------------------------------------------
// RP2354B has 2 MB stacked internal flash.
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// Conservative default; the internal die is fine with the standard timings.
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// --- Crystal ----------------------------------------------------------------
// Y1 = Abracon ABM8-272-T3 with C29/C30 = 15 pF load caps. 12 MHz.
// 12000 kHz is also the SDK default, so this is documentation as much as
// configuration -- but state it explicitly so the crystal is recorded in the
// board header rather than only in the schematic.
#ifndef XOSC_KHZ
#define XOSC_KHZ 12000
#endif

// --- On-board status LEDs ---------------------------------------------------
// D6 red on GPIO18, D5 yellow on GPIO19. Anode-driven through 120 R to GND:
// ACTIVE HIGH. Both live on the always-on +3V3 rail, so they work on USB power
// with the +5V latch open â€” which is what makes them the Phase 0/1 feedback path.
// (The panel LEDs on J7 are fed from the *switched* +5V rail and cannot indicate standby.)
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 18
#endif

// --- UART -------------------------------------------------------------------
// UART1 on GPIO36/37 goes to the Pi 5 (J8.8/10) and will carry a framed binary
// protocol. Declared here for hardware_uart users, but stdio over UART is
// disabled in CMakeLists.txt on purpose: never let printf() onto the Pi link.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 1
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 36
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 37
#endif

// --- No I2C or SPI is used by this board ------------------------------------
// The Pi header breaks out I2C1 and SPI0, but the RP2354 does not touch them.
// Deliberately no PICO_DEFAULT_I2C / PICO_DEFAULT_SPI here: those select an
// *instance* (0 means i2c0/spi0, not "none"), so defining them would imply a
// peripheral this board never uses.

// --- USB --------------------------------------------------------------------
// J6 USB-C, native RP2354 device, used for firmware upload (BOOTSEL) and the
// CDC bench CLI. VBUS diode-ORs into +5V_IN through D8 (SS14), which is why the
// board runs at ~4.6-4.7 V on USB alone -- and why firmware must refuse to close
// the +5V latch in that condition. See V5_MIN_FOR_PI in src/board.h.
// The default Raspberry Pi VID/PID from the SDK is fine; no override needed.

#endif // _BOARDS_PITRAC_LTB_V1_H
