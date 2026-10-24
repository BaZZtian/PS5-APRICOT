// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
// Own-title paths only. Source is already ROM-bound, hash-verified APAU0001.
bool prepare_shell_music(const std::vector<uint8_t>& pcm,const std::string& persistent,
                         const std::string& app,const std::string& cache,const char* title,std::string& error);
