#pragma once
#include "guardian/Graphics.h"
uint32_t GraphicsSyscall(uint64_t, uint64_t) noexcept;
void GraphicsSelfTest(void (*)(bool, uint32_t) noexcept) noexcept;
void GraphicsPublish() noexcept;
extern "C" uint64_t GraphicsRunUser() noexcept;
