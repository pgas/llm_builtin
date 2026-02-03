#!/bin/bash
# Test script for the new configuration system

echo "=== Testing LLM Builtin Configuration System ==="
echo

# Create test config directory
TEST_DIR="$HOME/.bash_llm"
mkdir -p "$TEST_DIR"

# Create a test config file
echo "Creating test config file..."
cat > "$TEST_DIR/config.json" << EOF
{
  "provider": "copilot",
  "litellm": {
    "base_url": "http://localhost:8000",
    "model": "gpt-3.5-turbo",
    "api_key": "test-key"
  }
}
EOF

echo "Config file created at $TEST_DIR/config.json"
echo

# Load the builtin
echo "Loading builtin..."
enable -f ./build/src/llm.so llm

# Test -h option to show help and configuration
echo "Testing -h option (show help and configuration):"
llm -h
echo

# Update config to litellm
echo "Changing provider to litellm..."
cat > "$TEST_DIR/config.json" << EOF
{
  "provider": "litellm",
  "litellm": {
    "base_url": "http://localhost:8000",
    "model": "gpt-3.5-turbo",
    "api_key": "test-key"
  }
}
EOF

# Test -r option to reload
echo "Testing -r option (reload config):"
llm -r
echo

# Show configuration again
echo "Confirming new provider and model:"
llm -h
echo

# Change back to copilot
echo "Changing back to copilot..."
cat > "$TEST_DIR/config.json" << EOF
{
  "provider": "copilot"
}
EOF

llm -r
llm -h

echo
echo "=== Configuration system test complete ==="
