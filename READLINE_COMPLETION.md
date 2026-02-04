# Readline Completion with LLM

The `llm` builtin now supports intelligent command-line completion using the `-c` flag, which integrates with bash's `bind -x` feature to provide LLM-powered completions directly in your readline session.

## How It Works

When you use `bind -x` to bind a key sequence to `llm -c`, bash automatically sets special variables:
- `READLINE_LINE` - The current contents of the readline buffer
- `READLINE_POINT` - The cursor position in the buffer
- `READLINE_MARK` - The saved insertion point (if any)

The `llm -c` command:
1. Reads the current line from `READLINE_LINE`
2. Sends it to the LLM with a completion prompt
3. Updates `READLINE_LINE` with the completed command
4. Sets `READLINE_POINT` to the end of the completion

This all happens internally using bash's C API functions like `bind_variable()` and `find_variable()`.

## Setup

Add this to your `~/.bashrc` or `~/.bash_profile`:

```bash
# Enable the llm builtin
enable -f /path/to/llm_builtin.so llm

# Bind Ctrl-O to trigger LLM completion
bind -x '"\C-o": llm -c'
```

## Usage

1. Start typing a command:
   ```
   $ find . -name
   ```

2. Press `Ctrl-O` (or your configured key binding)

3. The LLM will complete your command:
   ```
   $ find . -name "*.txt" -type f
   ```

4. Press Enter to execute or continue editing

## Key Binding Examples

You can bind completion to any key sequence:

```bash
# Ctrl-O (recommended)
bind -x '"\C-o": llm -c'

# Alt-O
bind -x '"\eo": llm -c'

# Ctrl-Space (might conflict with other bindings)
bind -x '"\C-@": llm -c'

# F12
bind -x '"\e[24~": llm -c'

# Ctrl-x then Ctrl-c
bind -x '"\C-x\C-c": llm -c'
```

## Configuration

The completion feature uses your configured LLM provider (Copilot or LiteLLM) and model. You can override the model for completion by editing your `~/.bash_llm/config.json`:

```json
{
  "provider": "copilot",
  "copilot": {
    "model": "gpt-4o"
  }
}
```

## Implementation Details

Unlike helper script approaches, this implementation works entirely within the builtin:

```c++
// The builtin directly accesses bash variables
SHELL_VAR *readline_line_var = find_variable("READLINE_LINE");
std::string current_line = readline_line_var->value;

// ... send to LLM ...

// Update the line directly
bind_variable("READLINE_LINE", completion.c_str(), 0);
bind_variable("READLINE_POINT", point_str, 0);
```

This is more efficient and reliable than spawning subshells or using temporary files.

## Tips

1. **Use specific prompts**: The more context you provide in your partial command, the better the completion
   
2. **Try multiple completions**: If the first completion isn't what you want, you can:
   - Edit the completion and press Ctrl-O again
   - Use Ctrl-C to cancel and start over
   
3. **History integration**: Completed commands are added to your bash history normally

4. **Works everywhere**: Since this uses `bind -x`, it works at any bash prompt, including:
   - Interactive shells
   - After pipes and redirects
   - In command substitutions that use the terminal

## Comparison with Programmable Completion

Traditional bash programmable completion (using `complete -F` or `compgen`) is designed for completing specific arguments (file names, options, etc.) based on the current word being typed.

The `llm -c` approach is different:
- **Whole-line completion**: Completes the entire command line, not just the current word
- **Context-aware**: Uses LLM understanding to suggest logical completions
- **Flexible**: Works for any command, not just ones with defined completions
- **Intent-based**: Can understand partial intent and complete accordingly

Example:
```bash
# Traditional completion would complete filenames after -name
$ find . -name <TAB>

# LLM completion understands intent and completes the whole pattern
$ find . -name <Ctrl-O>
$ find . -name "*.txt" -type f -mtime -7
```

## Troubleshooting

**"READLINE_LINE not set" error**:
- This means you called `llm -c` directly instead of through `bind -x`
- The `-c` flag only works when invoked via a keybinding

**Completion replaces the entire line**:
- This is by design - the LLM provides a complete command
- If you want to keep part of your line, the LLM should preserve it in its completion

**Slow response**:
- LLM API calls take time (typically 1-3 seconds)
- Consider using a faster model if available
- You can press Ctrl-C to cancel a completion request

## Advanced Usage

You can create different bindings for different types of completion:

```bash
# Standard completion
bind -x '"\C-o": llm -c'

# With a specific model for quick completions
# (This would require a -m flag in the binding, which could be added)
bind -x '"\eo": llm -c'
```

## Future Enhancements

Possible improvements:
- Add a flag to append instead of replace
- Support for completion of specific words only
- Integration with bash-completion for hybrid approach
- Multiple completion suggestions with menu selection
