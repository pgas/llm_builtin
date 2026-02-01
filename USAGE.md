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

You'll need a GitHub Copilot token to use this builtin. Use the provided script to get it:

```bash
./get_token.sh
```

This script will guide you through the authentication process and retrieve your GitHub Copilot token.

## Setting the Token

Export your GitHub Copilot token as an environment variable:

```bash
export GITHUB_COPILOT_TOKEN='your_token_here'
```

Add this to your `.bashrc` or `.bash_profile` for persistence:

```bash
echo 'export GITHUB_COPILOT_TOKEN="your_token_here"' >> ~/.bashrc
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

### Token Not Set
If you see:
```
Error: GITHUB_COPILOT_TOKEN environment variable not set
```

Make sure you've exported the token:
```bash
export GITHUB_COPILOT_TOKEN='your_token_here'
```

### Invalid Token
If you get authentication errors, your token may have expired. GitHub Copilot tokens typically expire after some time and need to be refreshed.

### Connection Errors
Make sure you have internet connectivity and can reach `api.githubcopilot.com`:
```bash
curl -I https://api.githubcopilot.com
```

### No Response
If the command hangs or returns no response, check:
1. Your token is valid
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
