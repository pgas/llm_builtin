#ifndef LITELLM_PROVIDER_H
#define LITELLM_PROVIDER_H

#include "llm_provider.h"
#include <string>

/**
 * LiteLLM gateway provider implementation.
 * Handles communication with LiteLLM - a gateway/proxy for LLM APIs.
 * Assumes LiteLLM is running locally and provides an OpenAI-compatible API.
 */
class LiteLLMProvider : public LLMProvider {
public:
    LiteLLMProvider();
    virtual ~LiteLLMProvider();

    bool initialize() override;
    void cleanup() override;
    bool authenticate() override;
    bool send_message(
        const std::string& message,
        const std::vector<json>& history,
        const std::string& system_message,
        const json& tools,
        std::string& response,
        json* tool_calls = nullptr) override;
    std::string get_provider_name() const override;
    std::string get_model_name() const override;
    void set_model(const std::string& model_name) override;
    std::vector<std::string> get_available_models() const override;

private:
    // Configuration
    std::string get_base_url();
    std::string get_model_name();
    std::string get_api_token();
    bool load_config();
    
    // Communication
    bool check_server_available();
    
    // File paths
    std::string get_llm_dir_path();
    std::string get_config_file_path();
    
    // Configuration values
    std::string base_url_;
    std::string model_name_;
    std::string api_token_;
};

#endif /* LITELLM_PROVIDER_H */
