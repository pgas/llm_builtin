/* LLM Chat builtin - Interact with GitHub Copilot */

#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include <sys/stat.h>
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
}

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

// Get path to credentials file
static std::string get_auth_file_path() {
  const char *home = getenv("HOME");
  if (!home) {
    home = "/root";
  }
  return std::string(home) + "/.copilot_auth";
}

// Load credentials from ~/.copilot_auth
static bool load_credentials(std::string& copilot_token, std::string& access_token, time_t& expires_at) {
  std::string auth_file = get_auth_file_path();
  std::ifstream file(auth_file);
  
  if (!file.is_open()) {
    fprintf(stderr, "Debug: Cannot open %s\n", auth_file.c_str());
    return false;
  }
  
  try {
    json auth_data;
    file >> auth_data;
    
    if (!auth_data.contains("copilot_token") || !auth_data.contains("access_token") || 
        !auth_data.contains("expires_at")) {
      fprintf(stderr, "Debug: Missing required fields in %s\n", auth_file.c_str());
      fprintf(stderr, "Debug: Has copilot_token: %s\n", auth_data.contains("copilot_token") ? "yes" : "no");
      fprintf(stderr, "Debug: Has access_token: %s\n", auth_data.contains("access_token") ? "yes" : "no");
      fprintf(stderr, "Debug: Has expires_at: %s\n", auth_data.contains("expires_at") ? "yes" : "no");
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
      fprintf(stderr, "Debug: expires_at has invalid type\n");
      return false;
    }
    
    return true;
  } catch (const json::exception& e) {
    fprintf(stderr, "Debug: JSON parsing error: %s\n", e.what());
    return false;
  }
}

// Save credentials to ~/.copilot_auth
static bool save_credentials(const std::string& copilot_token, const std::string& access_token, time_t expires_at) {
  std::string auth_file = get_auth_file_path();
  
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
  
  curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to initialize curl for token refresh\n");
    return false;
  }
  
  // Prepare authorization header
  std::string auth_header = "Authorization: token " + access_token;
  
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, auth_header.c_str());
  
  curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/copilot_internal/v2/token");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  res = curl_easy_perform(curl);
  
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    fprintf(stderr, "Failed to refresh token: %s\n", curl_easy_strerror(res));
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
    expires_at = time(nullptr) + 3600;  // Token valid for 1 hour
    
    // Save updated token
    return save_credentials(copilot_token, access_token, expires_at);
  } catch (const json::exception& e) {
    fprintf(stderr, "Error parsing token response: %s\n", e.what());
    return false;
  }
}

// Get valid copilot token, refreshing if needed
static bool get_copilot_token(std::string& copilot_token) {
  std::string access_token;
  time_t expires_at;
  
  // Try to load existing credentials
  if (load_credentials(copilot_token, access_token, expires_at)) {
    time_t now = time(nullptr);
    
    // Check if token is expired or expiring within 5 minutes
    if (now < expires_at - 300) {
      // Token still valid
      return true;
    }
    
    // Token expired or expiring soon, refresh it
    fprintf(stderr, "Refreshing expired Copilot token...\n");
    if (refresh_copilot_token(copilot_token, access_token, expires_at)) {
      return true;
    } else {
      fprintf(stderr, "Error: Failed to refresh token. Please run: bash get_token.sh\n");
      return false;
    }
  }
  
  // No credentials file found
  fprintf(stderr, "Error: No credentials found at ~/.copilot_auth\n");
  fprintf(stderr, "Please run: bash get_token.sh\n");
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
  json payload_obj = {
    {"messages", json::array({
      {
        {"role", "user"},
        {"content", message}
      }
    })},
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
  
  return EXECUTION_SUCCESS;
}

// Interactive chat mode
static int interactive_chat() {
  printf("GitHub Copilot Chat (type 'exit' or 'quit' to end)\n");
  printf("================================================\n\n");
  
  char buffer[4096];
  while (true) {
    printf("You: ");
    fflush(stdout);
    
    if (!fgets(buffer, sizeof(buffer), stdin)) {
      printf("\n");
      break;
    }
    
    std::string message(buffer);
    
    // Trim whitespace and newline
    message.erase(0, message.find_first_not_of(" \t\n\r"));
    message.erase(message.find_last_not_of(" \t\n\r") + 1);
    
    if (message.empty()) {
      continue;
    }
    
    if (message == "exit" || message == "quit") {
      break;
    }
    
    printf("\nCopilot: ");
    fflush(stdout);
    
    if (send_chat_message(message) != EXECUTION_SUCCESS) {
      fprintf(stderr, "Failed to send message\n");
    }
    
    printf("\n");
  }
  
  return EXECUTION_SUCCESS;
}

int
llm_builtin (WORD_LIST *list)
{
  int opt;
  int interactive = 0;
  std::string message;
  const char *opt_string = "i";
  
  reset_internal_getopt();
  while ((opt = internal_getopt(list, const_cast<char*>(opt_string))) != -1) {
    switch (opt) {
      case 'i':
        interactive = 1;
        break;
      CASE_HELPOPT;
      default:
        builtin_usage();
        return (EX_USAGE);
    }
  }
  list = loptend;
  
  if (interactive) {
    return interactive_chat();
  }
  
  // Collect all arguments as the message
  if (!list) {
    fprintf(stderr, "Error: Please provide a message or use -i for interactive mode\n");
    return EX_USAGE;
  }
  
  std::stringstream ss;
  while (list) {
    ss << list->word->word;
    list = list->next;
    if (list) {
      ss << " ";
    }
  }
  
  message = ss.str();
  
  return send_chat_message(message);
}

int
llm_builtin_load (char *s)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
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
  "Usage: llm [-i] [message...]",
  "",
  "Options:",
  "  -i    Interactive chat mode",
  "",
  "Examples:",
  "  llm What is the capital of France?",
  "  llm -i    # Start interactive chat",
  "",
  "Setup:",
  "  Run once: bash get_token.sh",
  "  Token will be auto-refreshed as needed.",
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