#include "util_env.h"

 #include <array>
#include <filesystem>
#include <SDL.h>
#include <limits.h>

namespace dxvk::env {

  std::string getExePath() {
    char* base = SDL_GetBasePath();
    std::string result = base ? base : "";
    SDL_free(base);
    return result + "Skydream";
  }
  
  
  void setThreadName(const std::string& name) {
  }


  bool createDirectory(const std::string& path) {
    return std::filesystem::create_directories(path);
  }

}