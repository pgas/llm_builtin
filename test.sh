#!/bin/bash
# Test script for the LLM bash builtin

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "LLM Bash Builtin Test Script"
echo "============================"
echo

# Check if llm.so exists
if [ ! -f "build/src/llm.so" ]; then
    echo -e "${RED}Error: llm.so not found. Please build the project first:${NC}"
    echo "  mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

# Load the builtin
echo -e "${YELLOW}Loading llm builtin...${NC}"
enable -f "$(pwd)/build/src/llm.so" llm

if ! type llm &> /dev/null; then
    echo -e "${RED}Error: Failed to load llm builtin${NC}"
    exit 1
fi

echo -e "${GREEN}✓ LLM builtin loaded successfully${NC}"
echo

# Check for token
if [ -z "$GITHUB_COPILOT_TOKEN" ]; then
    echo -e "${RED}Error: GITHUB_COPILOT_TOKEN not set${NC}"
    echo
    echo "Please set your GitHub Copilot token:"
    echo "  export GITHUB_COPILOT_TOKEN='your_token_here'"
    echo
    echo "See USAGE.md for instructions on how to obtain a token."
    exit 1
fi

echo -e "${GREEN}✓ GITHUB_COPILOT_TOKEN is set${NC}"
echo

# Show help
echo -e "${YELLOW}Builtin help:${NC}"
help llm
echo

# Ask a simple question
echo -e "${YELLOW}Testing with a simple question...${NC}"
echo -e "${GREEN}Running: llm What is 2+2?${NC}"
echo
llm What is 2+2?
echo

echo -e "${GREEN}Test complete!${NC}"
echo
echo "You can now use the llm builtin:"
echo "  - Ask questions: llm How do I list files in bash?"
echo "  - Interactive mode: llm -i"
echo
echo "To load automatically, add this to your ~/.bashrc:"
echo "  enable -f $(pwd)/build/src/llm.so llm"
echo "  export GITHUB_COPILOT_TOKEN='your_token_here'"
