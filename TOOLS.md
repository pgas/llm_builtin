# Tools/Function Calling Support

The llm_builtin now supports OpenAI-style tool/function calling. You can provide tool definitions in your requests and the LLM can choose to call them.

## Usage

### JSON Request Format

Send a JSON request with tools defined:

```json
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
        "description": "Executes a shell command on the local system and returns the output.",
        "parameters": {
          "type": "object",
          "properties": {
            "command": {
              "type": "string",
              "description": "The full shell command to execute (e.g., 'ls -la')."
            }
          },
          "required": ["command"]
        }
      }
    }
  ]
}
```

### Sending Requests

You can pipe JSON requests to the llm command:

```bash
cat request.json | llm
```

Or use a heredoc:

```bash
cat << 'EOF' | llm
{
  "model": "gpt-4o",
  "messages": [
    {"role": "user", "content": "What's the weather?"}
  ],
  "tools": [...]
}
EOF
```

### Tool Call Display

When the LLM returns tool calls, they will be displayed with a 🔧 (wrench/tool) symbol:

```
🔧 Function Call: run_shell_command
Arguments: {"command": "ls -la"}
```

## Features

- **OpenAI-compatible tools format**: Uses the standard OpenAI tools API format
- **Multiple tools**: You can define multiple tools in a single request
- **Tool call responses**: Tool calls are displayed with clear formatting and the 🔧 symbol
- **Provider support**: Works with both GitHub Copilot and LiteLLM providers
- **Automatic parsing**: JSON requests are automatically detected and parsed

## Tool Definition Structure

Each tool must have:

- `type`: Always "function"
- `function`: Object containing:
  - `name`: Function name (string)
  - `description`: What the function does (string)
  - `parameters`: JSON Schema object describing the parameters
    - `type`: "object"
    - `properties`: Object with parameter definitions
    - `required`: Array of required parameter names

## Examples

See [test_tools.json](test_tools.json) and [test_tools.sh](test_tools.sh) for working examples.

## Technical Details

The implementation:

1. Parses JSON requests from stdin or arguments
2. Extracts the tools array from the request
3. Passes tools to the LLM provider via the API
4. Parses tool_calls from the streaming or non-streaming response
5. Displays tool calls with the 🔧 symbol prefix
6. Updates conversation history with tool call information

## Notes

- Tool execution is NOT automatic - tool calls are displayed but not executed
- The LLM decides whether to use tools based on the context
- Not all models support function calling - check your model's capabilities
- Tool calls are stored in conversation history for multi-turn interactions
