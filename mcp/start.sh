#!/bin/bash
# bt-light MCP サーバー起動スクリプト
# 使い方: ./start.sh

set -e
cd "$(dirname "$0")"

MCP_PORT=8766
MCP_API_KEY="${MCP_API_KEY:-8qk-y8QkhR6--JAVkUwJVbrKotz1Cqx-K84Xr5g6L5w}"
BT_LIGHT_HOST="${BT_LIGHT_HOST:-192.168.0.43}"

# 既存プロセス停止
echo "[start] 既存プロセス停止..."
pkill -f "bt-light/mcp/server.py" 2>/dev/null || true
pkill -f "cloudflared.*8766" 2>/dev/null || true
fuser -k ${MCP_PORT}/tcp 2>/dev/null || true
sleep 1

# cloudflared起動してURLを取得
echo "[start] cloudflaredトンネル起動..."
cloudflared tunnel --url http://localhost:${MCP_PORT} > /tmp/cloudflared-bt.log 2>&1 &
CF_PID=$!

# URL取得を待つ
for i in $(seq 1 15); do
    URL=$(grep -oE 'https://[a-z0-9-]+\.trycloudflare\.com' /tmp/cloudflared-bt.log 2>/dev/null | head -1)
    if [ -n "$URL" ]; then break; fi
    sleep 1
done

if [ -z "$URL" ]; then
    echo "[ERROR] cloudflaredのURL取得に失敗"
    cat /tmp/cloudflared-bt.log
    exit 1
fi

echo "[start] トンネルURL: $URL"

# MCPサーバー起動
echo "[start] MCPサーバー起動..."
MCP_API_KEY="$MCP_API_KEY" \
MCP_PORT="$MCP_PORT" \
MCP_BASE_URL="$URL" \
BT_LIGHT_HOST="$BT_LIGHT_HOST" \
nohup python3 "$(dirname "$0")/server.py" --sse > /tmp/mcp-bt.log 2>&1 &
SRV_PID=$!

sleep 3

# 確認
if curl -sf "${URL}/.well-known/oauth-authorization-server" > /dev/null; then
    echo ""
    echo "======================================"
    echo " bt-light MCP 起動完了！"
    echo "======================================"
    echo " URL: ${URL}/mcp"
    echo " client_id: ${MCP_API_KEY}"
    echo " (claude.ai の Integrations に登録)"
    echo "======================================"
else
    echo "[ERROR] サーバー応答なし"
    cat /tmp/mcp-bt.log
    exit 1
fi
