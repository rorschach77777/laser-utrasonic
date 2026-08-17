#pragma once
#include <cstdint>

namespace Reg {

// === Input Registers (FC 0x04) ===
namespace Input {
    // Device Info (0x0000-0x000F)
    constexpr uint16_t DEVICE_INFO       = 0x0000;
    constexpr uint16_t DEVICE_TYPE       = 0x0000;
    constexpr uint16_t HW_VERSION        = 0x0001;
    constexpr uint16_t SW_VERSION        = 0x0002;
    constexpr uint16_t SERIAL_H          = 0x0003;
    constexpr uint16_t SERIAL_L          = 0x0004;
    constexpr uint16_t UPTIME_H          = 0x0006;
    constexpr uint16_t UPTIME_L          = 0x0007;
    constexpr uint16_t DEVICE_INFO_COUNT = 16;

    // System Status (0x0010-0x001F)
    constexpr uint16_t SYS_STATUS_BASE   = 0x0010;
    constexpr uint16_t SYS_STATUS        = 0x0010;
    constexpr uint16_t SYS_FAULT         = 0x0011;
    constexpr uint16_t GATE_LEVEL        = 0x0012;
    constexpr uint16_t GATE_STATE        = 0x0013;
    constexpr uint16_t GATE_STATUS_BASE  = GATE_LEVEL;
    constexpr uint16_t GATE_STATUS_COUNT = 2;
    constexpr uint16_t MAIN_SYNC_STEP    = 0x0018;
    constexpr uint16_t MAIN_SYNC_STATUS  = 0x0019;
    constexpr uint16_t MAIN_TD_MEAS_H    = 0x001A;
    constexpr uint16_t MAIN_TD_MEAS_L    = 0x001B;
    constexpr uint16_t SYS_STATUS_COUNT  = 16;

    // Slave Status (0x0020-0x002F)
    constexpr uint16_t SLAVE_STATUS_BASE      = 0x0020;
    constexpr uint16_t SLAVE_ONLINE           = 0x0020;
    constexpr uint16_t SLAVE_SYS_STATUS       = 0x0021;
    constexpr uint16_t SLAVE_SYS_FAULT        = 0x0022;
    constexpr uint16_t SLAVE_GATE_LEVEL       = 0x0023;
    constexpr uint16_t SLAVE_GATE_STATE       = 0x0024;
    constexpr uint16_t SLAVE_GATE_STATUS_BASE = SLAVE_GATE_LEVEL;
    constexpr uint16_t SLAVE_GATE_STATUS_COUNT = 2;
    constexpr uint16_t SLAVE_SYNC_STEP        = 0x0028;
    constexpr uint16_t SLAVE_SYNC_STATUS_CODE = 0x0029;
    constexpr uint16_t SLAVE_SYNC_ERR_COUNT   = 0x002A;
    constexpr uint16_t SLAVE_UPTIME_H         = 0x002E;
    constexpr uint16_t SLAVE_UPTIME_L         = 0x002F;
    constexpr uint16_t SLAVE_STATUS_COUNT     = 16;

    // DFB Status (0x0030-0x003F)
    constexpr uint16_t DFB_STATUS_BASE   = 0x0030;
    constexpr uint16_t DFB_ONLINE        = 0x0030;
    constexpr uint16_t DFB_FAULT         = 0x0031;
    constexpr uint16_t DFB_TEC_STATUS    = 0x0032;
    constexpr uint16_t DFB_TEMP          = 0x0033;
    constexpr uint16_t DFB_CURRENT       = 0x0034;
    constexpr uint16_t DFB_PD_VOLTAGE    = 0x0035;
    constexpr uint16_t DFB_UPTIME_H      = 0x003E;
    constexpr uint16_t DFB_UPTIME_L      = 0x003F;
    constexpr uint16_t DFB_STATUS_COUNT  = 16;

    // MultiMode Pump Status (0x0040-0x004F)
    constexpr uint16_t MPUMP_STATUS_BASE   = 0x0040;
    constexpr uint16_t MPUMP_ONLINE        = 0x0040;
    constexpr uint16_t MPUMP_FAULT         = 0x0041;
    constexpr uint16_t MPUMP_CURRENT_ACT   = 0x0042;
    constexpr uint16_t MPUMP_UPTIME_H      = 0x004E;
    constexpr uint16_t MPUMP_UPTIME_L      = 0x004F;
    constexpr uint16_t MPUMP_STATUS_COUNT  = 16;

    // Pump1 Status (0x0050-0x005F)
    constexpr uint16_t PUMP1_STATUS_BASE = 0x0050;
    constexpr uint16_t PUMP1_STATUS_COUNT = 16;
    // Pump2 Status (0x0060-0x006F)
    constexpr uint16_t PUMP2_STATUS_BASE = 0x0060;
    constexpr uint16_t PUMP2_STATUS_COUNT = 16;

    // Temperature (0x0080-0x008F)
    constexpr uint16_t TEMP_BASE         = 0x0080;
    constexpr uint16_t TEMP_COUNT        = 8;
    constexpr uint16_t TEMP_BLOCK_COUNT  = 16;

    // Slave Pump1 Status (0x0400-0x040F)
    constexpr uint16_t SLAVE_PUMP1_STATUS_BASE  = 0x0400;
    constexpr uint16_t SLAVE_PUMP1_STATUS_COUNT = 16;
    // Slave Pump2 Status (0x0410-0x041F)
    constexpr uint16_t SLAVE_PUMP2_STATUS_BASE  = 0x0410;
    constexpr uint16_t SLAVE_PUMP2_STATUS_COUNT = 16;
}

// === Holding Registers (FC 0x03/0x06/0x10) ===
namespace Hold {
    // Communication Params (0x0000-0x000F)
    constexpr uint16_t SLAVE_ADDR        = 0x0000;
    constexpr uint16_t BAUDRATE          = 0x0001;
    constexpr uint16_t PASSTHROUGH_MODE  = 0x0002;
    constexpr uint16_t SUB_MODULE_EN     = 0x0008;

    // System Control (0x0010-0x001F)
    constexpr uint16_t SYS_CTRL_BASE     = 0x0010;
    constexpr uint16_t PULSE_CH_ENABLE   = 0x0014;
    constexpr uint16_t PULSE_PERIOD      = 0x0015;
    constexpr uint16_t PULSE_COUNT       = 0x0016;
    constexpr uint16_t GATE_ENABLE       = 0x0017;
    constexpr uint16_t MAIN_SYNC_ENABLE  = 0x0018;
    constexpr uint16_t MAIN_TW_H         = 0x0019;
    constexpr uint16_t MAIN_TW_L         = 0x001A;
    constexpr uint16_t GATE_RISE_DELAY_US = 0x001D;
    constexpr uint16_t GATE_FALL_DELAY_US = 0x001E;
    constexpr uint16_t GATE_POLARITY      = 0x001F;
    constexpr uint16_t GATE_CTRL_BASE     = GATE_ENABLE;
    constexpr uint16_t GATE_CTRL_COUNT    = 9;

    // Slave Control (0x0020-0x002F)
    constexpr uint16_t SLAVE_CTRL_BASE           = 0x0020;
    constexpr uint16_t SLAVE_SUB_MODULE_EN       = 0x0020;
    constexpr uint16_t SLAVE_PULSE_CH_ENABLE     = 0x0024;
    constexpr uint16_t SLAVE_PULSE_PERIOD        = 0x0025;
    constexpr uint16_t SLAVE_PULSE_COUNT         = 0x0026;
    constexpr uint16_t SLAVE_GATE_ENABLE         = 0x0027;
    constexpr uint16_t SLAVE_SYNC_ENABLE         = 0x0028;
    constexpr uint16_t SLAVE_TW_H                = 0x0029;
    constexpr uint16_t SLAVE_TW_L                = 0x002A;
    constexpr uint16_t SLAVE_TD_MEAS_H           = 0x002B;
    constexpr uint16_t SLAVE_TD_MEAS_L           = 0x002C;
    constexpr uint16_t SLAVE_GATE_RISE_DELAY_US  = 0x002D;
    constexpr uint16_t SLAVE_GATE_FALL_DELAY_US  = 0x002E;
    constexpr uint16_t SLAVE_GATE_POLARITY       = 0x002F;
    constexpr uint16_t SLAVE_GATE_CTRL_BASE      = SLAVE_GATE_ENABLE;
    constexpr uint16_t SLAVE_GATE_CTRL_COUNT     = 9;

    // DFB Control (0x0030-0x003F)
    constexpr uint16_t DFB_CTRL_BASE     = 0x0030;
    constexpr uint16_t DFB_SWITCH        = 0x0030;
    constexpr uint16_t DFB_LD_SWITCH     = 0x0031;
    constexpr uint16_t DFB_TEMP_TARGET   = 0x0032;
    constexpr uint16_t DFB_DRIVE_MODE    = 0x0033;
    constexpr uint16_t DFB_CURRENT_SET   = 0x0034;
    constexpr uint16_t DFB_CURRENT_MAX   = 0x0035;

    // MultiMode Pump Control (0x0040-0x004F)
    constexpr uint16_t MPUMP_CTRL_BASE   = 0x0040;
    constexpr uint16_t MPUMP_SWITCH      = 0x0040;
    constexpr uint16_t MPUMP_DRIVE_MODE  = 0x0041;
    constexpr uint16_t MPUMP_CURRENT_SET = 0x0042;

    // Pump1 Control (0x0050-0x005F)
    constexpr uint16_t PUMP1_CTRL_BASE   = 0x0050;
    // Pump2 Control (0x0060-0x006F)
    constexpr uint16_t PUMP2_CTRL_BASE   = 0x0060;

    // Pump register offsets (shared for Pump1/2 and slave pumps)
    namespace PumpOff {
        constexpr uint16_t ENABLE        = 0;
        constexpr uint16_t MODE          = 1;
        constexpr uint16_t CURRENT       = 2;
        constexpr uint16_t PULSE_WIDTH   = 3;
        constexpr uint16_t VOLTAGE_LIMIT = 4;
        constexpr uint16_t INT_FREQ      = 5;
        constexpr uint16_t EXT_DELAY     = 6;
        constexpr uint16_t DELAY_OUT     = 7;
        constexpr uint16_t ADDR          = 8;
    }

    // Pump status input register offsets
    namespace PumpStatOff {
        constexpr uint16_t ONLINE        = 0;
        constexpr uint16_t ENABLE_RB     = 1;
        constexpr uint16_t STATUS_CODE   = 2;
        constexpr uint16_t ADDR_RB       = 3;
        constexpr uint16_t CURRENT_ACT   = 4;
        constexpr uint16_t VOLTAGE_ACT   = 5;
        constexpr uint16_t FREQ_ACT      = 6;
        constexpr uint16_t CURRENT_RB    = 7;
        constexpr uint16_t PULSE_WIDTH_RB = 8;
        constexpr uint16_t VLIMIT_RB     = 9;
        constexpr uint16_t INT_FREQ_RB   = 10;
        constexpr uint16_t EXT_DELAY_RB  = 11;
        constexpr uint16_t DELAY_OUT_RB  = 12;
    }

    // Command Control (0x0080-0x008F)
    constexpr uint16_t CMD_RUN           = 0x0080;
    constexpr uint16_t CMD_STOP          = 0x0081;
    constexpr uint16_t CMD_SINGLE        = 0x0082;
    constexpr uint16_t CMD_SAVE          = 0x0083;
    constexpr uint16_t CMD_FACTORY       = 0x0084;
    constexpr uint16_t CMD_REBOOT        = 0x0085;
    constexpr uint16_t CMD_CLR_FAULT     = 0x0086;
    constexpr uint16_t CMD_SYNC_PARAM    = 0x0087;

    constexpr uint16_t CMD_RUN_VAL       = 0x0001;
    constexpr uint16_t CMD_STOP_VAL      = 0x0001;
    constexpr uint16_t CMD_SINGLE_VAL    = 0x0001;
    constexpr uint16_t CMD_SAVE_VAL      = 0x5A5A;
    constexpr uint16_t CMD_FACTORY_VAL   = 0xA5A5;
    constexpr uint16_t CMD_REBOOT_VAL    = 0x55AA;
    constexpr uint16_t CMD_CLR_FAULT_VAL = 0xAAAA;
    constexpr uint16_t CMD_SYNC_PARAM_VAL = 0x0001;

    // Main Pulse Timing (0x0200-0x031F)
    constexpr uint16_t MAIN_TIMING_BASE  = 0x0200;
    constexpr uint16_t TIMING_CH_STRIDE  = 18;   // 0x12

    // Slave Pump1 Control (0x0400-0x040F)
    constexpr uint16_t SLAVE_PUMP1_CTRL_BASE = 0x0400;
    // Slave Pump2 Control (0x0410-0x041F)
    constexpr uint16_t SLAVE_PUMP2_CTRL_BASE = 0x0410;

    // Slave Pulse Timing (0x0500-0x061F)
    constexpr uint16_t SLAVE_TIMING_BASE = 0x0500;

    // Timing channel register offsets
    namespace TimingOff {
        constexpr uint16_t PERIOD      = 0;
        constexpr uint16_t PULSE_COUNT = 1;
        constexpr uint16_t DELAY_1     = 2;
        constexpr uint16_t WIDTH_1     = 3;
        // pattern: DELAY_N = 2 + (N-1)*2, WIDTH_N = 3 + (N-1)*2
    }
}

// Channel names
inline const char* channelName(int ch) {
    static const char* names[] = {
        "DFB_EN", "PUMP_MM_EN", "PUMP1_EN", "PUMP2_EN",
        "POWER_DETECT", "QSW_L1", "QSW_L2_A", "QSW_L2_B",
        "QSW_L3_A", "QSW_L3_B", "QSW_L3_C", "QSW_L3_D",
        "RESERVED", "RESERVED", "RESERVED", "RESERVED"
    };
    if (ch >= 0 && ch < 16) return names[ch];
    return "UNKNOWN";
}

// Baudrate lookup
inline int baudRateFromCode(int code) {
    static const int rates[] = {4800, 9600, 19200, 38400, 57600, 115200, 230400};
    if (code >= 0 && code <= 6) return rates[code];
    return 9600;
}

} // namespace Reg
