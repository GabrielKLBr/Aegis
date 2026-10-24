#pragma once
#include "guardian/Graphics.h"
uint32_t GraphicsSyscall(uint64_t, uint64_t) noexcept;
void GraphicsSelfTest(void (*)(bool, uint32_t) noexcept) noexcept;
void GraphicsPublish() noexcept;
void GraphicsInitialize() noexcept;
void GraphicsComplete() noexcept;
uint32_t GraphicsExecute(guardian::graphics::Command&,guardian::graphics::Event&) noexcept;
extern "C" uint64_t GraphicsRunUser() noexcept;
