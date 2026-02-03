/* LiteLLM Gateway Provider Implementation */

#include "litellm_provider.h"
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <sstream>
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

LiteLLMProvider::LiteLLMProvider() 
    : base_url_("http://localhost:8000"), 
      model_name_("gpt-3.5-turbo"),
      api_token_("") {
}

LiteLLMProvider::~LiteLLMProvider() {
}

bool LiteLLMProvider::initialize() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Load configuration if available
    load_config();
    
    return true;
}

void LiteLLMProvider::cleanup() {
    curl_global_cleanup();
}

std::string LiteLLMProvider::get_provider_name() const {
    return "LiteLLM";
}

std::string LiteLLMProvider::get_model_name() const {
    return model_name_;
}

std::string LiteLLMProvider::get_llm_dir_path() {
  const char *home = getenv("HOME");
  if (!home) {
    home = "/root";
  }
  return std::string(home) + "/.bash_llm";
}

std::string LiteLLMProvider::get_config_file_path() {
  return get_llm_dir_path() + "/config.json";
}

std::string LiteLLMProvider::get_base_url() {
    return base_url_;
}

std::string LiteLLMProvider::get_model_name() {
    return model_name_;
}

std::string LiteLLMProvider::get_api_token() {
    return api_token_;
}

bool LiteLLMProvider::load_config() {
    std::string config_file = get_config_file_path();
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        // Config file doesn't exist, use defaults
        return true;
    }
    
    try {
        json config_data;
        file >> config_data;
        
        // Read from the "litellm" section of the config
        if (config_data.contains("litellm") && config_data["litellm"].is_object()) {
            json litellm_config = config_data["litellm"];
            
            if (litellm_config.contains("base_url")) {
                base_url_ = litellm_config["base_url"].get<std::string>();
            }
            
            if (litellm_config.contains("model")) {
                model_name_ = litellm_config["model"].get<std::string>();
            }
            
            if (litellm_config.contains("api_key")) {
                api_token_ = litellm_config["api_key"].get<std::string>();
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to parse litellm config: " << e.what() << "\n";
        return false;
    }
}

bool LiteLLMProvider::check_server_available() {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    ResponseData response;
    std::string url = base_url_ + "/health";
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && http_code == 200);
}

bool LiteLLMProvider::authenticate() {
    // LiteLLM is assumed to be a local gateway
    // Just check if the server is available
    
    if (!check_server_available()) {
        std::cerr << "Error: LiteLLM server is not available at " << base_url_ << "\n";
        std::cerr << "Please ensure litellm is running.\n";
        return false;
    }
    
    return true;
}

bool LiteLLMProvider::send_message(
    const std::string& message,
    const std::vector<json>& history,
    const std::string& system_message,
    std::string& response) {
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize curl\n";
        return false;
    }
    
    // Build the request payload
    json payload;
    payload["model"] = model_name_;
    
    // Build messages array
    json messages = json::array();
    
    // Add system message if provided
    if (!system_message.empty()) {
        json sys_msg;
        sys_msg["role"] = "system";
        sys_msg["content"] = system_message;
        messages.push_back(sys_msg);
    }
    
    // Add conversation history
    for (const auto& hist_msg : history) {
        messages.push_back(hist_msg);
    }
    
    // Add current user message
    json user_msg;
    user_msg["role"] = "user";
    user_msg["content"] = message;
    messages.push_back(user_msg);
    
    payload["messages"] = messages;
    
    // Optional parameters
    payload["stream"] = false;
    payload["temperature"] = 0.7;
    
    std::string json_str = payload.dump();
    
    // Set up the request
    ResponseData resp;
    std::string url = base_url_ + "/v1/chat/completions";
    
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // Add authorization header if token is configured
    if (!api_token_.empty()) {
        std::string auth_header = "Authorization: Bearer " + api_token_;
        headers = curl_slist_append(headers, auth_header.c_str());
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "Request failed: " << curl_easy_strerror(res) << "\n";
        return false;
    }
    
    if (http_code != 200) {
        std::cerr << "HTTP error " << http_code << ": " << resp.data << "\n";
        return false;
    }
    
    // Parse the response
    try {
        json response_json = json::parse(resp.data);
        
        if (!response_json.contains("choices") || response_json["choices"].empty()) {
            std::cerr << "Invalid response format: no choices\n";
            return false;
        }
        
        const auto& first_choice = response_json["choices"][0];
        if (!first_choice.contains("message") || !first_choice["message"].contains("content")) {
            std::cerr << "Invalid response format: no message content\n";
            return false;
        }
        
        response = first_choice["message"]["content"].get<std::string>();
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse response: " << e.what() << "\n";
        std::cerr << "Response was: " << resp.data << "\n";
        return false;
    }
}
