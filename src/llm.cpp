/* LLM Chat builtin - Interact with GitHub Copilot */

#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern "C" {
#include "builtins.h"
#include "shell.h"
#include "bashgetopt.h"
#include "common.h"
extern char* ttyname(int fd);
}

// Structure to hold response data from curl
struct ResponseData {
  std::string data;
};

// In-memory chat history for the current shell session
static std::vector<json> g_chat_history;

// Callback function for curl to write response data
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t total_size = size * nmemb;
  ResponseData *resp = (ResponseData *)userp;
  resp->data.append((char *)contents, total_size);
  return total_size;
}

// Parse expiration time from Copilot token
// Token format: tid=...;exp=1234567890;sku=...
static time_t parse_token_expiration(const std::string& copilot_token) {
  size_t exp_pos = copilot_token.find("exp=");
  if (exp_pos == std::string::npos) {
    return 0;  // No expiration found
  }
  
  size_t exp_start = exp_pos + 4;  // Skip "exp="
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

// Get base directory for builtin files
static std::string get_llm_dir_path() {
  const char *home = getenv("HOME");
  if (!home) {
    home = "/root";
  }
  return std::string(home) + "/.bash_llm";
}

// Get path to credentials file
static std::string get_auth_file_path() {
  return get_llm_dir_path() + "/copilot_auth.json";
}

// Get path to custom instructions file
static std::string get_instructions_file_path() {
  return get_llm_dir_path() + "/instructions.txt";
}

// Ensure custom instructions file exists with defaults
static void ensure_instructions_file() {
  std::string instructions_dir = get_llm_dir_path();
  std::string instructions_file = get_instructions_file_path();

  if (mkdir(instructions_dir.c_str(), 0700) != 0 && errno != EEXIST) {
    fprintf(stderr, "Warning: Cannot create directory %s\n", instructions_dir.c_str());
    return;
  }

  std::ifstream existing(instructions_file);
  if (existing.is_open()) {
    existing.close();
    return;
  }

  std::ofstream file(instructions_file);
  if (!file.is_open()) {
    fprintf(stderr, "Warning: Cannot write to %s\n", instructions_file.c_str());
    return;
  }

  file << "# Custom instructions for llm builtin\n";
  file << "# Edit this file to change default behavior for all chats.\n";
  file << "# Lines starting with # are comments.\n";
  file << "\n";
  file << "You are a bash/shell scripting expert assistant.\n";
  file << "The user is interacting with you from a command-line terminal.\n";
  file << "\n";
  file << "Response guidelines:\n";
  file << "- Provide concise, actionable answers optimized for terminal viewing\n";
  file << "- For commands: give working examples with brief explanations\n";
  file << "- Use plain text formatting (no markdown code blocks with ```)\n";
  file << "- Prefer one-liners and pipelines when appropriate\n";
  file << "- Include safety warnings for destructive operations\n";
  file << "- Assume Linux/Unix environment unless specified otherwise\n";
  file.close();

  chmod(instructions_file.c_str(), 0600);
}

static std::string trim_whitespace(const std::string& input) {
  size_t start = input.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = input.find_last_not_of(" \t\n\r");
  return input.substr(start, end - start + 1);
}

static std::string load_instructions() {
  std::string instructions_file = get_instructions_file_path();
  std::ifstream file(instructions_file);
  if (!file.is_open()) {
    return "";
  }

  std::ostringstream ss;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line[0] == '#') {
      continue;
    }
    ss << line << "\n";
  }

  return trim_whitespace(ss.str());
}

// Load credentials from ~/.bash_llm/copilot_auth.json
static bool load_credentials(std::string& copilot_token, std::string& access_token, time_t& expires_at) {
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
    
    // Handle expires_at as either integer or string
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

// Save credentials to ~/.bash_llm/copilot_auth.json
static bool save_credentials(const std::string& copilot_token, const std::string& access_token, time_t expires_at) {
  std::string auth_file = get_auth_file_path();

  if (mkdir(get_llm_dir_path().c_str(), 0700) != 0 && errno != EEXIST) {
    fprintf(stderr, "Error: Cannot create directory %s\n", get_llm_dir_path().c_str());
    return false;
  }
  
  json auth_data = {
    {"copilot_token", copilot_token},
    {"access_token", access_token},
    {"expires_at", expires_at}
  };
  
  std::ofstream file(auth_file);
  if (!file.is_open()) {
    fprintf(stderr, "Error: Cannot write to %s\n", auth_file.c_str());
    return false;
  }
  
  file << auth_data.dump();
  file.close();
  
  // Set permissions to 600 (read/write for owner only)
  chmod(auth_file.c_str(), 0600);
  
  return true;
}

// Refresh copilot token using access token
static bool refresh_copilot_token(std::string& copilot_token, const std::string& access_token, time_t& expires_at) {
  CURL *curl;
  CURLcode res;
  ResponseData response;
  long http_code = 0;
  
  curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to initialize curl for token refresh\n");
    return false;
  }
  
  // Prepare authorization header
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
    fprintf(stderr, "Failed to refresh token: %s\n", curl_easy_strerror(res));
    return false;
  }
  
  // Check HTTP response code
  if (http_code != 200) {
    fprintf(stderr, "Error: GitHub API returned HTTP %ld\n", http_code);
    fprintf(stderr, "Response: %s\n", response.data.c_str());
    return false;
  }
  
  try {
    json response_obj = json::parse(response.data);
    
    if (!response_obj.contains("token")) {
      fprintf(stderr, "Error: Failed to get Copilot token\n");
      if (response_obj.contains("message")) {
        fprintf(stderr, "GitHub API response: %s\n", response_obj["message"].get<std::string>().c_str());
      }
      return false;
    }
    
    copilot_token = response_obj["token"].get<std::string>();
    
    // Use expires_at from API response if available, otherwise default to 1 hour
    if (response_obj.contains("expires_at")) {
      expires_at = response_obj["expires_at"].get<time_t>();
    } else {
      expires_at = time(nullptr) + 3600;  // Token valid for 1 hour
    }
    
    // Save updated token
    return save_credentials(copilot_token, access_token, expires_at);
  } catch (const json::exception& e) {
    fprintf(stderr, "Error parsing token response: %s\n", e.what());
    fprintf(stderr, "Response was: %s\n", response.data.c_str());
    return false;
  }
}

// Get device code for OAuth flow
static bool get_device_code(std::string& device_code, std::string& user_code, 
                             std::string& verification_uri, int& interval) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to initialize curl\n");
    return false;
  }
  
  ResponseData response;
  const char* client_id = "Iv1.b507a08c87ecfe98";  // GitHub Copilot CLI OAuth Client ID
  
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
    fprintf(stderr, "Failed to get device code: %s\n", curl_easy_strerror(res));
    return false;
  }
  
  try {
    json response_obj = json::parse(response.data);
    
    if (!response_obj.contains("device_code")) {
      fprintf(stderr, "Error: Failed to get device code\n");
      return false;
    }
    
    device_code = response_obj["device_code"].get<std::string>();
    user_code = response_obj["user_code"].get<std::string>();
    verification_uri = response_obj["verification_uri"].get<std::string>();
    interval = response_obj.value("interval", 5);
    
    return true;
  } catch (const json::exception& e) {
    fprintf(stderr, "Error parsing device code response: %s\n", e.what());
    return false;
  }
}

// Poll for access token after user authorizes
static bool poll_for_access_token(const std::string& device_code, std::string& access_token) {
  CURL *curl;
  CURLcode res;
  const char* client_id = "Iv1.b507a08c87ecfe98";
  
  int max_attempts = 120;  // 10 minutes max
  int interval = 5;
  
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    sleep(interval);
    
    curl = curl_easy_init();
    if (!curl) {
      fprintf(stderr, "Failed to initialize curl\n");
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
        fprintf(stderr, ".");
        fflush(stderr);
        continue;
      } else if (error == "slow_down") {
        interval += 5;
        fprintf(stderr, ".");
        fflush(stderr);
        continue;
      } else if (!error.empty()) {
        fprintf(stderr, "\nError: %s\n", error.c_str());
        return false;
      }
    } catch (const json::exception& e) {
      continue;
    }
  }
  
  fprintf(stderr, "\nTimeout waiting for authorization\n");
  return false;
}

// Authenticate using GitHub device flow
static bool authenticate_with_github(std::string& copilot_token, std::string& access_token, time_t& expires_at) {
  fprintf(stderr, "\n[Step 1/4] Requesting device code from GitHub...\n");
  
  std::string device_code, user_code, verification_uri;
  int interval = 5;
  
  if (!get_device_code(device_code, user_code, verification_uri, interval)) {
    return false;
  }
  
  fprintf(stderr, "\n[Step 2/4] Authorization required\n");
  fprintf(stderr, "==================================\n\n");
  fprintf(stderr, "Please visit: %s\n", verification_uri.c_str());
  fprintf(stderr, "And enter code: %s\n\n", user_code.c_str());
  fprintf(stderr, "Waiting for authorization");
  fflush(stderr);
  
  if (!poll_for_access_token(device_code, access_token)) {
    return false;
  }
  
  fprintf(stderr, "\n✓ GitHub access token obtained\n\n");
  fprintf(stderr, "[Step 3/4] Fetching GitHub Copilot token...\n");
  
  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to initialize curl\n");
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
    fprintf(stderr, "Failed to get Copilot token: %s\n", curl_easy_strerror(res));
    return false;
  }
  
  try {
    json response_obj = json::parse(response.data);
    
    if (!response_obj.contains("token") || response_obj["token"].is_null()) {
      fprintf(stderr, "Error: Failed to get Copilot token\n");
      if (response_obj.contains("message")) {
        fprintf(stderr, "GitHub API response: %s\n", response_obj["message"].get<std::string>().c_str());
      }
      fprintf(stderr, "Note: You need an active GitHub Copilot subscription\n");
      return false;
    }
    
    copilot_token = response_obj["token"].get<std::string>();
    
    // Use expires_at from API response if available, otherwise default to 1 hour
    if (response_obj.contains("expires_at")) {
      expires_at = response_obj["expires_at"].get<time_t>();
    } else {
      expires_at = time(nullptr) + 3600;  // Token valid for 1 hour
    }
    
    fprintf(stderr, "✓ GitHub Copilot token obtained\n\n");
    
    return true;
  } catch (const json::exception& e) {
    fprintf(stderr, "Error parsing Copilot token response: %s\n", e.what());
    return false;
  }
}

// Get valid copilot token, refreshing if needed, and authenticate if necessary
static bool get_copilot_token(std::string& copilot_token) {
  std::string access_token;
  time_t expires_at;
  
  // Try to load existing credentials
  if (load_credentials(copilot_token, access_token, expires_at)) {
    time_t now = time(nullptr);
    
    // Parse actual expiration from token itself
    time_t token_expiration = parse_token_expiration(copilot_token);
    
    // Use the token's actual expiration if available, otherwise use stored expires_at
    time_t actual_expiration = (token_expiration > 0) ? token_expiration : expires_at;
    
    // Check if token is expired or expiring within 5 minutes
    if (now < actual_expiration - 300) {
      // Token still valid
      return true;
    }
    
    // Token expired or expiring soon, refresh it
    if (refresh_copilot_token(copilot_token, access_token, expires_at)) {
      return true;
    } else {
      if (authenticate_with_github(copilot_token, access_token, expires_at)) {
        return save_credentials(copilot_token, access_token, expires_at);
      }
      return false;
    }
  }
  
  // No credentials file found, start authentication flow
  fprintf(stderr, "\n=== First Time Setup ===\n");
  fprintf(stderr, "No GitHub Copilot credentials found. Starting authentication...\n");
  
  if (authenticate_with_github(copilot_token, access_token, expires_at)) {
    fprintf(stderr, "[Step 4/4] Saving credentials...\n");
    if (save_credentials(copilot_token, access_token, expires_at)) {
      fprintf(stderr, "✓ Credentials saved to ~/.bash_llm/copilot_auth.json\n\n");
      return true;
    }
  }
  
  return false;
}

// Extract content from streaming SSE response
static std::string extract_content_from_sse(const std::string& response) {
  std::stringstream ss(response);
  std::string line;
  std::string full_content;
  
  while (std::getline(ss, line)) {
    if (line.find("data: ") == 0) {
      std::string json_data = line.substr(6); // Remove "data: " prefix
      
      if (json_data == "[DONE]") {
        break;
      }
      
      try {
        // Parse JSON and extract content
        json response_obj = json::parse(json_data);
        if (response_obj.contains("choices") && response_obj["choices"].is_array() && 
            response_obj["choices"].size() > 0) {
          const auto& choice = response_obj["choices"][0];
          if (choice.contains("delta") && choice["delta"].contains("content")) {
            full_content += choice["delta"]["content"].get<std::string>();
          }
        }
      } catch (const json::exception& e) {
        // Skip lines that aren't valid JSON
        continue;
      }
    }
  }
  
  return full_content;
}

// Send a chat message to GitHub Copilot
static int send_chat_message(const std::string& message) {
  std::string token;
  
  // Get valid copilot token (will refresh if needed)
  if (!get_copilot_token(token)) {
    return EXECUTION_FAILURE;
  }
  
  CURL *curl;
  CURLcode res;
  ResponseData response;
  
  curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to initialize curl\n");
    return EXECUTION_FAILURE;
  }
  
  // Build JSON payload using nlohmann/json
  json messages = json::array();
  std::string instructions = load_instructions();
  if (!instructions.empty()) {
    messages.push_back({
      {"role", "system"},
      {"content", instructions}
    });
  }
  for (const auto& msg : g_chat_history) {
    messages.push_back(msg);
  }
  messages.push_back({
    {"role", "user"},
    {"content", message}
  });

  json payload_obj = {
    {"messages", messages},
    {"model", "gpt-4"},
    {"stream", true}
  };
  
  std::string payload = payload_obj.dump();
  
  // Set up curl
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
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  res = curl_easy_perform(curl);
  
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    return EXECUTION_FAILURE;
  }
  
  // Extract and print the content
  std::string content = extract_content_from_sse(response.data);
  
  if (content.empty()) {
    fprintf(stderr, "Error: No content received or failed to parse response\n");
    fprintf(stderr, "Response: %s\n", response.data.c_str());
    return EXECUTION_FAILURE;
  }
  
  printf("%s\n", content.c_str());
  fflush(stdout);

  // Update history after successful response
  g_chat_history.push_back({
    {"role", "user"},
    {"content", message}
  });
  g_chat_history.push_back({
    {"role", "assistant"},
    {"content", content}
  });
  
  return EXECUTION_SUCCESS;
}

// Interactive chat mode
static int interactive_chat(bool is_tty) {
  const char *cyan = "\033[36m";
  const char *green = "\033[32m";
  const char *yellow = "\033[33m";
  const char *bold = "\033[1m";
  const char *reset = "\033[0m";

  FILE *input = fopen("/dev/stdin", "r");

  char buffer[4096];
  while (true) {
  
    printf("%s%s> %s", green, bold, reset);
    fflush(stdout);
    
    if (!fgets(buffer, sizeof(buffer), input)) {
      break;
    }
    
    std::string message(buffer);
    
    // Trim whitespace and newline
    message.erase(0, message.find_first_not_of(" \t\n\r"));
    message.erase(message.find_last_not_of(" \t\n\r") + 1);
    
    if (message.empty()) {
      continue;
    }
    
    // Only process commands if connected to a tty
    if (message == "/exit" || message == "/quit") {
      break;
    }
    
    if (message == "/new") {
      g_chat_history.clear();
      printf("%s%sNew chat started.%s\n\n", yellow, bold, reset);
      continue;
    }
    
    if (message == "/help") {
      printf("%s%sCommands%s\n", bold, cyan, reset);
      printf("  %s/help%s   Show this help\n", bold, reset);
      printf("  %s/new%s    Start a new chat (clear history)\n", bold, reset);
      printf("  %s/exit%s   Exit interactive mode\n", bold, reset);
      printf("  %s/quit%s   Exit interactive mode\n\n", bold, reset);
      continue;
    }
    
    // Send message and wait for response before processing next line
    if (send_chat_message(message) != EXECUTION_SUCCESS) {
      fprintf(stderr, "Failed to send message\n");
      // Continue processing remaining lines even on error
    }
  }
  
  if (input != stdin) {
    fclose(input);
  }

  return EXECUTION_SUCCESS;
}

// Non-tty interactive mode: read each stdin line as a prompt
static int interactive_chat_pipe() {
  char buffer[4096];
  FILE *input = fopen("/dev/stdin", "r");
  while (fgets(buffer, sizeof(buffer), input)) {
    std::string message = trim_whitespace(std::string(buffer));
    if (message.empty()) {
      continue;
    }
    // Send message and wait for response before processing next line
    if (send_chat_message(message) != EXECUTION_SUCCESS) {
      fprintf(stderr, "Failed to send message\n");
      // Continue processing remaining lines even on error
    }
  }

  return EXECUTION_SUCCESS;
}

int
llm_builtin (WORD_LIST *list)
{
  // Ensure instructions file exists on first use
  static bool first_run = true;
  if (first_run) {
    ensure_instructions_file();
    first_run = false;
  }

  // Check if stdin is connected to a tty
  // If so, automatically enable interactive mode
  int is_tty = ttyname(0) != nullptr ? 1 : 0;
  int opt;
  int interactive = 0;
  int new_chat = 0;
  std::string message;
  const char *opt_string = "in";
  
  reset_internal_getopt();
  while ((opt = internal_getopt(list, const_cast<char*>(opt_string))) != -1) {
    switch (opt) {
      case 'i':
        interactive = 1;
        break;
      case 'n':
        new_chat = 1;
        break;
      CASE_HELPOPT;
      default:
        builtin_usage();
        return (EX_USAGE);
    }
  }
  list = loptend;

  if (new_chat) {
    g_chat_history.clear();
  }
  
  if (is_tty && (interactive || list == nullptr)) {
       return interactive_chat(true);
  }
  if (interactive) {
    return interactive_chat_pipe();
  }
  
  // Collect all arguments as the message
  std::stringstream ss;
  while (list) {
    ss << list->word->word;
    list = list->next;
    if (list) {
      ss << " ";
    }
  }
  
  // add input to the prompt
  if (!is_tty){
    FILE * input = fopen("/dev/stdin", "r");
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), input)) {
      ss << "\n" << buffer;
    }
    fclose(input);
  }

  message = ss.str();
  
  return send_chat_message(message);
}

int
llm_builtin_load (char *s)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
  ensure_instructions_file();
  return (1);
}

void
llm_builtin_unload (char *s)
{
  curl_global_cleanup();
}

const char *llm_doc[] = {
  "Chat with GitHub Copilot LLM.",
  "",
  "Usage: llm [-i] [-n] [message...]",
  "",
  "Options:",
  "  -i    Interactive chat mode",
  "  -n    Start a new chat (clear conversation history)",
  "",
  "Examples:",
  "  llm What is the capital of France?",
  "  llm -i    # Start interactive chat",
  "",
  "Setup:",
  "  On first use, you will be prompted to authenticate with GitHub.",
  "  Tokens are automatically refreshed as needed.",
  (char *)NULL
};

struct builtin llm_struct = {
  const_cast<char*>("llm"),		
  llm_builtin,		
  BUILTIN_ENABLED,	
  const_cast<char* const*>(llm_doc),		
  const_cast<char*>("llm [-i] [message...]"),		
  0			
};