# LLM Bash Builtin - GitHub Copilot Chat

A bash builtin that enables direct chat interaction with GitHub Copilot's LLM from your terminal.

(Written with the help of an llms)

## Quick Start

### 1. Build

```bash
mkdir -p build && cd build
cmake ..
make
```

### 2. Load the Builtin

```bash
enable -f /path/to/llm_builtin/build/src/llm.so llm
```

### 3. Set Your Token

```bash
export GITHUB_COPILOT_TOKEN='your_token_here'
```

See [USAGE.md](USAGE.md) for detailed instructions on obtaining a GitHub Copilot token.

### 4. Start Chatting

```bash
# Ask a question
llm How do I use grep to search recursively?

# Interactive mode
llm -i
```

## Usage

```bash
llm [options] [message...]

Options:
  -i    Start interactive chat mode

Examples:
  llm What is the capital of France?
  llm Explain what this bash command does: find . -name "*.txt"
  llm -i    # Start interactive chat
```

## Requirements

- Linux system with bash 4.0+
- CMake 3.10+
- libcurl development libraries
- GitHub Copilot subscription and token

## Documentation

See [USAGE.md](USAGE.md) for complete documentation including:
- How to obtain a GitHub Copilot token
- Detailed usage examples
- Troubleshooting guide
- Advanced usage patterns

## Testing

Run the test script to verify everything is working:

```bash
./test.sh
```

## Original Project

This project was originally a simple cmake template for a bash loadable builtin.
The original autoconf project is in the autoconf branch.
