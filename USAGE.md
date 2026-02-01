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

## Getting GitHub Copilot Token

You'll need a GitHub Copilot subscription to use this builtin. Use the provided script to authenticate:

```bash
./get_token.sh
```

This script will:
1. Guide you through GitHub's OAuth device flow
2. Open a browser for you to authenticate
3. Fetch your GitHub Copilot token
4. Save both your OAuth access token and Copilot token to `~/.copilot_auth`
5. Set file permissions to 600 (secure)

The credentials will be stored locally and the builtin will automatically refresh your Copilot token as needed (tokens expire after 1 hour but the long-lived access token allows automatic refresh).

## Token Management

Your credentials are automatically managed by the builtin:

- **Storage**: `~/.copilot_auth` (permissions: 600)
- **Format**: JSON file containing:
  - `access_token`: GitHub OAuth token (long-lived)
  - `copilot_token`: GitHub Copilot API token (1-hour expiry)
  - `expires_at`: Unix timestamp of when the Copilot token expires

**No manual token refresh needed!** The builtin will:
- Check if your Copilot token is expired
- Automatically refresh it using the access token if needed
- Save the new token back to `~/.copilot_auth`

## Setup (One-time)

After building the project:

```bash
# Generate and save credentials
bash get_token.sh

# Load the builtin into bash
enable -f ./build/src/llm.so llm

# Start using it!
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
If you see:
```
Error: No credentials found at ~/.copilot_auth
```

Run the token generator script:
```bash
bash get_token.sh
```

### Token Expired
The builtin automatically handles expired tokens by refreshing them. If you see token-related errors, try:
```bash
bash get_token.sh
```

This will update your credentials file with new tokens.

### Connection Errors
Make sure you have internet connectivity and can reach the GitHub API:
```bash
curl -I https://api.github.com
curl -I https://api.githubcopilot.com
```

### No Response
If the command hangs or returns no response, check:
1. Your credentials are in `~/.copilot_auth`
2. You have an active GitHub Copilot subscription
3. Network connectivity is working
4. The GitHub Copilot API is accessible

## Advanced Usage

### Piping Input
```bash
cat error.log | xargs -I {} llm "Explain this error: {}"
```

### Using in Scripts
```bash
#!/bin/bash
result=$(llm "Generate a random password")
echo "Generated password: $result"
```

### Command Substitution
```bash
# Get a command explanation
cmd="awk '{print \$2}' file.txt"
llm "Explain this command: $cmd"
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
