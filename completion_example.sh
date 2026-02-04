#!/bin/bash
# Example: Using llm builtin with readline completion via bind -x
#
# This demonstrates how to bind the llm completion feature to a key sequence.
# The -c flag makes llm read READLINE_LINE, send it to the LLM for completion,
# and update READLINE_LINE with the result.

# Make sure the llm builtin is loaded
if ! type -t llm &>/dev/null; then
    echo "Error: llm builtin not loaded"
    echo "Load it in your .bashrc with:"
    echo '  enable -f /path/to/llm_builtin.so llm'
    exit 1
fi

echo "=== LLM Readline Completion Example ==="
echo ""
echo "The llm builtin is now enhanced with completion support!"
echo ""
echo "Usage:"
echo "  1. Bind a key to invoke completion:"
echo "     bind -x '\"\\C-o\": llm -c'"
echo ""
echo "  2. Type a partial command and press Ctrl-O"
echo ""
echo "Example bindings:"
echo "  bind -x '\"\\C-o\": llm -c'     # Ctrl-O for completion"
echo "  bind -x '\"\\eo\": llm -c'      # Alt-O for completion"
echo "  bind -x '\"\\e[24~\": llm -c'   # F12 for completion"
echo ""
echo "Try it now! Setting up Ctrl-O binding..."
bind -x '"\C-o": llm -c'

echo ""
echo "Binding configured! Press Ctrl-O while typing to complete your command."
echo ""
echo "Examples to try:"
echo "  - Type: find . -name"
echo "    Press Ctrl-O to complete"
echo ""
echo "  - Type: docker run -d -p"
echo "    Press Ctrl-O to complete"
echo ""
echo "  - Type: git commit -m"
echo "    Press Ctrl-O to complete"
echo ""
echo "Press Enter to continue with interactive bash..."
read -r

# Start an interactive bash session with the binding active
bash --rcfile <(echo "
# Preserve current environment
$(declare -p)
$(declare -f)

# Re-enable the llm builtin and set up the binding
bind -x '\"\\C-o\": llm -c'

echo 'LLM completion ready! Press Ctrl-O to complete the current line.'
echo 'Type \"exit\" to leave this demo.'
")
