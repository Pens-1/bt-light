"""bt-light MCP サーバー。

ESP32 HTTP API を Claude から操作するためのツールを提供する。

使い方:
  stdio モード（Claude Code ローカル）:
    python server.py

  HTTP モード（claude.ai リモート公開）:
    BT_LIGHT_HOST=192.168.0.43 MCP_API_KEY=secret MCP_BASE_URL=https://xxx.trycloudflare.com python server.py --sse
"""
from __future__ import annotations

import logging
import os
import sys
from pathlib import Path

import httpx
from mcp.server.fastmcp import FastMCP
from mcp.server.transport_security import TransportSecuritySettings

_log_dir = Path(__file__).parent / "logs"
_log_dir.mkdir(exist_ok=True)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[
        logging.StreamHandler(sys.stderr),
        logging.FileHandler(_log_dir / "mcp.log", encoding="utf-8"),
    ],
)
logger = logging.getLogger(__name__)

ESP32_HOST = os.environ.get("BT_LIGHT_HOST", "192.168.0.43")
BASE_URL = f"http://{ESP32_HOST}"
HTTP_PORT = int(os.environ.get("MCP_PORT", "8766"))

# MCP_BASE_URLからホスト名を取得してTransportSecuritySettingsに渡す
_mcp_base_url = os.environ.get("MCP_BASE_URL", f"http://localhost:{HTTP_PORT}")
_mcp_host = _mcp_base_url.replace("https://", "").replace("http://", "").split("/")[0]

mcp = FastMCP(
    "bt-light",
    transport_security=TransportSecuritySettings(
        allowed_hosts=[_mcp_host, "localhost", "127.0.0.1"],
        allowed_origins=["https://claude.ai", "https://claude.com"],
    ),
)


async def _post(path: str) -> str:
    """ESP32にPOSTリクエストを送り、レスポンスを返す。"""
    url = f"{BASE_URL}{path}"
    try:
        async with httpx.AsyncClient(timeout=15.0) as client:
            resp = await client.post(url)
            resp.raise_for_status()
            logger.info("POST %s -> %s", url, resp.text)
            return resp.text
    except httpx.TimeoutException:
        logger.error("タイムアウト: %s", url)
        return f"エラー: タイムアウト ({url})"
    except httpx.HTTPError as e:
        logger.error("HTTPエラー: %s %s", url, e)
        return f"エラー: {e}"


# ── BLEライト ──────────────────────────────────────────────


@mcp.tool()
async def lamp_on() -> str:
    """天井のBLEライトを点灯する。"""
    return await _post("/on")


@mcp.tool()
async def lamp_off() -> str:
    """天井のBLEライトを消灯する。"""
    return await _post("/off")


@mcp.tool()
async def lamp_night() -> str:
    """天井のBLEライトをナイトライトモードにする（極小輝度）。"""
    return await _post("/night")


@mcp.tool()
async def lamp_pair() -> str:
    """天井のBLEライトとペアリングする。ライトの電源を入れ直した後に実行。"""
    return await _post("/pair")


@mcp.tool()
async def lamp_rgb(r: int, g: int, b: int) -> str:
    """天井のBLEライトをRGBカラーモードにする。

    Args:
        r: 赤 0-255
        g: 緑 0-255
        b: 青 0-255
    """
    r = max(0, min(255, r))
    g = max(0, min(255, g))
    b = max(0, min(255, b))
    return await _post(f"/rgb?r={r}&g={g}&b={b}")


@mcp.tool()
async def lamp_brightness(cold: int, warm: int) -> str:
    """天井のBLEライトの輝度を設定する（白色モード）。

    Args:
        cold: 昼白色 0-255
        warm: 電球色 0-255
    """
    cold = max(0, min(255, cold))
    warm = max(0, min(255, warm))
    return await _post(f"/brightness?cold={cold}&warm={warm}")


# ── リレー（テープライト・モニターライト） ──────────────────


@mcp.tool()
async def tape_on() -> str:
    """デスクのテープライトを点灯する。"""
    return await _post("/tape/on")


@mcp.tool()
async def tape_off() -> str:
    """デスクのテープライトを消灯する。"""
    return await _post("/tape/off")


@mcp.tool()
async def monitor_on() -> str:
    """モニターライトを点灯する。"""
    return await _post("/monitor/on")


@mcp.tool()
async def monitor_off() -> str:
    """モニターライトを消灯する。"""
    return await _post("/monitor/off")


# ── 昇降デスク ────────────────────────────────────────────


@mcp.tool()
async def desk_button(button: int) -> str:
    """昇降デスクのメモリーボタンを押す。

    Args:
        button: ボタン番号 (1=一番低い座り高さ, 2=一番高い立ち高さ)
    """
    if button not in (1, 2):
        return "エラー: button は 1 か 2 を指定してください"
    return await _post(f"/desk/{button}")


if __name__ == "__main__":
    if "--sse" in sys.argv:
        import anyio
        import base64
        import hashlib
        import json as _json
        import secrets as _secrets
        from starlette.middleware.base import BaseHTTPMiddleware
        from starlette.requests import Request
        from starlette.responses import JSONResponse, RedirectResponse
        from starlette.routing import Route
        import uvicorn

        _api_key = os.environ.get("MCP_API_KEY", "")
        _BASE_URL = os.environ.get("MCP_BASE_URL", f"http://localhost:{HTTP_PORT}")
        _TOKENS_FILE = Path(__file__).parent / "session" / "oauth_tokens.json"
        _TOKENS_FILE.parent.mkdir(exist_ok=True)

        _auth_codes: dict = {}

        def _load_tokens() -> set:
            if _TOKENS_FILE.exists():
                try:
                    return set(_json.loads(_TOKENS_FILE.read_text()))
                except Exception:
                    pass
            return set()

        def _save_tokens(tokens: set) -> None:
            try:
                _TOKENS_FILE.write_text(_json.dumps(list(tokens)))
            except Exception:
                pass

        _access_tokens: set = _load_tokens()
        if _api_key:
            _access_tokens.add(_api_key)
            _save_tokens(_access_tokens)

        async def _well_known_oauth_server(request: Request):
            return JSONResponse({
                "issuer": _BASE_URL,
                "authorization_endpoint": f"{_BASE_URL}/authorize",
                "token_endpoint": f"{_BASE_URL}/token",
                "registration_endpoint": f"{_BASE_URL}/register",
                "response_types_supported": ["code"],
                "grant_types_supported": ["authorization_code"],
                "code_challenge_methods_supported": ["S256"],
            })

        async def _well_known_protected_resource(request: Request):
            return JSONResponse({
                "resource": f"{_BASE_URL}/mcp",
                "authorization_servers": [_BASE_URL],
                "bearer_methods_supported": ["header"],
            })

        async def _register(request: Request):
            body = await request.json()
            client_id = body.get("client_id") or _secrets.token_urlsafe(16)
            return JSONResponse({
                "client_id": client_id,
                "client_name": body.get("client_name", "Claude"),
                "redirect_uris": body.get("redirect_uris", []),
            }, status_code=201)

        async def _authorize(request: Request):
            params = dict(request.query_params)
            redirect_uri = params.get("redirect_uri", "")
            state = params.get("state", "")
            code_challenge = params.get("code_challenge", "")
            client_id = params.get("client_id", "")

            if _api_key and not _secrets.compare_digest(client_id, _api_key):
                return JSONResponse({"error": "unauthorized_client"}, status_code=401)

            code = _secrets.token_urlsafe(32)
            _auth_codes[code] = code_challenge
            sep = "&" if "?" in redirect_uri else "?"
            return RedirectResponse(
                url=f"{redirect_uri}{sep}code={code}&state={state}",
                status_code=302,
            )

        async def _token(request: Request):
            form = await request.form()
            code = form.get("code", "")
            code_verifier = form.get("code_verifier", "")

            if code not in _auth_codes:
                return JSONResponse({"error": "invalid_grant"}, status_code=400)

            code_challenge = _auth_codes.pop(code)
            digest = hashlib.sha256(code_verifier.encode()).digest()
            expected = base64.urlsafe_b64encode(digest).rstrip(b"=").decode()
            if not _secrets.compare_digest(expected, code_challenge):
                return JSONResponse({"error": "invalid_grant"}, status_code=400)

            access_token = _secrets.token_urlsafe(32)
            _access_tokens.add(access_token)
            _save_tokens(_access_tokens)
            return JSONResponse({
                "access_token": access_token,
                "token_type": "bearer",
                "expires_in": 31536000,
            })

        class BearerAuthMiddleware(BaseHTTPMiddleware):
            async def dispatch(self, request, call_next):
                path = request.url.path
                if path.startswith("/.well-known") or path in ("/authorize", "/token", "/register"):
                    return await call_next(request)
                if not _api_key and not _access_tokens:
                    return await call_next(request)
                auth = request.headers.get("Authorization", "")
                if not auth.startswith("Bearer "):
                    return JSONResponse(
                        {"detail": "Unauthorized"},
                        status_code=401,
                        headers={"WWW-Authenticate": "Bearer"},
                    )
                token = auth[len("Bearer "):]
                if token not in _access_tokens:
                    return JSONResponse(
                        {"detail": "Invalid token"},
                        status_code=401,
                        headers={"WWW-Authenticate": "Bearer"},
                    )
                return await call_next(request)

        logger.info("SSEモードで起動: port=%d, base_url=%s", HTTP_PORT, _BASE_URL)

        mcp_app = mcp.streamable_http_app()
        mcp_app.routes.insert(0, Route("/.well-known/oauth-authorization-server", _well_known_oauth_server))
        mcp_app.routes.insert(1, Route("/.well-known/oauth-protected-resource", _well_known_protected_resource))
        mcp_app.routes.insert(2, Route("/.well-known/oauth-protected-resource/mcp", _well_known_protected_resource))
        mcp_app.routes.insert(3, Route("/authorize", _authorize))
        mcp_app.routes.insert(4, Route("/token", _token, methods=["POST"]))
        mcp_app.routes.insert(5, Route("/register", _register, methods=["POST"]))
        mcp_app.add_middleware(BearerAuthMiddleware)

        config = uvicorn.Config(
            mcp_app,
            host="0.0.0.0",
            port=HTTP_PORT,
            log_level="info",
            http="h11",
        )
        server = uvicorn.Server(config)
        anyio.run(server.serve)
    else:
        mcp.run()
