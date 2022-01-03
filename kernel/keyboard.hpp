#pragma once

#include <deque>
#include "interrupt.hpp"

void InitializeKeyboard(std::deque<Message>& msg_queue);