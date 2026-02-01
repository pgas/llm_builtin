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

# Show help
echo -e "${YELLOW}Builtin help:${NC}"
help llm
echo

# Ask a simple question
echo -e "${YELLOW}Testing with a simple question...${NC}"
echo -e "${GREEN}Running: llm What is 2+2?${NC}"
echo
if llm What is 2+2?; then
    echo
    echo -e "${GREEN}Test complete!${NC}"
else
    echo
    echo -e "${RED}Test failed!${NC}"
    exit 1
fi
echo
echo -e "${YELLOW}Testing new chat option (-n)...${NC}"
echo -e "${GREEN}Running: llm -n Say 'OK'${NC}"
echo
if llm -n Say OK; then
    echo
    echo -e "${GREEN}✓ -n option smoke test passed${NC}"
else
    echo
    echo -e "${RED}-n option test failed!${NC}"
    exit 1
fi
echo

# Optional, best-effort context persistence test (may be flaky depending on model behavior)
if [[ "${LLM_CONTEXT_TEST:-}" == "1" ]]; then
    echo -e "${YELLOW}Testing context persistence (LLM_CONTEXT_TEST=1)...${NC}"
    echo -e "${GREEN}Running: llm Remember the word KITTENS. Reply ONLY with MEMORIZED.${NC}"
    echo
    if ! llm "Remember the word KITTENS. Reply ONLY with MEMORIZED." > /tmp/llm_ctx_1.txt; then
        echo -e "${RED}Context test step 1 failed!${NC}"
        exit 1
    fi

    echo -e "${GREEN}Running: llm What word did I ask you to memorize? Reply ONLY with the word.${NC}"
    echo
    if ! llm "What word did I ask you to memorize? Reply ONLY with the word." > /tmp/llm_ctx_2.txt; then
        echo -e "${RED}Context test step 2 failed!${NC}"
        exit 1
    fi

    if grep -qi "KITTENS" /tmp/llm_ctx_2.txt; then
        echo -e "${GREEN}✓ Context appears to persist across invocations${NC}"
    else
        echo -e "${RED}Context persistence check failed (expected KITTENS)${NC}"
        exit 1
    fi

    echo -e "${GREEN}Running: llm -n What word did I ask you to memorize? Reply ONLY with the word.${NC}"
    echo
    if ! llm -n "What word did I ask you to memorize? Reply ONLY with the word." > /tmp/llm_ctx_3.txt; then
        echo -e "${RED}Context test reset step failed!${NC}"
        exit 1
    fi

    if grep -qi "KITTENS" /tmp/llm_ctx_3.txt; then
        echo -e "${RED}New chat reset check failed (still saw KITTENS)${NC}"
        exit 1
    else
        echo -e "${GREEN}✓ New chat reset appears to clear context${NC}"
    fi

    rm -f /tmp/llm_ctx_1.txt /tmp/llm_ctx_2.txt /tmp/llm_ctx_3.txt
    echo
fi

echo "You can now use the llm builtin:"
echo "  - Ask questions: llm How do I list files in bash?"
echo "  - Interactive mode: llm -i"
echo "  - New chat: llm -n"
echo
echo "To load automatically, add this to your ~/.bashrc:"
echo "  enable -f $(pwd)/build/src/llm.so llm"
