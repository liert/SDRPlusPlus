#pragma once
#include <string>

namespace flrc {

enum class Language {
    ZH_CN = 0, // 简体中文
    EN_US = 1  // English
};

struct TextStrings {
    // Top Tabs
    const char* tabDemod;
    const char* tabFraming;
    const char* tabProtocol;
    const char* tabLog;

    // Language & Preset
    const char* languageSelect;
    const char* presetLabel;
    const char* phyHeader;
    const char* bitrate;
    const char* deviation;
    const char* lpfCutoff;
    const char* eyeDiagram;

    // Framing Tab
    const char* preambleHeader;
    const char* agcPreamble;
    const char* agcThreshold;
    const char* timingPreamble;
    const char* timingTolerance;
    const char* syncHeader;
    const char* syncWord;
    const char* autoSync;
    const char* maskMode;
    const char* diffDecode;
    const char* hwCrc;

    // Protocol Tab
    const char* protoParser;
    const char* h12Stats;
    const char* h12Waiting;
    const char* frameType;
    const char* hopChannel;
    const char* group;
    const char* route;
    const char* rcChannels;
    const char* telemetry;
    const char* remoteId;
    const char* hopSequence;

    // Log Tab
    const char* statsSummary;
    const char* clearLog;
    const char* recentFrames;
    const char* noFrames;
};

inline const TextStrings& getText(Language lang) {
    static const TextStrings zh = {
        /* tabDemod         */ " 📡 1. 物理解调 ",
        /* tabFraming       */ " 📐 2. 帧同步与切片 ",
        /* tabProtocol      */ " 🧩 3. 协议解析 (H12) ",
        /* tabLog           */ " 📋 4. 帧日志与统计 ",
        /* languageSelect   */ "界面语言",
        /* presetLabel      */ "调制预设",
        /* phyHeader        */ "物理层调制参数:",
        /* bitrate          */ "符号率 / 比特率 (kbps)",
        /* deviation        */ "频偏 (kHz)",
        /* lpfCutoff        */ "低通滤波器截止 (kHz)",
        /* eyeDiagram       */ "瞬时频偏 / 眼图波形:",
        /* preambleHeader   */ "前导码与时钟同步设置:",
        /* agcPreamble      */ "启用 32位 AGC 前导码 (0101...)",
        /* agcThreshold     */ "AGC 相关检测门限",
        /* timingPreamble   */ "启用 21位 时序恢复前导码",
        /* timingTolerance  */ "时序容差 (允许误码位数)",
        /* syncHeader       */ "同步字与差分解扰设置:",
        /* syncWord         */ "同步字 (十六进制 Hex)",
        /* autoSync         */ "自动同步字匹配 (捕获所有有效载波帧)",
        /* maskMode         */ "CR=1 掩码模式",
        /* diffDecode       */ "启用差分 CumXOR 累计异或解码",
        /* hwCrc            */ "SX1280 硬件 CRC-32 校验",
        /* protoParser      */ "协议扩展解析器",
        /* h12Stats         */ "H12 统计: 总帧数: %llu | CRC8有效: %llu | 对频帧: %llu | 管理帧: %llu",
        /* h12Waiting       */ "正在等待 H12 空口协议帧数据...",
        /* frameType        */ "帧类型:",
        /* hopChannel       */ "跳频信道序号: %u (CH#%u)",
        /* group            */ "数据组号: %u",
        /* route            */ "透明路由: %s",
        /* rcChannels       */ "12路遥控摇杆通道数值 (0..960, 中位=480):",
        /* telemetry        */ "透明数传串口数据:",
        /* remoteId         */ "遥控器身份 ID: 0x%08X",
        /* hopSequence      */ "15信道跳频序列: %s",
        /* statsSummary     */ "统计: 捕获总帧数: %llu | CRC有效: %llu",
        /* clearLog         */ "清空日志",
        /* recentFrames     */ "历史捕获报文 (最近 %zu 帧):",
        /* noFrames         */ "尚未捕获到数据帧。"
    };

    static const TextStrings en = {
        /* tabDemod         */ " 📡 1. Demod ",
        /* tabFraming       */ " 📐 2. Framing ",
        /* tabProtocol      */ " 🧩 3. Protocol (H12) ",
        /* tabLog           */ " 📋 4. Log & Stats ",
        /* languageSelect   */ "Language",
        /* presetLabel      */ "Preset",
        /* phyHeader        */ "Physical Layer Parameters:",
        /* bitrate          */ "Bitrate / Symbolrate (kbps)",
        /* deviation        */ "Deviation (kHz)",
        /* lpfCutoff        */ "LPF Cutoff (kHz)",
        /* eyeDiagram       */ "Instantaneous Frequency / Eye Diagram:",
        /* preambleHeader   */ "Preamble & Clock Sync Settings:",
        /* agcPreamble      */ "Enable 32-bit AGC Preamble (0101...)",
        /* agcThreshold     */ "AGC Correlation Threshold",
        /* timingPreamble   */ "Enable 21-bit Timing Preamble",
        /* timingTolerance  */ "Timing Tolerance (bits)",
        /* syncHeader       */ "Sync Word & De-scrambling:",
        /* syncWord         */ "Sync Word (Hex)",
        /* autoSync         */ "Auto Sync Word (Capture Any Valid Sync)",
        /* maskMode         */ "CR=1 Mask Mode",
        /* diffDecode       */ "Differential CumXOR Decoding",
        /* hwCrc            */ "SX1280 Hardware CRC-32 Verification",
        /* protoParser      */ "Protocol Parser",
        /* h12Stats         */ "H12 Stats: Total: %llu | CRC8 OK: %llu | Pairing: %llu | Mgmt: %llu",
        /* h12Waiting       */ "Waiting for H12 air-protocol frames...",
        /* frameType        */ "Frame Type:",
        /* hopChannel       */ "Hop Index: %u (CH#%u)",
        /* group            */ "Group: %u",
        /* route            */ "Route: %s",
        /* rcChannels       */ "12-Channel RC Status (0..960, Mid=480):",
        /* telemetry        */ "Transparent Serial Telemetry:",
        /* remoteId         */ "Remote Identity ID: 0x%08X",
        /* hopSequence      */ "15-Channel Hop Sequence: %s",
        /* statsSummary     */ "Statistics: Total: %llu | Valid CRC: %llu",
        /* clearLog         */ "Clear Log",
        /* recentFrames     */ "Recent Frames (Last %zu):",
        /* noFrames         */ "No frames captured yet."
    };

    return (lang == Language::ZH_CN) ? zh : en;
}

} // namespace flrc
