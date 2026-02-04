#ifndef COPILOT_PROVIDER_H
#define COPILOT_PROVIDER_H

#include "llm_provider.h"
#include <string>
#include <ctime>

/**
 * GitHub Copilot LLM provider implementation.
 * Handles authentication via GitHub OAuth and communication with
 * the GitHub Copilot API.
 */
class CopilotProvider : public LLMProvider {
public:
    CopilotProvider();
    virtual ~CopilotProvider();

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
    void set_model(const std::string& model_name);
    std::vector<std::string> get_available_models() const override;

private:
    // Credential management
    bool load_credentials(std::string& copilot_token, std::string& access_token, time_t& expires_at);
    bool save_credentials(const std::string& copilot_token, const std::string& access_token, time_t expires_at);
    bool refresh_copilot_token(std::string& copilot_token, const std::string& access_token, time_t& expires_at);
    time_t parse_token_expiration(const std::string& copilot_token);
    bool get_copilot_token(std::string& copilot_token);

    // OAuth device flow
    bool get_device_code(std::string& device_code, std::string& user_code, 
                         std::string& verification_uri, int& interval);
    bool poll_for_access_token(const std::string& device_code, std::string& access_token);
    bool authenticate_with_github(std::string& copilot_token, std::string& access_token, time_t& expires_at);

    // Response parsing
    std::string extract_content_from_sse(const std::string& response, json* tool_calls);

    // File paths
    std::string get_llm_dir_path();
    std::string get_auth_file_path();
    std::string get_config_file_path();
    
    // Configuration
    bool load_config();
    
    // Member variables
    std::string model_name_;
};

#endif /* COPILOT_PROVIDER_H */
