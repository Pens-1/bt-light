#!/bin/bash
set -e

MCP_BASE_URL="${MCP_BASE_URL:-https://mcp-my-room.fullweak.com}"

echo "[bt-light] MCPサーバー起動..."
echo ""
echo "======================================"
echo " claude.ai に登録するURL:"
echo " ${MCP_BASE_URL}/mcp"
echo "======================================"

exec python server.py --sse
