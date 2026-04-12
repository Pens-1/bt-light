#!/bin/bash
set -e

MCP_PORT=8766

echo "[bt-light] cloudflaredトンネル起動..."
cloudflared tunnel --url http://localhost:${MCP_PORT} > /tmp/cf.log 2>&1 &

# URL取得を待つ
for i in $(seq 1 20); do
    URL=$(grep -oE 'https://[a-z0-9-]+\.trycloudflare\.com' /tmp/cf.log 2>/dev/null | head -1)
    if [ -n "$URL" ]; then break; fi
    sleep 1
done

if [ -z "$URL" ]; then
    echo "[ERROR] cloudflaredのURL取得失敗"
    cat /tmp/cf.log
    exit 1
fi

echo "[bt-light] トンネルURL: $URL"
echo "[bt-light] MCPサーバー起動..."
echo ""
echo "======================================"
echo " claude.ai に登録するURL:"
echo " ${URL}/mcp"
echo " client_id: ${MCP_API_KEY}"
echo "======================================"

export MCP_BASE_URL="$URL"
exec python server.py --sse
