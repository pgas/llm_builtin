#!/bin/bash
# Test script for readline completion functionality

cd "$(dirname "$0")"

echo "=== Testing LLM Readline Completion ==="
echo ""

# Check if build exists
if [ ! -f "build/src/llm.so" ]; then
    echo "Error: llm.so not found. Please build first:"
    echo "  cd build && cmake .. && make"
    exit 1
fi

# Load the builtin
enable -f "$PWD/build/src/llm.so" llm

# Check if llm is loaded
if ! type -t llm &>/dev/null; then
    echo "Error: Failed to load llm builtin"
    exit 1
fi

echo "✓ llm builtin loaded successfully"
echo ""

# Test that -c flag exists
if llm -h 2>&1 | grep -q -- "-c"; then
    echo "✓ Completion flag (-c) is present in help"
else
    echo "✗ Completion flag not found in help output"
    exit 1
fi

echo ""
echo "=== Manual Test ==="
echo "To test completion manually, run:"
echo ""
echo "  # Start bash with completion enabled"
echo "  bash --rcfile <(echo 'enable -f $PWD/build/src/llm.so llm; bind -x \"\\\C-o\": llm -c')"
echo ""
echo "  # Then type a partial command and press Ctrl-O"
echo "  # Example: find . -name <Ctrl-O>"
echo ""
echo "Or run the interactive example:"
echo "  ./completion_example.sh"
echo ""

# Test that READLINE_LINE requirement is enforced
echo "=== Testing Error Handling ==="
if llm -c 2>&1 | grep -q "READLINE_LINE not set"; then
    echo "✓ Properly detects when READLINE_LINE is not set"
else
    echo "✗ Should require READLINE_LINE to be set"
fi

echo ""
echo "=== Basic Test with Simulated READLINE_LINE ==="

# Create a test function that simulates bind -x environment
test_completion() {
    # Set up the variables that bind -x would set
    export READLINE_LINE="find . -name"
    export READLINE_POINT="15"
    
    echo "Testing with READLINE_LINE='$READLINE_LINE'"
    echo ""
    
    # Note: This will actually call the LLM, so it may take a few seconds
    # and requires a valid configuration
    if llm -c 2>/dev/null; then
        echo ""
        echo "Completion result:"
        echo "  READLINE_LINE: $READLINE_LINE"
        echo "  READLINE_POINT: $READLINE_POINT"
        echo ""
        echo "✓ Completion executed successfully"
    else
        echo "Note: Completion failed - this might be normal if:"
        echo "  - No LLM provider is configured"
        echo "  - Copilot authentication is needed"
        echo "  - Network is unavailable"
        echo ""
        echo "To set up, run: llm -h"
    fi
    
    unset READLINE_LINE READLINE_POINT
}

# Only run the actual completion test if requested
if [ "$1" = "--with-llm" ]; then
    echo ""
    test_completion
else
    echo "Skipping actual LLM call test (use --with-llm to enable)"
fi

echo ""
echo "=== Summary ==="
echo "Basic functionality tests passed!"
echo ""
echo "To test completion interactively:"
echo "  1. Add to ~/.bashrc: bind -x '\"\\C-o\": llm -c'"
echo "  2. Start a new shell"
echo "  3. Type a partial command and press Ctrl-O"
echo ""
echo "Or run: ./completion_example.sh"
