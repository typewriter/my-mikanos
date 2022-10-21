#pragma once

#include <stdint.h>

extern "C"
{
    void IoOut32(uint16_t addr, uint32_t data);
    uint32_t IoIn32(uint16_t addr);
    uint16_t GetCS(void);
    uint64_t GetCR3();
    void LoadIDT(uint16_t limit, uint64_t offset);
    void LoadGDT(uint16_t limit, uint64_t offset);
    void SetDSAll(uint16_t value);
    void SetCSSS(uint16_t cs, uint16_t ss);
    void SetCR3(uint64_t value);
    // I/O 書き込み
    void IoOutb(uint8_t port, uint8_t data);
    // I/O 読み込み
    uint8_t IoInb(uint8_t port);

    void CallApp(int argc, char **argv, uint16_t cs, uint16_t ss, uint64_t rip, uint64_t rsp);

    void LoadTR(uint16_t sel);
    void IntHandlerLAPICTimer();

    void SwitchContext(void *next_ctx, void *current_ctx);
    void RestoreContext(void *task_context);
}