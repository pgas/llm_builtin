# Tools/Function Calling Implementation Summary

## What Was Implemented

Added OpenAI-style tool/function calling support to llm_builtin, allowing LLMs to request tool execution based on provided tool definitions.

## Changes Made

### 1. LLMProvider Interface ([llm_provider.h](src/llm_provider.h))
- Updated `send_message()` signature to accept:
  - `const json& tools` - array of tool definitions
  - `json* tool_calls` - output parameter for tool calls from LLM response
  
### 2. CopilotProvider ([copilot_provider.h](src/copilot_provider.h), [copilot_provider.cpp](src/copilot_provider.cpp))
- Modified `send_message()` to include tools in API payload
- Updated `extract_content_from_sse()` to parse tool_calls from streaming responses
- Added logic to handle both content and tool_calls in responses
- Tools are sent to GitHub Copilot API when provided

### 3. LiteLLMProvider ([litellm_provider.h](src/litellm_provider.h), [litellm_provider.cpp](src/litellm_provider.cpp))
- Modified `send_message()` to include tools in API payload
- Updated response parsing to extract tool_calls from JSON response
- Handles non-streaming responses with tool calls

### 4. Main Builtin Logic ([llm_builtin.cpp](src/llm_builtin.cpp))
- Added `try_parse_json_request()` function to detect and parse JSON requests
- Extracts tools array from JSON requests
- Modified `send_chat_message()` to accept tools parameter
- Added tool_calls display logic with 🔧 (wrench) unicode symbol prefix
- Tool calls are formatted with function name and arguments
- Updates conversation history with tool_calls for multi-turn context

## Key Features

1. **JSON Request Parsing**: Automatically detects JSON-formatted requests
2. **Tool Definition Support**: Accepts OpenAI-compatible tool definitions
3. **Tool Call Display**: Shows tool calls with clear formatting and visual indicator (🔧)
4. **Provider Support**: Works with both Copilot and LiteLLM
5. **History Tracking**: Tool calls are preserved in conversation history
6. **Backward Compatible**: Regular text requests still work as before

## Usage Example

```bash
cat << 'EOF' | llm
{
  "model": "gpt-4o",
  "messages": [
    {"role": "user", "content": "List the files in the current directory."}
  ],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "run_shell_command",
        "description": "Executes a shell command on the local system.",
        "parameters": {
          "type": "object",
          "properties": {
            "command": {
              "type": "string",
              "description": "The shell command to execute."
            }
          },
          "required": ["command"]
        }
      }
    }
  ]
}
EOF
```

## Output Format

When tool calls are returned:
```
🔧 Function Call: run_shell_command
Arguments: {"command": "ls -la"}
```

## Files Created

- [test_tools.json](test_tools.json) - Example JSON request with tool definition
- [test_tools.sh](test_tools.sh) - Test script demonstrating usage
- [TOOLS.md](TOOLS.md) - Comprehensive documentation

## Notes

- Tool execution is intentionally NOT implemented - only display
- The LLM decides when to use tools based on context
- Model must support function calling (e.g., GPT-4, GPT-4o)
- Tool calls are informational - up to the user to execute them
