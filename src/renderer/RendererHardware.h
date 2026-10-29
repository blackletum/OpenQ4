// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <string_view>

// Active-device facts, not a performance score. Unknown is deliberately useful:
// neither an OS adapter inventory nor shared RAM establishes dedicated VRAM.
enum class RendererGpuClass { Unknown, Integrated, Discrete, Virtual, Software };
inline bool RendererNameContains(std::string_view name, std::string_view token) noexcept {
 if (token.size() > name.size()) return false;
 const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};
 for(size_t i=0;i+token.size()<=name.size();++i){
  size_t j=0;for(;j<token.size()&&lower(name[i+j])==lower(token[j]);++j){}
  if(j==token.size())return true;
 }
 return false;
}
inline RendererGpuClass RendererClassifyGLDevice(std::string_view name) noexcept {
 const auto has=[&](std::string_view token){return RendererNameContains(name,token);};
 if(has("llvmpipe")||has("softpipe")||has("swiftshader")||has("software rasterizer")||
    has("gdi generic")||has("basic render driver")||has("lavapipe"))return RendererGpuClass::Software;
 if(has("virgl")||has("svga3d")||has("virtualbox")||has("parallels"))return RendererGpuClass::Virtual;
 // Integrated names are checked before vendor/family names (including ANGLE).
 if(has("apple m")||has("apple gpu")||has("iris")||has("uhd graphics")||has("hd graphics")||
    has("intel(r) graphics")||has("radeon(tm) graphics")||has("radeon graphics")||
    has("radeon vega")||has("radeon(tm) vega")||has("adreno")||has("mali")||
    has("powervr")||has("videocore")||has("v3d")||has("tegra")||
    has("radeon r7 graphics")||has("radeon r5 graphics")||
    has("radeon(tm) r7 graphics")||has("radeon(tm) r5 graphics")||
    has("radeon 610m")||has("radeon 660m")||has("radeon 680m")||
    has("radeon 740m")||has("radeon 760m")||has("radeon 780m")||
    has("radeon 820m")||has("radeon 840m")||has("radeon 860m")||
    has("radeon 880m")||has("radeon 890m")||has("arc(tm) graphics")||
    has("arc graphics")||has("arc(tm) 1")||has("arc(tm) 2"))return RendererGpuClass::Integrated;
 if(has("geforce")||has("quadro")||has("nvidia rtx")||has("radeon rx")||
    has("radeon pro")||has("radeon r9")||has("radeon r7")||
    has("arc(tm) a")||has("arc(tm) b"))return RendererGpuClass::Discrete;
 return RendererGpuClass::Unknown;
}
inline const char* RendererGpuClassName(RendererGpuClass type) noexcept {
 switch(type){
  case RendererGpuClass::Integrated:return "integrated/shared";
  case RendererGpuClass::Discrete:return "discrete";
  case RendererGpuClass::Virtual:return "virtual";
  case RendererGpuClass::Software:return "software";
  default:return "unknown";
 }
}
