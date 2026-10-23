#pragma once
#include "preparation_session.h"
#include "game_video.h"
namespace apricot::aot {
// Build a complete private edition transactionally, including title media.
// Destination must not exist. Existing bundles/saves are never modified.
bool prepare_edition(const char*rom,const char*destination,NativeGameVideo&,
    PreparationCallback,void*,std::string&error);
// Preparation continuation used after the native compiler has completed.
// The loader independently enforces module/ROM identity before title execution.
bool finish_prepared_edition(const PreparedRom&,Result&&,const char*destination,
    NativeGameVideo&,PreparationCallback,void*,std::string&error);
}
