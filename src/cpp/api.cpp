#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include "piper.hpp"

using namespace std;

std::map<int, piper::Voice> voices;
std::mutex voiceMutex;
int nextId = 1;

std::string getEspeakPath() {
#ifdef _MSC_VER
  auto exePath = []() {
    wchar_t moduleFileName[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, moduleFileName, std::size(moduleFileName));
    return filesystem::path(moduleFileName);
  }();
#else
#ifdef __APPLE__
  auto exePath = []() {
    char moduleFileName[PATH_MAX] = {0};
    uint32_t moduleFileNameSize = std::size(moduleFileName);
    _NSGetExecutablePath(moduleFileName, &moduleFileNameSize);
    return filesystem::path(moduleFileName);
  }();
#else
  auto exePath = filesystem::canonical("/proc/self/exe");
#endif
#endif
  return std::filesystem::absolute(exePath.parent_path().append("espeak-ng-data")).string();
}
 
extern "C" __declspec(dllexport) void initialize() {
  // set debug
  // spdlog::set_level(spdlog::level::debug);

  // enable espeak just incase some voices need it
  piper::PiperConfig piperConfig;
  piperConfig.useESpeak = true;
  piperConfig.eSpeakDataPath = getEspeakPath();
  piper::initialize(piperConfig);
}

extern "C" __declspec(dllexport) void release() {
  piper::PiperConfig piperConfig;
  piperConfig.useESpeak = true;
  piperConfig.eSpeakDataPath = getEspeakPath();
  // cleanup
  voices.clear();
  piper::terminate(piperConfig);
}

bool loadVoiceInternal(const char* modelPath, const char* configPath, piper::Voice& voice) {
  if (!modelPath || !configPath) {
    spdlog::error("model path or config path is null.");
    return false;
  }

  try {
    // default speaker setup
    std::optional<piper::SpeakerId> speakerId;
    piper::PiperConfig piperConfig;
    piper::loadVoice(piperConfig, std::string(modelPath), std::string(configPath), voice, speakerId, false);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("failed to load voice: {}", e.what());
    return false;
  }
}

extern "C" __declspec(dllexport) int loadVoice(const char* modelPath, const char* configPath) {
  std::lock_guard<std::mutex> lock(voiceMutex);

  int id = nextId++;
  piper::Voice voice;
  if (loadVoiceInternal(modelPath, configPath, voice)) {
    voices[id] = std::move(voice);
    return id;
  }
  return -1;
}

extern "C" __declspec(dllexport) int updateVoice(int id, const char* modelPath, const char* configPath) {
  std::lock_guard<std::mutex> lock(voiceMutex);

  auto it = voices.find(id);
  if (it == voices.end()) {
    spdlog::error("voice ID {} not found.", id);
    return -1;
  }
  piper::Voice voice;
  if (loadVoiceInternal(modelPath, configPath, voice)) {
    voices[id] = std::move(voice);
    return id;
  }
  return -1;
}

extern "C" __declspec(dllexport) long synthesize(int id, const char *text, int16_t** buffer) {
  spdlog::debug("synthesizing for {}, {}", id, text);
  
  if (!text) {
    spdlog::error("text to speak is null.");
    return -1;
  }
   
  try {
    std::lock_guard<std::mutex> lock(voiceMutex);
    
    auto it = voices.find(id);
    if (it == voices.end()) {
      spdlog::error("voice ID {} not found.", id);
      return -1; // error
    }
    
    piper::SynthesisResult result;
    vector<int16_t> audio;  
    // config is required but not really used
    piper::PiperConfig piperConfig;
    
    // create audio
    piper::textToAudio(piperConfig, it->second, text, audio, result, NULL);
    
    // allocate buffer
    auto size = audio.size();
    *buffer = new int16_t[size];
    std::memcpy(*buffer, audio.data(), size * sizeof(int16_t));

    return (long)size;
  } catch (const std::exception& e) {
    spdlog::error("failed to synthesize: {}", e.what());
    return -1; // error
  }
}

extern "C" __declspec(dllexport) void freeAudioData(int16_t* buffer) {
  delete[] buffer;
}