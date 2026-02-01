#!/bin/bash
# GitHub Copilot Token Generator using OAuth Device Flow

set -e

echo "GitHub Copilot Token Generator"
echo "==============================="
echo

# Check if jq is available
if ! command -v jq &> /dev/null; then
    echo "Error: jq is required but not installed"
    echo "Install it with:"
    echo "  Ubuntu/Debian: sudo apt-get install jq"
    echo "  macOS: brew install jq"
    echo "  Or visit: https://jqlang.github.io/jq/download/"
    exit 1
fi

# GitHub Copilot CLI OAuth Client ID (public)
CLIENT_ID="Iv1.b507a08c87ecfe98"

# Step 1: Request device code
echo "[Step 1/4] Requesting device code from GitHub..."
DEVICE_RESPONSE=$(curl -s -X POST https://github.com/login/device/code \
  -H "Accept: application/json" \
  -H "Content-Type: application/json" \
  -d "{\"client_id\":\"${CLIENT_ID}\",\"scope\":\"read:user\"}")

# Parse the response
DEVICE_CODE=$(echo "$DEVICE_RESPONSE" | jq -r '.device_code')
USER_CODE=$(echo "$DEVICE_RESPONSE" | jq -r '.user_code')
VERIFICATION_URI=$(echo "$DEVICE_RESPONSE" | jq -r '.verification_uri')
INTERVAL=$(echo "$DEVICE_RESPONSE" | jq -r '.interval // 5')

if [ -z "$DEVICE_CODE" ]; then
    echo "Error: Failed to get device code from GitHub"
    echo "Response: $DEVICE_RESPONSE"
    exit 1
fi

# Default interval to 5 seconds if not provided
INTERVAL=${INTERVAL:-5}

echo "✓ Device code received"
echo

# Step 2: Show user what to do
echo "[Step 2/4] Authorization required"
echo "=================================="
echo
echo "Please visit: ${VERIFICATION_URI}"
echo "And enter code: ${USER_CODE}"
echo
read -p "Press Enter once you have completed the authorization in your browser..."
echo
echo "Checking for authorization..."
echo

# Step 3: Poll for access token
MAX_ATTEMPTS=120  # 10 minutes max
ATTEMPT=0

while [ $ATTEMPT -lt $MAX_ATTEMPTS ]; do
    sleep $INTERVAL
    ATTEMPT=$((ATTEMPT + 1))
    
    TOKEN_RESPONSE=$(curl -s -X POST https://github.com/login/oauth/access_token \
      -H "Accept: application/json" \
      -H "Content-Type: application/json" \
      -d "{\"client_id\":\"${CLIENT_ID}\",\"device_code\":\"${DEVICE_CODE}\",\"grant_type\":\"urn:ietf:params:oauth:grant-type:device_code\"}")
    
    # Check if we got an access token
    ACCESS_TOKEN=$(echo "$TOKEN_RESPONSE" | jq -r '.access_token // empty')
    
    if [ -n "$ACCESS_TOKEN" ]; then
        echo "✓ GitHub access token obtained"
        break
    fi
    
    # Check for errors
    ERROR=$(echo "$TOKEN_RESPONSE" | jq -r '.error // empty')
    
    if [ "$ERROR" = "authorization_pending" ]; then
        # Still waiting for user to authorize
        echo -n "."
        continue
    elif [ "$ERROR" = "slow_down" ]; then
        # We're polling too fast, increase interval
        INTERVAL=$((INTERVAL + 5))
        echo -n "."
        continue
    elif [ -n "$ERROR" ]; then
        echo
        echo "Error: $ERROR"
        echo "Response: $TOKEN_RESPONSE"
        exit 1
    fi
done

if [ -z "$ACCESS_TOKEN" ]; then
    echo
    echo "Error: Timeout waiting for authorization"
    exit 1
fi

echo
echo

# Step 4: Get Copilot token
echo "[Step 3/4] Fetching GitHub Copilot token..."
COPILOT_RESPONSE=$(curl -s https://api.github.com/copilot_internal/v2/token \
  -H "Authorization: token ${ACCESS_TOKEN}")

COPILOT_TOKEN=$(echo "$COPILOT_RESPONSE" | jq -r '.token')

if [ -z "$COPILOT_TOKEN" ] || [ "$COPILOT_TOKEN" = "null" ]; then
    echo "Error: Failed to get Copilot token"
    echo "Response: $COPILOT_RESPONSE"
    echo
    echo "Note: You need an active GitHub Copilot subscription"
    exit 1
fi

echo "✓ GitHub Copilot token obtained"
echo

# Step 5: Save tokens to file
echo "[Step 4/4] Saving credentials..."
AUTH_FILE="$HOME/.copilot_auth"

# Create JSON with both tokens and expiry time (1 hour from now)
EXPIRY=$(($(date +%s) + 3600))
AUTH_DATA=$(jq -n \
  --arg access_token "$ACCESS_TOKEN" \
  --arg copilot_token "$COPILOT_TOKEN" \
  --argjson expires_at "$EXPIRY" \
  '{access_token: $access_token, copilot_token: $copilot_token, expires_at: $expires_at}')

echo "$AUTH_DATA" > "$AUTH_FILE"
chmod 600 "$AUTH_FILE"

echo "✓ Credentials saved to $AUTH_FILE"
echo

# Step 6: Display result
echo "[Step 5/5] Success!"
echo "==================="
echo
echo "Your credentials have been saved to: $AUTH_FILE"
echo
echo "The builtin will now automatically refresh your Copilot token when needed."
echo
echo "Reload bash to start using the 'llm' builtin:"
echo "  exec bash"
echo
