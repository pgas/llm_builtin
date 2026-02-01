/* LLM Chat builtin - Interact with GitHub Copilot */

#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

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
      
      // Simple JSON parsing to extract content
      size_t content_pos = json_data.find("\"content\":\"");
      if (content_pos != std::string::npos) {
        content_pos += 11; // Move past "content":"
        size_t end_pos = json_data.find("\"", content_pos);
        
        while (end_pos != std::string::npos && json_data[end_pos - 1] == '\\') {
          end_pos = json_data.find("\"", end_pos + 1);
        }
        
        if (end_pos != std::string::npos) {
          std::string content = json_data.substr(content_pos, end_pos - content_pos);
          
          // Unescape common sequences
          size_t pos = 0;
          while ((pos = content.find("\\n", pos)) != std::string::npos) {
            content.replace(pos, 2, "\n");
            pos += 1;
          }
          pos = 0;
          while ((pos = content.find("\\\"", pos)) != std::string::npos) {
            content.replace(pos, 2, "\"");
            pos += 1;
          }
          
          full_content += content;
        }
      }
    }
  }
  
  return full_content;
}

// Send a chat message to GitHub Copilot
static int send_chat_message(const std::string& message, const std::string& token) {
  CURL *curl;
  CURLcode res;
  ResponseData response;
  
  curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to initialize curl\n");
    return EXECUTION_FAILURE;
  }
  
  // Prepare JSON payload
  std::string json_message = message;
  // Escape quotes and newlines
  size_t pos = 0;
  while ((pos = json_message.find("\"", pos)) != std::string::npos) {
    json_message.replace(pos, 1, "\\\"");
    pos += 2;
  }
  pos = 0;
  while ((pos = json_message.find("\n", pos)) != std::string::npos) {
    json_message.replace(pos, 1, "\\n");
    pos += 2;
  }
  
  std::string payload = "{\"messages\":[{\"role\":\"user\",\"content\":\"" + json_message + "\"}],\"model\":\"gpt-4\",\"stream\":true}";
  
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
static int interactive_chat(const std::string& token) {
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
    
    if (send_chat_message(message, token) != EXECUTION_SUCCESS) {
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
  
  // Get GitHub Copilot token from environment
  char *token_env = get_string_value("GITHUB_COPILOT_TOKEN");
  if (!token_env || strlen(token_env) == 0) {
    fprintf(stderr, "Error: GITHUB_COPILOT_TOKEN environment variable not set\n");
    fprintf(stderr, "Please set it with: export GITHUB_COPILOT_TOKEN='your_token_here'\n");
    return EXECUTION_FAILURE;
  }
  
  std::string token(token_env);
  
  if (interactive) {
    return interactive_chat(token);
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
  
  return send_chat_message(message, token);
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
  "Environment:",
  "  GITHUB_COPILOT_TOKEN    GitHub Copilot API token (required)",
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