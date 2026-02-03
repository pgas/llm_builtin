# LLM Bash Builtin - GitHub Copilot Chat

This bash builtin allows you to chat with GitHub Copilot's LLM directly from your bash terminal.

## Building

```bash
mkdir -p build
cd build
cmake ..
make
```

## Installation

After building, install the builtin:

```bash
cd build
cmake --install .
```

This will install the builtin to `/usr/local/lib/bas/llm.so` by default.

Load the builtin into your bash session:

```bash
enable -f /usr/local/lib/bas/llm.so llm
```

Or add to your `.bashrc`:

```bash
enable -f /usr/local/lib/bas/llm.so llm
```

## Configuration

The builtin reads its configuration from `~/.bash_llm/config.json`.

### Provider Selection

Create or edit `~/.bash_llm/config.json` to select your provider:

```json
{
  "provider": "copilot"
}
```

Or for LiteLLM:

```json
{
  "provider": "litellm",
  "litellm": {
    "base_url": "http://localhost:8000",
    "model": "gpt-3.5-turbo",
    "api_key": "your-api-key"
  }
}
```

### GitHub Copilot (Default)

If no configuration file exists, the builtin defaults to GitHub Copilot:

### LiteLLM

LiteLLM configuration is specified in the same config file:

```json
{
  "provider": "litellm",
  "litellm": {
    "base_url": "http://localhost:8000",
    "model": "gpt-3.5-turbo",
    "api_key": "your-litellm-api-key"
  }
}
```

Configuration options:
- `base_url`: URL where LiteLLM is running (default: `http://localhost:8000`)
- `model`: Model to use (default: `gpt-3.5-turbo`)
- `api_key`: Optional API token for authentication (sent as `Authorization: Bearer <token>`)

After editing the configuration, reload it with:

```bash
llm -r
```

The LiteLLM provider expects:
- A local LiteLLM gateway running at the configured URL
- OpenAI-compatible API endpoint at `/v1/chat/completions`
- Health check endpoint at `/health`
- Optional token-based authentication via `api_key` config

## Authentication

You'll need a GitHub Copilot subscription to use this builtin. On first use, the builtin will automatically guide you through GitHub's OAuth device flow:

1. You'll be shown a verification URL and a code
2. Visit the URL in your browser and enter the code
3. Authorize the application
4. The builtin will fetch your GitHub Copilot token
5. Credentials are saved to `~/.bash_llm/copilot_auth.json` with secure permissions (600)

The credentials are stored locally and the builtin will automatically refresh your Copilot token as needed (tokens expire after 1 hour but the long-lived access token allows automatic refresh).

## Custom Instructions

When the builtin is loaded, it ensures a custom instructions file exists at `~/.bash_llm/instructions.txt`.
Edit this file to provide default guidance applied to every chat request.
Lines starting with `#` are treated as comments.

Default contents:
```
Assume shell scripting context (bash).
Assume the user is currently using a command line interface.
Optimize output for terminal display (plain text, no rich formatting).
```

## Token Management

Your credentials are automatically managed by the builtin:

- **Storage**: `~/.bash_llm/copilot_auth.json` (permissions: 600)
- **Format**: JSON file containing:
  - `access_token`: GitHub OAuth token (long-lived)
  - `copilot_token`: GitHub Copilot API token (1-hour expiry)
  - `expires_at`: Unix timestamp of when the Copilot token expires

**No manual token refresh needed!** The builtin will:
- Check if your Copilot token is expired
- Automatically refresh it using the access token if needed
- Save the new token back to `~/.bash_llm/copilot_auth.json`

## Setup (One-time)

After building the project:

```bash
# Load the builtin into bash
enable -f ./build/src/llm.so llm

# Start using it! (will prompt for authentication on first use)
llm What is the capital of France?
```

To make it permanent, add to your `~/.bashrc`:

```bash
enable -f /path/to/llm_builtin/build/src/llm.so llm
```

## Usage

### Command Options

```bash
llm [-i] [-n] [-r] [-h] [message...]
```

Options:
- `-i`: Start interactive chat mode
- `-n`: Start a new chat (clear conversation history)
- `-r`: Reload configuration from `~/.bash_llm/config.json`
- `-h`: Show help with current provider and model

### Single Question Mode

Ask a question directly:

```bash
llm What is the capital of France?
llm How do I list all files recursively in bash?
llm "Explain what this means: $(cat somefile.txt)"
```

### Show Configuration

Check provider and model information:

```bash
llm -h
```

Edit your config file and reload:

```bash
# Edit ~/.bash_llm/config.json to change provider
llm -r    # Reload configuration
llm -h    # Confirm new provider and model
```

### Interactive Chat Mode

Start an interactive chat session:

```bash
llm -i
```

This will open an interactive prompt where you can have a conversation with the LLM:

```
GitHub Copilot Chat (type 'exit' or 'quit' to end)
================================================

You: What is recursion?

Copilot: [Response from Copilot]

You: Give me an example in Python

Copilot: [Response from Copilot]

You: exit
```

#### Interactive Mode with Piped Input

When using `-i` with piped input (non-TTY), each line from stdin is treated as a separate prompt:

```bash
# Process multiple prompts from a file
cat prompts.txt | llm -i

# Or from a heredoc
llm -i <<EOF
What is Python?
What is JavaScript?
EOF
```

Each line will be sent as a separate message and the response will be displayed before processing the next line.

## Examples

```bash
# Get help with a bash command
llm How do I use find to search for files modified in the last 24 hours?

# Debug code
llm "Why doesn't this work: for i in {1..10}; do echo $i; done | grep 5"

# Get coding help
llm Write a Python function to calculate fibonacci numbers

# Interactive mode for longer conversations
llm -i

# Pipe stdin to add context to your message
cat error.log | llm "What does this error mean?"
ps aux | llm "Which process is using the most memory?"

# Pipe code for review
cat script.sh | llm "Review this bash script for potential issues"

# Multiple prompts via pipe in interactive mode
echo -e "Explain recursion\nShow me an example" | llm -i
```

## Troubleshooting

### Credentials Not Found
If the builtin can't find credentials, it will automatically prompt you to authenticate on your next use.

### Token Expired
The builtin automatically handles token expiration by refreshing them using the stored access token. No manual action needed.

### Connection Errors
Make sure you have internet connectivity and can reach the GitHub API:
```bash
curl -I https://api.github.com
curl -I https://api.githubcopilot.com
```

### No Response
If the command hangs or returns no response, check:
1. Your credentials are in `~/.bash_llm/copilot_auth.json`
2. You have an active GitHub Copilot subscription
3. Network connectivity is working
4. The GitHub Copilot API is accessible

## Advanced Usage

### Stdin Handling

The builtin intelligently handles stdin based on whether it's connected to a TTY:

**Non-Interactive Mode (no `-i` flag):**
- All stdin content is appended to your message
- Useful for adding context from files or command output

```bash
# Add file content to your prompt
cat config.json | llm "Explain this configuration"

# Add command output to your prompt  
ls -la | llm "Organize these files by type"

# Combine arguments with stdin
cat error.log | llm "This is the error log" "What went wrong?"
```

**Interactive Mode with TTY:**
- Standard interactive prompt where you type each message
- Special commands available: `/exit`, `/quit`, `/new`, `/help`

```bash
llm -i
> What is recursion?
[response]
> Give me an example
[response]
> /exit
```

**Interactive Mode with Piped Input (non-TTY):**
- Each line from stdin is sent as a separate prompt
- Responses are displayed sequentially
- No special commands are processed

```bash
echo -e "What is 2+2?\nWhat is 3+3?" | llm -i
# First sends "What is 2+2?", waits for response
# Then sends "What is 3+3?", waits for response
```

### Piping Examples
```bash
# Analyze log files
cat error.log | llm "Summarize the errors in this log"

# Code review
git diff | llm "Review these changes"

# Batch processing
cat questions.txt | llm -i  # Each line is a separate question
```

## Unloading

To unload the builtin from your bash session:

```bash
enable -d llm
```

## Notes

- By default, the builtin uses GitHub Copilot, which requires an active subscription
- Configure the provider in `~/.bash_llm/config.json`
- Use `llm -r` to reload configuration after making changes
- Use `llm -s` to see which provider is currently active
- For LiteLLM, configure the base URL, model, and API key in the config file
- Responses are streamed from the API
- The interactive mode uses simple line input (no advanced readline features)
- Conversation history is maintained in memory during the shell session
