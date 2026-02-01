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

### Single Question Mode

Ask a question directly:

```bash
llm What is the capital of France?
llm How do I list all files recursively in bash?
llm "Explain what this means: $(cat somefile.txt)"
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

### Piping Input
```bash
cat error.log | xargs -I {} llm "Explain this error: {}"
```

## Unloading

To unload the builtin from your bash session:

```bash
enable -d llm
```

## Notes

- The builtin uses the GitHub Copilot API which requires an active subscription
- Responses are streamed from the API
- The interactive mode uses simple line input (no advanced readline features)
- All messages are stateless (no conversation history is maintained between separate invocations)
