// SPDX-License-Identifier: GPL-3.0-or-later
// The same GPU pipeline is exercised by the PC integration test and PS5 player.
#pragma once
#include "bus.h"
#include "geometry_gpu.h"
#include "video_state.h"
#include "vulkan_tiles.h"
struct NativeGameVideo {
    VulkanTiles tiles,geometry;std::vector<uint32_t> geometry_pixels=std::vector<uint32_t>(256*384),pixels;
    uint64_t geometry_frame=UINT64_MAX;std::string error;
    std::vector<uint8_t> previous_geometry,previous_tiles;
    bool initialize(PFN_vkGetInstanceProcAddr loader,const uint32_t *tiles_shader,size_t tiles_bytes,const uint32_t *geometry_shader,size_t geometry_bytes,const uint32_t *resolve_shader,size_t resolve_bytes) {
        if(!tiles.initialize(loader,tiles_shader,tiles_bytes)) {error=tiles.error;return false;}
        if(!geometry.initialize(loader,geometry_shader,geometry_bytes,resolve_shader,resolve_bytes)) {error=geometry.error;return false;}
        return true;
    }
    bool render(const Bus &bus) {
        if(!bus.raster.ready) {error="no completed native frame";return false;}
        if(bus.raster_geometry_frame!=geometry_frame) {
            std::vector<uint8_t> state;const char *why=nullptr;uint32_t flags=0;
            if(!prepare_geometry_gpu(bus.raster_geometry,state,why)) {error=why;return false;}
            if(state!=previous_geometry) {
                if(!geometry.render(state,geometry_pixels,flags)) {error=geometry.error;return false;}
                if(!flags)previous_geometry=state;
            }
            if(flags) {error="unsupported geometry flags="+std::to_string(flags);return false;}
            geometry_frame=bus.raster_geometry_frame;
        }
        auto state=pack_video_state(&bus.display.engine[0][0],bus.vram.control,bus.raster.vram.data(),bus.raster.palette.data(),bus.raster.oam.data(),bus.raster.rows.data(),geometry_pixels.data());
        uint32_t flags=0;
        if(state==previous_tiles&&!pixels.empty())return true;
        if(!tiles.render(state,pixels,flags)) {error=tiles.error;return false;}
        if(flags) {error="unsupported display flags="+std::to_string(flags);return false;}
        previous_tiles=std::move(state);return true;
    }
};
