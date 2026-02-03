#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Abstract interface for LLM provider interactions.
 * Concrete implementations provide access to specific LLM services
 * (e.g., GitHub Copilot, OpenAI, Anthropic, etc.)
 */
class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    /**
     * Initialize the provider (called once during builtin load).
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;

    /**
     * Cleanup provider resources (called during builtin unload).
     */
    virtual void cleanup() = 0;

    /**
     * Ensure provider is authenticated and ready to send messages.
     * May trigger interactive authentication if needed.
     * @return true if authenticated, false otherwise
     */
    virtual bool authenticate() = 0;

    /**
     * Send a message to the LLM and get a response.
     * @param message The user's message
     * @param history The conversation history
     * @param system_message Optional system message/instructions
     * @param tools Optional tools/functions available to the LLM
     * @param response Output parameter for the LLM's response
     * @param tool_calls Output parameter for any tool_calls from the response (optional)
     * @return true on success, false on failure
     */
    virtual bool send_message(
        const std::string& message,
        const std::vector<json>& history,
        const std::string& system_message,
        const json& tools,
        std::string& response,
        json* tool_calls = nullptr) = 0;

    /**
     * Get the name of this provider.
     * @return Provider name (e.g., "GitHub Copilot", "OpenAI")
     */
    virtual std::string get_provider_name() const = 0;

    /**
     * Get the model name being used.
     * @return Model name (e.g., "gpt-4", "claude-3-sonnet")
     */
    virtual std::string get_model_name() const = 0;
    
    /**
     * Set the model name to use.
     * @param model_name The model name to use
     */
    virtual void set_model(const std::string& model_name) = 0;
};

#endif /* LLM_PROVIDER_H */
