/* LLM Chat builtin - Interact with LLM providers (GitHub Copilot, etc.) */

#include "llm_builtin.h"
#include "llm_provider.h"
#include "copilot_provider.h"
#include "litellm_provider.h"
#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern "C" {
#include "builtins.h"
#include "shell.h"
#include "bashgetopt.h"
#include "common.h"

// Forward declarations from readline/history.h
typedef struct _hist_entry {
  char *line;
  char *timestamp;
  void *data;
} HIST_ENTRY;

extern HIST_ENTRY **history_list(void);
extern char* ttyname(int fd);
}

// Global LLM provider instance
static std::unique_ptr<LLMProvider> g_provider;

// In-memory chat history for the current shell session
static std::vector<json> g_chat_history;

// Get the last N entries from bash history
static std::vector<std::string> get_bash_history(int limit = 10) {
  std::vector<std::string> history_entries;
  
  HIST_ENTRY **hlist = history_list();
  if (!hlist) {
    return history_entries;
  }
  
  int total = 0;
  while (hlist[total]) {
    total++;
  }
  
  limit += 1; // last command is llm itself, skip it
  int start_idx = (total > limit) ? (total - limit) : 0;
  
  for (int i = start_idx; i < total-1; i++) {
    if (hlist[i] && hlist[i]->line) {
      history_entries.push_back(std::string(hlist[i]->line));
    }
  }
  
  return history_entries;
}

// Get base directory for builtin files
static std::string get_llm_dir_path() {
  const char *home = getenv("HOME");
  if (!home) {
    home = "/root";
  }
  return std::string(home) + "/.bash_llm";
}

// Get path to custom instructions file
static std::string get_instructions_file_path() {
  return get_llm_dir_path() + "/instructions.txt";
}

// Get path to config file
static std::string get_config_file_path() {
  return get_llm_dir_path() + "/config.json";
}

// Load configuration from JSON file
static bool load_config() {
  std::string config_file = get_config_file_path();
  std::ifstream file(config_file);
  
  std::string provider_name = "copilot"; // default
  
  if (file.is_open()) {
    try {
      json config_data;
      file >> config_data;
      
      if (config_data.contains("provider")) {
        provider_name = config_data["provider"].get<std::string>();
      }
    } catch (const std::exception& e) {
      std::cerr << "Warning: Failed to parse config file: " << e.what() << "\n";
      std::cerr << "Using default provider (copilot)\n";
    }
  }
  
  // Create appropriate provider
  if (provider_name == "litellm") {
    g_provider = std::make_unique<LiteLLMProvider>();
  } else {
    g_provider = std::make_unique<CopilotProvider>();
  }
  
  if (!g_provider->initialize()) {
    std::cerr << "Failed to initialize LLM provider\n";
    g_provider.reset();
    return false;
  }
  
  return true;
}

// Reload configuration and reinitialize provider
static bool reload_config() {
  if (g_provider) {
    g_provider->cleanup();
    g_provider.reset();
  }
  g_chat_history.clear();
  return load_config();
}

// Ensure custom instructions file exists with defaults
static void ensure_instructions_file() {
  std::string instructions_dir = get_llm_dir_path();
  std::string instructions_file = get_instructions_file_path();

  if (mkdir(instructions_dir.c_str(), 0700) != 0 && errno != EEXIST) {
    std::cerr << "Warning: Cannot create directory " << instructions_dir << "\n";
    return;
  }

  std::ifstream existing(instructions_file);
  if (existing.is_open()) {
    existing.close();
    return;
  }

  std::ofstream file(instructions_file);
  if (!file.is_open()) {
    std::cerr << "Warning: Cannot write to " << instructions_file << "\n";
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

// Get predefined tools to include in all requests
static json get_predefined_tools() {
  json tools = json::array();
  
  // Tool 1: Run shell command
  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "run_shell_command"},
      {"description", "Execute a shell command in the user's current bash environment. Use this to run commands, check files, execute scripts, or perform system operations. The command will be executed with the user's current environment and permissions."},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"command", {
            {"type", "string"},
            {"description", "The shell command to execute. Can be a simple command or a complex pipeline."}
          }}
        }},
        {"required", json::array({"command"})}
      }}
    }}
  });
  
  return tools;
}

// Send a chat message using the LLM provider
static int send_chat_message(const std::string& message) {
  if (!g_provider) {
    std::cerr << "Error: LLM provider not initialized\n";
    return EXECUTION_FAILURE;
  }
  
  std::string instructions = load_instructions();
  
  // Append bash history context to system message
  std::string system_message = instructions;
  std::vector<std::string> bash_history = get_bash_history(10);
  if (!bash_history.empty()) {
    if (!system_message.empty()) {
      system_message += "\n\n";
    }
    system_message += "Recent shell commands:\n";
    for (const auto& cmd : bash_history) {
      system_message += "  " + cmd + "\n";
    }
  }
  
  // Get predefined tools to include in the request
  json tools = get_predefined_tools();
  
  std::string response;
  json tool_calls;
  if (!g_provider->send_message(message, g_chat_history, system_message, tools, response, &tool_calls)) {
    return EXECUTION_FAILURE;
  }
  
  // Display regular response if present
  if (!response.empty()) {
    std::cout << response << "\n" << std::flush;
  }
  
  // Display tool_calls if present (with unicode tool symbol)
  if (tool_calls.is_array() && !tool_calls.empty()) {
    for (const auto& tool_call : tool_calls) {
      std::cout << "🔧 ";  // Unicode tool/wrench symbol

      if (tool_call.contains("function")) {
        const auto& func = tool_call["function"];
        std::string func_name = func.contains("name") ? func["name"].get<std::string>() : "unknown";
        std::string func_args = func.contains("arguments") ? func["arguments"].get<std::string>() : "{}";

        if (func_name == "run_shell_command") {
          std::string command;
          try {
            json args = json::parse(func_args);
            if (args.contains("command") && args["command"].is_string()) {
              command = args["command"].get<std::string>();
            }
          } catch (const std::exception&) {
            // fall back to raw arguments
          }

          if (command.empty()) {
            command = func_args;
          }

          std::cout << command << "\n";
          std::cout << "Execute? (y/N) " << std::flush;

          std::string confirm;
          if (std::getline(std::cin, confirm)) {
            if (!confirm.empty() && (confirm[0] == 'y' || confirm[0] == 'Y')) {
              int status = system(command.c_str());
              if (status != 0) {
                std::cerr << "Command exited with status " << status << "\n";
              }
            }
          }
        } else {
          std::cout << tool_call.dump(2) << "\n";
        }
      } else {
        std::cout << tool_call.dump(2) << "\n";
      }
    }
    std::cout << std::flush;
  }

  // Update history after successful response
  g_chat_history.push_back({
    {"role", "user"},
    {"content", message}
  });
  
  json assistant_msg = {
    {"role", "assistant"}
  };
  
  if (!response.empty()) {
    assistant_msg["content"] = response;
  }
  
  if (tool_calls.is_array() && !tool_calls.empty()) {
    assistant_msg["tool_calls"] = tool_calls;
  }
  
  g_chat_history.push_back(assistant_msg);
  
  return EXECUTION_SUCCESS;
}

// Interactive chat mode
static int interactive_chat(bool is_tty) {
  const char *cyan = "\033[36m";
  const char *green = "\033[32m";
  const char *yellow = "\033[33m";
  const char *bold = "\033[1m";
  const char *reset = "\033[0m";

  std::ifstream input("/dev/stdin");

  std::string message;
  while (true) {
  
    std::cout << green << bold << "> " << reset << std::flush;
    
    if (!std::getline(input, message)) {
      break;
    }
    
    message = trim_whitespace(message);
    
    if (message.empty()) {
      continue;
    }
    
    if (message == "/exit" || message == "/quit") {
      break;
    }
    
    if (message == "/new") {
      g_chat_history.clear();
      std::cout << yellow << bold << "New chat started." << reset << "\n\n";
      continue;
    }
    
    if (message.substr(0, 6) == "/model") {
      if (message == "/model") {
        // Show current model
        std::cout << cyan << "Current model: " << bold << g_provider->get_model_name() << reset << "\n\n";
      } else {
        // Switch model
        std::string new_model = trim_whitespace(message.substr(6));
        if (!new_model.empty()) {
          g_provider->set_model(new_model);
          std::cout << yellow << bold << "Switched to model: " << new_model << reset << "\n\n";
        } else {
          std::cout << "Usage: /model <model_name>\n\n";
        }
      }
      continue;
    }
    
    if (message == "/help") {
      std::cout << bold << cyan << "Commands" << reset << "\n";
      std::cout << "  " << bold << "/help" << reset << "   Show this help\n";
      std::cout << "  " << bold << "/new" << reset << "    Start a new chat (clear history)\n";
      std::cout << "  " << bold << "/model" << reset << "  Show current model or switch: /model <name>\n";
      std::cout << "  " << bold << "/exit" << reset << "   Exit interactive mode\n";
      std::cout << "  " << bold << "/quit" << reset << "   Exit interactive mode\n\n";
      continue;
    }
    
    if (send_chat_message(message) != EXECUTION_SUCCESS) {
      std::cerr << "Failed to send message\n";
    }
  }

  return EXECUTION_SUCCESS;
}

// Non-tty interactive mode: read each stdin line as a prompt
static int interactive_chat_pipe() {
  std::ifstream input("/dev/stdin");
  std::string message;
  while (std::getline(input, message)) {
    message = trim_whitespace(message);
    if (message.empty()) {
      continue;
    }
    if (send_chat_message(message) != EXECUTION_SUCCESS) {
      std::cerr << "Failed to send message\n";
    }
  }

  return EXECUTION_SUCCESS;
}

extern "C" {
  
int
llm_builtin (WORD_LIST *list)
{
  // Initialize provider if not already initialized (fallback in case load wasn't called)
  static bool first_run = true;
  if (first_run) {
    if (!g_provider) {
      if (!load_config()) {
        return EXECUTION_FAILURE;
      }
    }
    
    ensure_instructions_file();
    first_run = false;
  }

  // Check if stdin is connected to a tty
  int is_tty = ttyname(0) != nullptr ? 1 : 0;
  int opt;
  int interactive = 0;
  int new_chat = 0;
  int reload = 0;
  int show_help = 0;
  std::string message;
  std::string model_override;
  const char *opt_string = "inrhm:";
  
  reset_internal_getopt();
  while ((opt = internal_getopt(list, const_cast<char*>(opt_string))) != -1) {
    switch (opt) {
      case 'i':
        interactive = 1;
        break;
      case 'n':
        new_chat = 1;
        break;
      case 'r':
        reload = 1;
        break;
      case 'h':
        show_help = 1;
        break;
      case 'm':
        model_override = list_optarg;
        break;
      CASE_HELPOPT;
      default:
        builtin_usage();
        return (EX_USAGE);
    }
  }
  list = loptend;
  
  if (reload) {
    if (!reload_config()) {
      std::cerr << "Failed to reload configuration\n";
      return EXECUTION_FAILURE;
    }
    std::cout << "Configuration reloaded. Using provider: " << g_provider->get_provider_name() << "\n";
    return EXECUTION_SUCCESS;
  }
  
  // Apply model override if specified
  if (!model_override.empty()) {
    g_provider->set_model(model_override);
  }
  
  if (show_help) {
    if (!g_provider) {
      std::cerr << "No provider initialized\n";
      return EXECUTION_FAILURE;
    }
    // Display help with provider and model info
    const char *bold = "\033[1m";
    const char *reset = "\033[0m";
    const char *cyan = "\033[36m";
    
    std::cout << bold << "LLM Bash Builtin" << reset << "\n\n";
    std::cout << "Current Configuration:\n";
    std::cout << "  Provider: " << cyan << g_provider->get_provider_name() << reset << "\n";
    std::cout << "  Model: " << cyan << g_provider->get_model_name() << reset << "\n\n";
    std::cout << bold << "Usage:" << reset << " llm [-i] [-n] [-r] [-h] [-m model] [message...]\n\n";
    std::cout << bold << "Options:" << reset << "\n";
    std::cout << "  -i          Interactive chat mode\n";
    std::cout << "  -n          Start a new chat (clear conversation history)\n";
    std::cout << "  -r          Reload configuration from ~/.bash_llm/config.json\n";
    std::cout << "  -m model    Override the model for this session\n";
    std::cout << "  -h          Show this help with current configuration\n\n";
    std::cout << bold << "Examples:" << reset << "\n";
    std::cout << "  llm What is the capital of France?\n";
    std::cout << "  llm -i              # Start interactive chat\n";
    std::cout << "  llm -m gpt-4o-mini  # Use a specific model\n";
    std::cout << "  llm -h              # Show this help\n";
    std::cout << "  llm -r              # Reload configuration\n\n";
    std::cout << bold << "Configuration:" << reset << "\n";
    std::cout << "  Edit ~/.bash_llm/config.json to change provider\n";
    std::cout << "  Example: {\"provider\": \"copilot\"} or {\"provider\": \"litellm\"}\n";
    return EXECUTION_SUCCESS;
  }

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
    std::ifstream input("/dev/stdin");
    std::string line;
    while (std::getline(input, line)) {
      ss << "\n" << line;
    }
  }

  message = ss.str();
  
  return send_chat_message(message);
}

int
llm_builtin_load (char *s)
{
  if (!load_config()) {
    return 0;
  }
  
  ensure_instructions_file();
  return 1;
}

void
llm_builtin_unload (char *s)
{
  if (g_provider) {
    g_provider->cleanup();
    g_provider.reset();
  }
}

const char *llm_doc[] = {
  "Chat with LLM provider (GitHub Copilot or LiteLLM).",
  "",
  "Usage: llm [-i] [-n] [-r] [-h] [-m model] [message...]",
  "",
  "Options:",
  "  -i          Interactive chat mode",
  "  -n          Start a new chat (clear conversation history)",
  "  -r          Reload configuration from ~/.bash_llm/config.json",
  "  -m model    Override the model for this session",
  "  -h          Show help with current provider and model",
  "",
  "Examples:",
  "  llm What is the capital of France?",
  "  llm -i              # Start interactive chat",
  "  llm -m gpt-4o-mini  # Use a specific model",
  "  llm -h              # Show help and configuration",
  "  llm -r              # Reload configuration",
  "",
  "Interactive Commands:",
  "  /help         Show available commands",
  "  /new          Start a new chat (clear history)",
  "  /model        Show current model",
  "  /model <name> Switch to a different model",
  "  /exit, /quit  Exit interactive mode",
  "",
  "Configuration:",
  "  Edit ~/.bash_llm/config.json to change provider and default model.",
  "  Example: {\"provider\": \"copilot\", \"copilot\": {\"model\": \"gpt-4o\"}}",
  "  Example: {\"provider\": \"litellm\", \"litellm\": {\"model\": \"gpt-4\"}}",
  "  On first use with copilot, you will be prompted to authenticate with GitHub.",
  (char *)NULL
};

struct builtin llm_struct __attribute__((visibility("default"))) = {
  const_cast<char*>("llm"),		
  llm_builtin,		
  BUILTIN_ENABLED,	
  const_cast<char* const*>(llm_doc),		
  const_cast<char*>("llm [-i] [-n] [-r] [-h] [-m model] [message...]"),		
  0			
};

}
