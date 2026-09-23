#include "logger.h"

#include <user_interface.h>

char logBuffer[LOG_BUFFER_SIZE][LOG_LINE_LENGTH];
int logIndex = 0;
int logCount = 0;

void loggerInit() {
    memset(logBuffer, 0, sizeof(logBuffer));
    logIndex = 0;
    logCount = 0;
}

void logPrint(const String &msg) {
    // Print to serial
    Serial.println(msg);

    // Store in circular buffer
    strncpy(logBuffer[logIndex], msg.c_str(), LOG_LINE_LENGTH - 1);
    logBuffer[logIndex][LOG_LINE_LENGTH - 1] = '\0';

    logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;
    if (logCount < LOG_BUFFER_SIZE) {
        logCount++;
    }
}

void logPrintf(const char* format, ...) {
    char buffer[LOG_LINE_LENGTH];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, LOG_LINE_LENGTH, format, args);
    va_end(args);

    logPrint(String(buffer));
}

String logGetAll() {
    String result = "";
    result.reserve(LOG_BUFFER_SIZE * LOG_LINE_LENGTH);

    int start = (logCount < LOG_BUFFER_SIZE) ? 0 : logIndex;
    int entries = (logCount < LOG_BUFFER_SIZE) ? logCount : LOG_BUFFER_SIZE;

    for (int i = 0; i < entries; i++) {
        int idx = (start + i) % LOG_BUFFER_SIZE;
        result += logBuffer[idx];
        result += "\n";
    }

    return result;
}

// --- Crash record ----------------------------------------------------------------------
// On a crash (CPU exception, software watchdog, abort, `new` out of memory, stack smash)
// the core prints the details to serial and restarts, and the next boot reports most of
// them as a plain "Software/System restart". This board runs headless, so the core's
// custom_crash_callback() hook saves the essentials to RTC user memory, which survives a
// restart but not a power cut, and /app.json shows them for the boot that followed.
// Decode the addresses with xtensa-lx106-elf-addr2line against the matching firmware.elf.
namespace {
constexpr uint32_t kCrashRtcBlock = 64;       // user blocks 0-31 belong to eboot (OTA)
constexpr uint32_t kCrashNew = 0xC0DE0001;   // written by the crash, not yet booted past
constexpr uint32_t kCrashSeen = 0xC0DE0002;  // this boot followed that crash
constexpr int kCrashStackWords = 16;

struct CrashRecord {
    uint32_t magic;
    uint32_t reason;  // 2 exception, 3 soft WDT, 253 stack smash, 254 abort/panic/`new` OOM
    uint32_t exccause;
    uint32_t epc1;
    uint32_t excvaddr;
    uint32_t sp;
    uint32_t failedAllocCaller;  // the core's note of the last malloc/new that failed
    uint32_t failedAllocSize;
    uint32_t code[kCrashStackWords];  // code addresses found on the stack, innermost first
};

bool isCodeAddress(uint32_t value) {
    return (value >= 0x40100000 && value < 0x40108000) ||  // IRAM
           (value >= 0x40201000 && value < 0x40300000);    // flash
}

bool readCrashRecord(CrashRecord &record) {
    return ESP.rtcUserMemoryRead(kCrashRtcBlock, reinterpret_cast<uint32_t *>(&record),
                                 sizeof(record));
}
}  // namespace

extern "C" {
extern void *umm_last_fail_alloc_addr;
extern int umm_last_fail_alloc_size;

// Runs inside the crash, just before the restart: no heap, no Serial.
void custom_crash_callback(struct rst_info *info, uint32_t stack, uint32_t stackEnd) {
    CrashRecord record{};
    record.magic = kCrashNew;
    record.reason = info->reason;
    record.exccause = info->exccause;
    record.epc1 = info->epc1;
    record.excvaddr = info->excvaddr;
    record.sp = stack;
    record.failedAllocCaller = reinterpret_cast<uint32_t>(umm_last_fail_alloc_addr);
    record.failedAllocSize = static_cast<uint32_t>(umm_last_fail_alloc_size);
    int found = 0;
    for (uint32_t at = stack; at + 4 <= stackEnd && found < kCrashStackWords; at += 4) {
        uint32_t value = *reinterpret_cast<const uint32_t *>(at);
        if (isCodeAddress(value)) {
            record.code[found++] = value;
        }
    }
    ESP.rtcUserMemoryWrite(kCrashRtcBlock, reinterpret_cast<uint32_t *>(&record), sizeof(record));
}
}

void crashRecordOnBoot() {
    CrashRecord record;
    if (!readCrashRecord(record) || (record.magic != kCrashNew && record.magic != kCrashSeen)) {
        return;
    }
    // A new record explains this boot; a seen one explained the boot before, so retire it.
    record.magic = (record.magic == kCrashNew) ? kCrashSeen : 0;
    ESP.rtcUserMemoryWrite(kCrashRtcBlock, &record.magic, sizeof(record.magic));
}

String crashRecordText() {
    CrashRecord record;
    if (!readCrashRecord(record) || record.magic != kCrashSeen) {
        return String();
    }
    char text[320];
    int length = snprintf_P(text, sizeof(text),  // _P: format strings stay in flash, not DRAM
                            PSTR("reason %u cause %u epc1 0x%08x excvaddr 0x%08x sp 0x%08x "
                                 "alloc 0x%08x/%u stack"),
                            static_cast<unsigned>(record.reason),
                            static_cast<unsigned>(record.exccause),
                            static_cast<unsigned>(record.epc1),
                            static_cast<unsigned>(record.excvaddr),
                            static_cast<unsigned>(record.sp),
                            static_cast<unsigned>(record.failedAllocCaller),
                            static_cast<unsigned>(record.failedAllocSize));
    for (int i = 0; i < kCrashStackWords && record.code[i] != 0 &&
                    length < static_cast<int>(sizeof(text)) - 12;
         ++i) {
        length += snprintf_P(text + length, sizeof(text) - length, PSTR(" 0x%08x"),
                             static_cast<unsigned>(record.code[i]));
    }
    return String(text);
}
