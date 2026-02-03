/* GitHub Copilot LLM Provider Implementation */

#include "copilot_provider.h"
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

// Structure to hold response data from curl
struct ResponseData {
  std::string data;
};

// Callback function for curl to write response data
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t total_size = size * nmemb;
  ResponseData *resp = (ResponseData *)userp;
  resp->data.append((char *)contents, total_size);
  return total_size;
}

CopilotProvider::CopilotProvider() 
    : model_name_("gpt-4o") {
}

CopilotProvider::~CopilotProvider() {
}

bool CopilotProvider::initialize() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Load configuration if available
    load_config();
    
    return true;
}

void CopilotProvider::cleanup() {
    curl_global_cleanup();
}

std::string CopilotProvider::get_provider_name() const {
    return "GitHub Copilot";
}

std::string CopilotProvider::get_model_name() const {
    return model_name_;
}

void CopilotProvider::set_model(const std::string& model_name) {
    model_name_ = model_name;
}

std::string CopilotProvider::get_llm_dir_path() {
  const char *home = getenv("HOME");
  if (!home) {
    home = "/root";
  }
  return std::string(home) + "/.bash_llm";
}

std::string CopilotProvider::get_auth_file_path() {
  return get_llm_dir_path() + "/copilot_auth.json";
}

std::string CopilotProvider::get_config_file_path() {
  return get_llm_dir_path() + "/config.json";
}

bool CopilotProvider::load_config() {
    std::string config_file = get_config_file_path();
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        // Config file doesn't exist, use defaults
        return true;
    }
    
    try {
        json config_data;
        file >> config_data;
        
        // Read from the "copilot" section of the config
        if (config_data.contains("copilot") && config_data["copilot"].is_object()) {
            json copilot_config = config_data["copilot"];
            
            if (copilot_config.contains("model")) {
                model_name_ = copilot_config["model"].get<std::string>();
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to parse copilot config: " << e.what() << "\n";
        return false;
    }
}

time_t CopilotProvider::parse_token_expiration(const std::string& copilot_token) {
  size_t exp_pos = copilot_token.find("exp=");
  if (exp_pos == std::string::npos) {
    return 0;
  }
  
  size_t exp_start = exp_pos + 4;
  size_t exp_end = copilot_token.find(';', exp_start);
  
  std::string exp_str;
  if (exp_end == std::string::npos) {
    exp_str = copilot_token.substr(exp_start);
  } else {
    exp_str = copilot_token.substr(exp_start, exp_end - exp_start);
  }
  
  try {
    return (time_t)std::stol(exp_str);
  } catch (...) {
    return 0;
  }
}

bool CopilotProvider::load_credentials(std::string& copilot_token, std::string& access_token, time_t& expires_at) {
  std::string auth_file = get_auth_file_path();
  std::ifstream file(auth_file);
  
  if (!file.is_open()) {
    return false;
  }
  
  try {
    json auth_data;
    file >> auth_data;
    
    if (!auth_data.contains("copilot_token") || !auth_data.contains("access_token") || 
        !auth_data.contains("expires_at")) {
      return false;
    }
    
    copilot_token = auth_data["copilot_token"].get<std::string>();
    access_token = auth_data["access_token"].get<std::string>();
    
    if (auth_data["expires_at"].is_number()) {
      expires_at = auth_data["expires_at"].get<time_t>();
    } else if (auth_data["expires_at"].is_string()) {
      expires_at = (time_t)std::stol(auth_data["expires_at"].get<std::string>());
    } else {
      return false;
    }
    
    return true;
  } catch (const json::exception& e) {
    return false;
  }
}

bool CopilotProvider::save_credentials(const std::string& copilot_token, const std::string& access_token, time_t expires_at) {
  std::string auth_file = get_auth_file_path();

  if (mkdir(get_llm_dir_path().c_str(), 0700) != 0 && errno != EEXIST) {
    std::cerr << "Error: Cannot create directory " << get_llm_dir_path() << "\n";
    return false;
  }
  
  json auth_data = {
    {"copilot_token", copilot_token},
    {"access_token", access_token},
    {"expires_at", expires_at}
  };
  
  std::ofstream file(auth_file);
  if (!file.is_open()) {
    std::cerr << "Error: Cannot write to " << auth_file << "\n";
    return false;
  }
  
  file << auth_data.dump();
  file.close();
  
  chmod(auth_file.c_str(), 0600);
  
  return true;
}

bool CopilotProvider::refresh_copilot_token(std::string& copilot_token, const std::string& access_token, time_t& expires_at) {
  CURL *curl;
  CURLcode res;
  ResponseData response;
  long http_code = 0;
  
  curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize curl for token refresh\n";
    return false;
  }
  
  std::string auth_header = "Authorization: token " + access_token;
  
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "User-Agent: llm-builtin/1.0");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, auth_header.c_str());
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/copilot_internal/v2/token");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  res = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    std::cerr << "Failed to refresh token: " << curl_easy_strerror(res) << "\n";
    return false;
  }
  
  if (http_code != 200) {
    std::cerr << "Error: GitHub API returned HTTP " << http_code << "\n";
    std::cerr << "Response: " << response.data << "\n";
    return false;
  }
  
  try {
    json response_obj = json::parse(response.data);
    
    if (!response_obj.contains("token")) {
      std::cerr << "Error: Failed to get Copilot token\n";
      if (response_obj.contains("message")) {
        std::cerr << "GitHub API response: " << response_obj["message"].get<std::string>() << "\n";
      }
      return false;
    }
    
    copilot_token = response_obj["token"].get<std::string>();
    
    if (response_obj.contains("expires_at")) {
      expires_at = response_obj["expires_at"].get<time_t>();
    } else {
      expires_at = time(nullptr) + 3600;
    }
    
    return save_credentials(copilot_token, access_token, expires_at);
  } catch (const json::exception& e) {
    std::cerr << "Error parsing token response: " << e.what() << "\n";
    std::cerr << "Response was: " << response.data << "\n";
    return false;
  }
}

bool CopilotProvider::get_device_code(std::string& device_code, std::string& user_code, 
                                       std::string& verification_uri, int& interval) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize curl\n";
    return false;
  }
  
  ResponseData response;
  const char* client_id = "Iv1.b507a08c87ecfe98";
  
  std::string payload = "{\"client_id\":\"" + std::string(client_id) + "\",\"scope\":\"read:user\"}";
  
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Content-Type: application/json");
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://github.com/login/device/code");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  
  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    std::cerr << "Failed to get device code: " << curl_easy_strerror(res) << "\n";
    return false;
  }
  
  try {
    json response_obj = json::parse(response.data);
    
    if (!response_obj.contains("device_code")) {
      std::cerr << "Error: Failed to get device code\n";
      return false;
    }
    
    device_code = response_obj["device_code"].get<std::string>();
    user_code = response_obj["user_code"].get<std::string>();
    verification_uri = response_obj["verification_uri"].get<std::string>();
    interval = response_obj.value("interval", 5);
    
    return true;
  } catch (const json::exception& e) {
    std::cerr << "Error parsing device code response: " << e.what() << "\n";
    return false;
  }
}

bool CopilotProvider::poll_for_access_token(const std::string& device_code, std::string& access_token) {
  CURL *curl;
  CURLcode res;
  const char* client_id = "Iv1.b507a08c87ecfe98";
  
  int max_attempts = 120;
  int interval = 5;
  
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    sleep(interval);
    
    curl = curl_easy_init();
    if (!curl) {
      std::cerr << "Failed to initialize curl\n";
      return false;
    }
    
    ResponseData response;
    std::string payload = "{\"client_id\":\"" + std::string(client_id) + 
                          "\",\"device_code\":\"" + device_code + 
                          "\",\"grant_type\":\"urn:ietf:params:oauth:grant-type:device_code\"}";
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, "https://github.com/login/oauth/access_token");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
      continue;
    }
    
    try {
      json response_obj = json::parse(response.data);
      
      if (response_obj.contains("access_token") && !response_obj["access_token"].is_null()) {
        access_token = response_obj["access_token"].get<std::string>();
        return true;
      }
      
      std::string error = response_obj.value("error", "");
      if (error == "authorization_pending") {
        std::cerr << ".";
        std::cerr.flush();
        continue;
      } else if (error == "slow_down") {
        interval += 5;
        std::cerr << ".";
        std::cerr.flush();
        continue;
      } else if (!error.empty()) {
        std::cerr << "\nError: " << error << "\n";
        return false;
      }
    } catch (const json::exception& e) {
      continue;
    }
  }
  
  std::cerr << "\nTimeout waiting for authorization\n";
  return false;
}

bool CopilotProvider::authenticate_with_github(std::string& copilot_token, std::string& access_token, time_t& expires_at) {
  std::cerr << "\n[Step 1/4] Requesting device code from GitHub...\n";
  
  std::string device_code, user_code, verification_uri;
  int interval = 5;
  
  if (!get_device_code(device_code, user_code, verification_uri, interval)) {
    return false;
  }
  
  std::cerr << "\n[Step 2/4] Authorization required\n";
  std::cerr << "==================================\n\n";
  std::cerr << "Please visit: " << verification_uri << "\n";
  std::cerr << "And enter code: " << user_code << "\n\n";
  std::cerr << "Waiting for authorization";
  std::cerr.flush();
  
  if (!poll_for_access_token(device_code, access_token)) {
    return false;
  }
  
  std::cerr << "\n✓ GitHub access token obtained\n\n";
  std::cerr << "[Step 3/4] Fetching GitHub Copilot token...\n";
  
  CURL *curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize curl\n";
    return false;
  }
  
  ResponseData response;
  std::string auth_header = "Authorization: token " + access_token;
  
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "User-Agent: llm-builtin/1.0");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, auth_header.c_str());
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/copilot_internal/v2/token");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  
  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    std::cerr << "Failed to get Copilot token: " << curl_easy_strerror(res) << "\n";
    return false;
  }
  
  try {
    json response_obj = json::parse(response.data);
    
    if (!response_obj.contains("token") || response_obj["token"].is_null()) {
      std::cerr << "Error: Failed to get Copilot token\n";
      if (response_obj.contains("message")) {
        std::cerr << "GitHub API response: " << response_obj["message"].get<std::string>() << "\n";
      }
      std::cerr << "Note: You need an active GitHub Copilot subscription\n";
      return false;
    }
    
    copilot_token = response_obj["token"].get<std::string>();
    
    if (response_obj.contains("expires_at")) {
      expires_at = response_obj["expires_at"].get<time_t>();
    } else {
      expires_at = time(nullptr) + 3600;
    }
    
    std::cerr << "✓ GitHub Copilot token obtained\n\n";
    
    return true;
  } catch (const json::exception& e) {
    std::cerr << "Error parsing Copilot token response: " << e.what() << "\n";
    return false;
  }
}

bool CopilotProvider::get_copilot_token(std::string& copilot_token) {
  std::string access_token;
  time_t expires_at;
  
  if (load_credentials(copilot_token, access_token, expires_at)) {
    time_t now = time(nullptr);
    time_t token_expiration = parse_token_expiration(copilot_token);
    time_t actual_expiration = (token_expiration > 0) ? token_expiration : expires_at;
    
    if (now < actual_expiration - 300) {
      return true;
    }
    
    if (refresh_copilot_token(copilot_token, access_token, expires_at)) {
      return true;
    } else {
      if (authenticate_with_github(copilot_token, access_token, expires_at)) {
        return save_credentials(copilot_token, access_token, expires_at);
      }
      return false;
    }
  }
  
  std::cerr << "\n=== First Time Setup ===\n";
  std::cerr << "No GitHub Copilot credentials found. Starting authentication...\n";
  
  if (authenticate_with_github(copilot_token, access_token, expires_at)) {
    std::cerr << "[Step 4/4] Saving credentials...\n";
    if (save_credentials(copilot_token, access_token, expires_at)) {
      std::cerr << "✓ Credentials saved to ~/.bash_llm/copilot_auth.json\n\n";
      return true;
    }
  }
  
  return false;
}

bool CopilotProvider::authenticate() {
  std::string token;
  return get_copilot_token(token);
}

std::string CopilotProvider::extract_content_from_sse(const std::string& response, json* tool_calls = nullptr) {
  std::stringstream ss(response);
  std::string line;
  std::string full_content;
  
  // Map to accumulate tool calls by index
  std::map<int, json> tool_calls_map;
  int next_tool_index = 0;

  auto merge_tool_calls = [&](const json& tool_calls_array) {
    if (!tool_calls_array.is_array()) {
      return;
    }
    for (const auto& call : tool_calls_array) {
      int idx = call.contains("index") ? call["index"].get<int>() : next_tool_index++;

      if (tool_calls_map.find(idx) == tool_calls_map.end()) {
        tool_calls_map[idx] = {
          {"id", ""},
          {"type", "function"},
          {"function", {
            {"name", ""},
            {"arguments", ""}
          }}
        };
      }

      if (call.contains("id")) {
        tool_calls_map[idx]["id"] = call["id"];
      }
      if (call.contains("type")) {
        tool_calls_map[idx]["type"] = call["type"];
      }
      if (call.contains("function")) {
        const auto& func = call["function"];
        if (func.contains("name")) {
          tool_calls_map[idx]["function"]["name"] = func["name"];
        }
        if (func.contains("arguments")) {
          std::string args = func["arguments"].get<std::string>();
          std::string existing = tool_calls_map[idx]["function"]["arguments"].is_string()
            ? tool_calls_map[idx]["function"]["arguments"].get<std::string>()
            : std::string();
          tool_calls_map[idx]["function"]["arguments"] = existing + args;
        }
      }
    }
  };
  
  while (std::getline(ss, line)) {
    if (line.find("data: ") == 0) {
      std::string json_data = line.substr(6);
      
      if (json_data == "[DONE]") {
        break;
      }
      
      try {
        json response_obj = json::parse(json_data);
        if (response_obj.contains("choices") && response_obj["choices"].is_array() && 
            response_obj["choices"].size() > 0) {
          const auto& choice = response_obj["choices"][0];
          
          // Extract content
          if (choice.contains("delta") && choice["delta"].contains("content")) {
            full_content += choice["delta"]["content"].get<std::string>();
          }
          
          // Extract tool_calls from delta (streaming)
          if (choice.contains("delta")) {
            if (choice["delta"].contains("tool_calls")) {
              const auto& delta_tool_calls = choice["delta"]["tool_calls"];
              merge_tool_calls(delta_tool_calls);
            }
          }
          
          // Check for finish_reason indicating tool use
          if (choice.contains("finish_reason") && choice["finish_reason"] == "tool_calls") {
            if (choice.contains("message") && choice["message"].contains("tool_calls")) {
              // If we have the complete tool_calls in the message, use that
              const auto& msg_tool_calls = choice["message"]["tool_calls"];
              if (msg_tool_calls.is_array()) {
                tool_calls_map.clear();
                merge_tool_calls(msg_tool_calls);
              }
            }
          }
        }
      } catch (const json::exception& e) {
        continue;
      }
    }
  }
  
  // Fallback: scan raw response for tool_calls if streaming parse missed it
  if (tool_calls_map.empty()) {
    size_t pos = 0;
    while ((pos = response.find("\"tool_calls\"", pos)) != std::string::npos) {
      size_t array_start = response.find('[', pos);
      if (array_start == std::string::npos) {
        break;
      }
      int depth = 0;
      size_t array_end = std::string::npos;
      for (size_t i = array_start; i < response.size(); ++i) {
        if (response[i] == '[') {
          depth++;
        } else if (response[i] == ']') {
          depth--;
          if (depth == 0) {
            array_end = i;
            break;
          }
        }
      }
      if (array_end != std::string::npos) {
        std::string array_text = response.substr(array_start, array_end - array_start + 1);
        try {
          json parsed_array = json::parse(array_text);
          merge_tool_calls(parsed_array);
        } catch (const json::exception&) {
          // ignore and continue
        }
        pos = array_end + 1;
      } else {
        break;
      }
    }
  }

  // Convert map to array
  if (tool_calls && !tool_calls_map.empty()) {
    json extracted_tool_calls = json::array();
    for (const auto& [idx, call] : tool_calls_map) {
      extracted_tool_calls.push_back(call);
    }
    *tool_calls = extracted_tool_calls;
  }
  
  return full_content;
}

bool CopilotProvider::send_message(
    const std::string& message,
    const std::vector<json>& history,
    const std::string& system_message,
    const json& tools,
    std::string& response,
    json* tool_calls) {
  
  std::string token;
  if (!get_copilot_token(token)) {
    return false;
  }
  
  CURL *curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize curl\n";
    return false;
  }
  
  ResponseData curl_response;
  json messages = json::array();
  
  if (!system_message.empty()) {
    messages.push_back({
      {"role", "system"},
      {"content", system_message}
    });
  }
  
  for (const auto& msg : history) {
    messages.push_back(msg);
  }
  
  messages.push_back({
    {"role", "user"},
    {"content", message}
  });

  json payload_obj = {
    {"messages", messages},
    {"model", model_name_},
    {"stream", true}
  };
  
  // Add tools if provided
  if (tools.is_array() && tools.size() > 0) {
    payload_obj["tools"] = tools;
  }
  
  std::string payload = payload_obj.dump();
  
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth_header = "Authorization: Bearer " + token;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, "Editor-Version: vscode/1.85.0");
  headers = curl_slist_append(headers, "Editor-Plugin-Version: copilot-chat/0.12.0");
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://api.githubcopilot.com/chat/completions");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curl_response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  CURLcode res = curl_easy_perform(curl);
  
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
    return false;
  }
  
  response = extract_content_from_sse(curl_response.data, tool_calls);
  
  // Check if we got either content or tool_calls
  bool has_content = !response.empty();
  bool has_tool_calls = tool_calls && !tool_calls->is_null() && !tool_calls->empty();
  
  if (!has_content && !has_tool_calls) {
    std::cerr << "Error: No content or tool_calls received\n";
    std::cerr << "Response: " << curl_response.data << "\n";
    return false;
  }
  
  return true;
}
