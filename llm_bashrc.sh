# LLM Builtin Configuration for Bash
# Add this to your ~/.bashrc or source it with: source /path/to/llm_bashrc.sh

# Path to the llm builtin shared library
# Adjust this to match your installation
LLM_BUILTIN_PATH="${HOME}/proj/llm_builtin/build/src/llm.so"

# Load the llm builtin if it exists
if [ -f "$LLM_BUILTIN_PATH" ]; then
    enable -f "$LLM_BUILTIN_PATH" llm
    
    # Bind Ctrl-O to trigger AI-powered command completion
    # Press Ctrl-O while typing to complete your command
    bind -x '"\C-o": llm -c'
    
    # Optional: Add an alias for quick access to interactive mode
    alias llmi='llm -i'
    
    # Optional: Function to quickly reload llm configuration
    reload_llm() {
        llm -r
    }
    
    echo "LLM builtin loaded. Press Ctrl-O for AI completion, or type 'llm -h' for help."
else
    echo "Warning: LLM builtin not found at $LLM_BUILTIN_PATH"
fi
