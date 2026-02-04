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
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern "C" {
#include "builtins.h"
#include "shell.h"
#include "bashgetopt.h"
#include "common.h"
#include "variables.h"

// Forward declarations from readline/history.h
typedef struct _hist_entry {
  char *line;
  char *timestamp;
  void *data;
} HIST_ENTRY;

extern HIST_ENTRY **history_list(void);
extern char* ttyname(int fd);

// Forward declarations from bashhist.h for history management
extern int check_add_history(char *, int);
extern int remember_on_history;
extern int enable_history_list;
}

// Global LLM provider instance
static std::unique_ptr<LLMProvider> g_provider;

// In-memory chat history for the current shell session
static std::vector<json> g_chat_history;

// Whether to include bash history in context (from config)
static bool g_include_history = false;

// Get the last N entries from bash history, filtered and numbered
// Returns formatted strings like "1: ls -la" where higher numbers = more recent
static std::vector<std::string> get_bash_history(int limit = 20) {
  std::vector<std::string> history_entries;
  
  HIST_ENTRY **hlist = history_list();
  if (!hlist) {
    return history_entries;
  }
  
  int total = 0;
  while (hlist[total]) {
    total++;
  }
  
  // Collect commands in reverse order (most recent first), filtering out llm commands
  std::vector<std::string> filtered_commands;
  for (int i = total - 1; i >= 0 && filtered_commands.size() < static_cast<size_t>(limit); i--) {
    if (hlist[i] && hlist[i]->line) {
      std::string cmd(hlist[i]->line);
      // Skip commands that start with "llm"
      if (cmd.compare(0, 3, "llm") == 0 && (cmd.length() == 3 || cmd[3] == ' ')) {
        continue;
      }
      filtered_commands.push_back(cmd);
    }
  }
  
  // Number them so most recent has highest number, and display in chronological order
  // (oldest first, newest last) so the highest number visually appears at the end
  int num = 1;
  for (auto it = filtered_commands.rbegin(); it != filtered_commands.rend(); ++it) {
    history_entries.push_back(std::to_string(num) + ": " + *it);
    num++;
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

// Handle readline completion
static int handle_completion() {
  // Get the current readline line and cursor position
  SHELL_VAR *readline_line_var = find_variable("READLINE_LINE");
  SHELL_VAR *readline_point_var = find_variable("READLINE_POINT");
  
  if (!readline_line_var || !readline_line_var->value) {
    builtin_error("READLINE_LINE not set - use with bind -x");
    return EXECUTION_FAILURE;
  }
  
  std::string current_line = readline_line_var->value;
  int cursor_pos = 0;
  
  if (readline_point_var && readline_point_var->value) {
    cursor_pos = atoi(readline_point_var->value);
  }
  
  // Build prompt for completion
  std::string message = "Complete this bash command line. Only provide the completed command, no explanations:\n\n";
  message += current_line;
  
  if (!g_provider) {
    builtin_error("No provider initialized");
    return EXECUTION_FAILURE;
  }
  
  try {
    // Create a simple system message for completion
    std::string system_message = "You are a helpful bash command completion assistant. When given a partial command, complete it logically. Return ONLY the completed command with no explanation or formatting.";
    
    // Empty history for one-shot completion
    std::vector<json> empty_history;
    
    // No tools needed for completion
    json empty_tools = json::object();
    
    std::string response;
    if (!g_provider->send_message(message, empty_history, system_message, empty_tools, response)) {
      builtin_error("Failed to get completion from provider");
      return EXECUTION_FAILURE;
    }
    
    // Clean up the completion - remove leading/trailing whitespace and newlines
    size_t start = response.find_first_not_of(" \t\n\r");
    size_t end = response.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
      response = response.substr(start, end - start + 1);
    }
    
    // If completion starts with backticks or code fence, try to extract just the code
    if (response.find("```") != std::string::npos) {
      size_t code_start = response.find("```");
      code_start = response.find("\n", code_start);
      if (code_start != std::string::npos) {
        size_t code_end = response.find("```", code_start);
        if (code_end != std::string::npos) {
          response = response.substr(code_start + 1, code_end - code_start - 1);
          // Trim again
          start = response.find_first_not_of(" \t\n\r");
          end = response.find_last_not_of(" \t\n\r");
          if (start != std::string::npos && end != std::string::npos) {
            response = response.substr(start, end - start + 1);
          }
        }
      }
    }
    
    // Update READLINE_LINE with the completion
    // bind_variable expects char* not const char*, so we need to copy the string
    char *completion_str = strdup(response.c_str());
    bind_variable("READLINE_LINE", completion_str, 0);
    free(completion_str);
    
    // Set cursor to end of line
    char point_str[32];
    snprintf(point_str, sizeof(point_str), "%zu", response.length());
    bind_variable("READLINE_POINT", point_str, 0);
    
    return EXECUTION_SUCCESS;
    
  } catch (const std::exception& e) {
    builtin_error("Completion failed: %s", e.what());
    return EXECUTION_FAILURE;
  }
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
      
      if (config_data.contains("include_history")) {
        g_include_history = config_data["include_history"].get<bool>();
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
  file << "- Limit the response to what can appear in a terminal window 80 cols x 20 rows, and offer the user to give more explanation if needed\n";
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

// Check if input matches command (supports partial prefix matching)
// e.g., "/q" matches "/quit", "/h" matches "/help"
static bool matches_command(const std::string& input, const std::string& command) {
  if (input.length() > command.length()) {
    return false;
  }
  return command.compare(0, input.length(), input) == 0;
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
static int send_chat_message(const std::string& message, bool include_history_override = false) {
  if (!g_provider) {
    std::cerr << "Error: LLM provider not initialized\n";
    return EXECUTION_FAILURE;
  }
  
  std::string instructions = load_instructions();
  std::string system_message = instructions;
  
  // Build user message with optional history context
  std::string user_message = message;
  bool should_include_history = include_history_override || g_include_history;
  
  if (should_include_history) {
    std::vector<std::string> bash_history = get_bash_history(20);
    if (!bash_history.empty()) {
      std::string history_context = "\n\n=== RECENT SHELL COMMAND HISTORY ===\n";
      history_context += "Commands are numbered from OLDEST to NEWEST.\n";
      history_context += "THE HIGHEST NUMBER IS THE MOST RECENT COMMAND.\n";
      history_context += "When I say \"last command\" or \"this command\", I mean the HIGHEST numbered command below.\n\n";
      history_context += "Unless explicitely asked to, one and only one command, usually the last one should be considered.\n\n";
      for (const auto& cmd : bash_history) {
        history_context += cmd + "\n";
      }
      user_message = history_context + "\n" + message;
    }
  }
  
  // Get predefined tools to include in the request
  json tools = get_predefined_tools();
  
  std::string response;
  json tool_calls;
  if (!g_provider->send_message(user_message, g_chat_history, system_message, tools, response, &tool_calls)) {
    return EXECUTION_FAILURE;
  }
  
  // Update history first
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

  // Display regular response if present (BEFORE tool execution)
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

          std::string tool_call_id = tool_call.contains("id") && tool_call["id"].is_string()
            ? tool_call["id"].get<std::string>()
            : "";

          std::cout << "Run the following command?\n";
          std::cout << command << "\n";
          std::cout << "(A)llow/(S)kip " << std::flush;

          char ch = '\0';
          // Try to read a single character from /dev/tty
          int tty_fd = open("/dev/tty", O_RDONLY);
          if (tty_fd >= 0) {
            struct termios old_tio, new_tio;
            tcgetattr(tty_fd, &old_tio);
            new_tio = old_tio;
            new_tio.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(tty_fd, TCSANOW, &new_tio);
            
            ssize_t n = read(tty_fd, &ch, 1);
            (void)n;  // Suppress unused result warning
            
            tcsetattr(tty_fd, TCSANOW, &old_tio);
            close(tty_fd);
            std::cout << "\n";
          }

          std::string tool_output;
          if (ch == 'a' || ch == 'A') {
            // Add command to bash history (respects HISTCONTROL and HISTIGNORE)
            if (remember_on_history && enable_history_list) {
              char *hist_line = strdup(command.c_str());
              if (hist_line) {
                check_add_history(hist_line, 0);
                free(hist_line);
              }
            }
            
            int status = system(command.c_str());
            if (status != 0) {
              std::cerr << "Command exited with status " << status << "\n";
            }
            tool_output = "Command executed successfully with exit status " + std::to_string(status);
          } else {
            tool_output = "Command execution skipped by user.";
          }

          // Add tool response to history
          if (!tool_call_id.empty()) {
            g_chat_history.push_back({
              {"role", "tool"},
              {"tool_call_id", tool_call_id},
              {"content", tool_output}
            });
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
  
  return EXECUTION_SUCCESS;
}

// Interactive chat mode
static int interactive_chat(bool is_tty, bool force_history = false) {
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
    
    if (matches_command(message, "/exit") || matches_command(message, "/quit")) {
      break;
    }
    
    if (matches_command(message, "/new")) {
      g_chat_history.clear();
      std::cout << yellow << bold << "New chat started." << reset << "\n\n";
      continue;
    }
    
    if (message[0] == '/' && matches_command(message.substr(0, message.find(' ')), "/model")) {
      size_t space_pos = message.find(' ');
      if (space_pos == std::string::npos) {
        // Show current model
        std::cout << cyan << "Current model: " << bold << g_provider->get_model_name() << reset << "\n\n";
      } else {
        // Switch model
        std::string new_model = trim_whitespace(message.substr(space_pos));
        if (!new_model.empty()) {
          g_provider->set_model(new_model);
          std::cout << yellow << bold << "Switched to model: " << new_model << reset << "\n\n";
        } else {
          std::cout << "Usage: /model <model_name>\n\n";
        }
      }
      continue;
    }
    
    if (matches_command(message, "/help")) {
      std::cout << bold << cyan << "Commands" << reset << " (partial matches work, e.g., /q for /quit)\n";
      std::cout << "  " << bold << "/help" << reset << "   Show this help\n";
      std::cout << "  " << bold << "/new" << reset << "    Start a new chat (clear history)\n";
      std::cout << "  " << bold << "/model" << reset << "  Show current model or switch: /model <name>\n";
      std::cout << "  " << bold << "/exit" << reset << "   Exit interactive mode\n";
      std::cout << "  " << bold << "/quit" << reset << "   Exit interactive mode\n\n";
      continue;
    }
    
    if (send_chat_message(message, force_history) != EXECUTION_SUCCESS) {
      std::cerr << "Failed to send message\n";
    }
  }

  return EXECUTION_SUCCESS;
}

// Non-tty interactive mode: read each stdin line as a prompt
static int interactive_chat_pipe(bool force_history = false) {
  std::ifstream input("/dev/stdin");
  std::string message;
  while (std::getline(input, message)) {
    message = trim_whitespace(message);
    if (message.empty()) {
      continue;
    }
    if (send_chat_message(message, force_history) != EXECUTION_SUCCESS) {
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
  int completion_mode = 0;
  int force_history = 0;
  std::string message;
  std::string model_override;
  const char *opt_string = "inrhcm:H";
  
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
      case 'c':
        completion_mode = 1;
        break;
      case 'm':
        model_override = list_optarg;
        break;
      case 'H':
        force_history = 1;
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
  
  // Handle completion mode for use with bind -x
  if (completion_mode) {
    return handle_completion();
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
    std::cout << bold << "Usage:" << reset << " llm [-i] [-n] [-r] [-h] [-c] [-H] [-m model] [message...]\n\n";
    std::cout << bold << "Options:" << reset << "\n";
    std::cout << "  -i          Interactive chat mode\n";
    std::cout << "  -n          Start a new chat (clear conversation history)\n";
    std::cout << "  -r          Reload configuration from ~/.bash_llm/config.json\n";
    std::cout << "  -c          Completion mode (for use with bind -x)\n";
    std::cout << "  -H          Include bash command history in context\n";
    std::cout << "  -m model    Override the model for this session\n";
    std::cout << "  -h          Show this help with current configuration\n\n";
    std::cout << bold << "Examples:" << reset << "\n";
    std::cout << "  llm What is the capital of France?\n";
    std::cout << "  llm -i              # Start interactive chat\n";
    std::cout << "  llm -m gpt-4o-mini  # Use a specific model\n";
    std::cout << "  llm -h              # Show this help\n";
    std::cout << "  llm -r              # Reload configuration\n\n";
    std::cout << bold << "Readline Completion:" << reset << "\n";
    std::cout << "  bind -x '\"\\C-o\": llm -c'   # Bind Ctrl-O to complete current line\n\n";
    std::cout << bold << "Configuration:" << reset << "\n";
    std::cout << "  Edit ~/.bash_llm/config.json to change provider\n";
    std::cout << "  Example: {\"provider\": \"copilot\"} or {\"provider\": \"litellm\"}\n";
    return EXECUTION_SUCCESS;
  }

  if (new_chat) {
    g_chat_history.clear();
  }
  
  if (is_tty && (interactive || list == nullptr)) {
       return interactive_chat(true, force_history);
  }
  if (interactive) {
    return interactive_chat_pipe(force_history);
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
  
  return send_chat_message(message, force_history);
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
  "Usage: llm [-i] [-n] [-r] [-h] [-c] [-H] [-m model] [message...]",
  "",
  "Options:",
  "  -i          Interactive chat mode",
  "  -n          Start a new chat (clear conversation history)",
  "  -r          Reload configuration from ~/.bash_llm/config.json",
  "  -c          Completion mode (for use with bind -x)",
  "  -H          Include bash command history in context",
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
  "  Note: Partial matches work (e.g., /q for /quit, /h for /help)",
  "",
  "Readline Completion:",
  "  bind -x '\"\\C-o\": llm -c'   # Bind Ctrl-O to complete current line",
  "  The -c option reads READLINE_LINE, sends it to the LLM for completion,",
  "  and updates READLINE_LINE with the result. Works with any key binding.",
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
  const_cast<char*>("llm [-i] [-n] [-r] [-h] [-c] [-H] [-m model] [message...]"),		
  0			
};

}
