from __future__ import annotations

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from app.config import HubConfig

_bearer = HTTPBearer(auto_error=False)


def verify_device_token(
    credentials: HTTPAuthorizationCredentials | None = Depends(_bearer),
    config: HubConfig = Depends(),  # noqa: B008 — replaced at runtime via app.state
) -> str:
    if credentials is None or credentials.scheme.lower() != "bearer":
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing or invalid Authorization header",
        )

    token = credentials.credentials
    allowed: list[str] = getattr(verify_device_token, "_tokens", [])
    if token not in allowed:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Invalid device token",
        )
    return token


def bind_tokens(tokens: list[str]) -> None:
    verify_device_token._tokens = tokens  # type: ignore[attr-defined]
