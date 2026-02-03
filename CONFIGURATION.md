# Configuration Examples

This document provides examples of how to configure the LLM builtin using the JSON configuration file.

## Configuration File Location

The configuration file is located at `~/.bash_llm/config.json`.

## Example Configurations

### GitHub Copilot (Default)

Minimal configuration for GitHub Copilot:

```json
{
  "provider": "copilot"
}
```

### LiteLLM with Local Server

Configuration for LiteLLM running locally:

```json
{
  "provider": "litellm",
  "litellm": {
    "base_url": "http://localhost:8000",
    "model": "gpt-3.5-turbo",
    "api_key": ""
  }
}
```

### LiteLLM with Custom Model

Using a different model with LiteLLM:

```json
{
  "provider": "litellm",
  "litellm": {
    "base_url": "http://localhost:8000",
    "model": "claude-3-sonnet-20240229",
    "api_key": "your-api-key-here"
  }
}
```

### LiteLLM with Remote Server

Configuration for LiteLLM running on a remote server:

```json
{
  "provider": "litellm",
  "litellm": {
    "base_url": "https://llm-proxy.example.com",
    "model": "gpt-4",
    "api_key": "your-secure-api-key"
  }
}
```

## Switching Between Providers

To switch from one provider to another:

1. Edit `~/.bash_llm/config.json`
2. Change the `"provider"` field
3. Run `llm -r` to reload the configuration
4. Run `llm -h` to verify the new provider and model

Example workflow:

```bash
# Check current provider and model
$ llm -h
LLM Bash Builtin

Current Configuration:
  Provider: GitHub Copilot
  Model: gpt-4o

...

# Edit config file
$ nano ~/.bash_llm/config.json
# Change "provider": "copilot" to "provider": "litellm"

# Reload configuration
$ llm -r
Configuration reloaded. Using provider: LiteLLM

# Verify new provider and model
$ llm -h
LLM Bash Builtin

Current Configuration:
  Provider: LiteLLM
  Model: gpt-3.5-turbo

...
```

## Configuration Management Tips

1. **Keep a backup**: Save a copy of your working configuration
   ```bash
   cp ~/.bash_llm/config.json ~/.bash_llm/config.json.backup
   ```

2. **Test changes**: After modifying the config, test with a simple query:
   ```bash
   llm -r && llm "test message"
   ```

3. **Multiple environments**: Use different configs for different machines/contexts
   ```bash
   # Work machine (uses company LiteLLM proxy)
   cp ~/.bash_llm/config.work.json ~/.bash_llm/config.json
   llm -r
   
   # Personal machine (uses GitHub Copilot)
   cp ~/.bash_llm/config.personal.json ~/.bash_llm/config.json
   llm -r
   ```

## Configuration Precedence

1. If `~/.bash_llm/config.json` exists and has a valid `"provider"` field, that provider is used
2. If the config file doesn't exist or is invalid, GitHub Copilot is used as the default
3. Provider-specific settings (like `litellm` block) are only read when that provider is active

## Troubleshooting

### Invalid JSON

If your config file has invalid JSON, you'll see an error:

```bash
$ llm -r
Warning: Failed to parse config file: [error details]
Using default provider (copilot)
```

Fix: Validate your JSON using a tool like `jq`:
```bash
cat ~/.bash_llm/config.json | jq .
```

### Provider Not Changing

If `llm -h` shows the old provider after `llm -r`:

1. Check that you saved the config file
2. Verify the JSON is valid
3. Check that the `"provider"` value is exactly `"copilot"` or `"litellm"` (case-sensitive)

### LiteLLM Connection Issues

If using LiteLLM and getting connection errors:

1. Verify the server is running: `curl http://localhost:8000/health`
2. Check the `base_url` in your config matches the server address
3. Ensure the `api_key` is correct (if required by your LiteLLM setup)
