#!/bin/bash
# Example script demonstrating how to use the llm builtin

# This script shows you how to integrate the llm builtin into your workflow

# 1. Load the builtin
enable -f "$(dirname "$0")/build/src/llm.so" llm

# 2. Check if credentials are set up
if [ ! -f ~/.bash_llm/copilot_auth.json ]; then
    echo "Error: Credentials not found at ~/.bash_llm/copilot_auth.json"
    echo "Please run: bash get_token.sh"
    exit 1
fi

echo "=== LLM Builtin Examples ==="
echo

# Example 1: Ask a simple question
echo "Example 1: Simple question"
echo "$ llm What is the square root of 144?"
llm What is the square root of 144?
echo

# Example 2: Get help with a bash command
echo "Example 2: Bash command help"
echo "$ llm How do I find all .txt files in the current directory?"
llm How do I find all .txt files in the current directory?
echo

# Example 3: Code explanation
echo "Example 3: Explain code"
cmd='awk '\''{print $2}'\'' file.txt'
echo "$ llm Explain this command: $cmd"
llm "Explain this command: $cmd"
echo

# Example 4: Use in a pipeline
echo "Example 4: Pipeline usage"
echo "$ echo 'def fib(n): return n if n <= 1 else fib(n-1) + fib(n-2)' | xargs -I {} llm 'Explain this code: {}'"
echo 'def fib(n): return n if n <= 1 else fib(n-1) + fib(n-2)' | xargs -I {} llm "Explain this code: {}"
echo

echo "=== Interactive Mode ==="
echo "To start interactive chat, run:"
echo "  llm -i"
echo
echo "This will open a chat session where you can have a conversation."
