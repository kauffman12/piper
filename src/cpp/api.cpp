#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include "piper.hpp"

using namespace std;

std::map<std::string, piper::Voice> voices;
std::mutex voiceMutex;

// make client specify full path to espeak data
extern "C" __declspec(dllexport) void initialize(const char* espeakDataPath) {
  // set debug
  //spdlog::set_level(spdlog::level::debug);
  
  if (!espeakDataPath) {
    spdlog::error("espeak data path is null");
    return;
  }

  // enable espeak just incase some voices need it
  piper::PiperConfig piperConfig;
  piperConfig.useESpeak = true;
  piperConfig.eSpeakDataPath = std::string(espeakDataPath);
  piper::initialize(piperConfig);
}

extern "C" __declspec(dllexport) void release() {
  piper::PiperConfig piperConfig;
  // set to make sure espeak terminate is called
  piperConfig.useESpeak = true;
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

// make client specify full paths and manage their own user ids
extern "C" __declspec(dllexport) int loadVoice(const char* id, const char* modelPath, const char* configPath) {
  std::lock_guard<std::mutex> lock(voiceMutex);
  
  if (!id) {
    spdlog::error("id is null.");
    return -1;
  }

  auto stringId = std::string(id);
  auto it = voices.find(stringId);
  if (it != voices.end()) {
    voices.erase(it);
  } 

  piper::Voice voice;
  if (loadVoiceInternal(modelPath, configPath, voice)) {
    voices[stringId] = std::move(voice);
    return 0;
  }
  return -1;
}

extern "C" __declspec(dllexport) int removeVoice(const char* id) {
  std::lock_guard<std::mutex> lock(voiceMutex);
  
  if (!id) {
    spdlog::error("id is null.");
    return -1;
  }

  auto stringId = std::string(id);
  auto it = voices.find(stringId);
  if (it != voices.end()) {
    voices.erase(it);
  } 

  return 0;
}

extern "C" __declspec(dllexport) long synthesize(const char* id, const char *text, int16_t** buffer) {
  spdlog::debug("synthesizing for {}, {}", id, text);
  
  if (!text || !id) {
    spdlog::error("text to speak or id is null.");
    return -1;
  }
   
  try {
    std::lock_guard<std::mutex> lock(voiceMutex);
    
    auto stringId = std::string(id);
    auto it = voices.find(stringId);
    if (it == voices.end()) {
      spdlog::error("voice ID {} not found.", stringId);
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