import asyncio
import logging

import httpx
from fastapi import APIRouter, Depends, HTTPException, Request

from app.deps import verify_device_token
from app.models.schemas import CacheRefreshResponse, iso_now
from app.services import todoist_cache

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/v1", tags=["admin"])


@router.post("/projects/refresh", response_model=CacheRefreshResponse)
async def refresh_projects(
    request: Request,
    _token: str = Depends(verify_device_token),
) -> CacheRefreshResponse:
    """Força recarga do cache de projetos/labels (Spec 02 §5.5, §10.1)."""
    config = request.app.state.config
    engine = request.app.state.engine

    if not config.todoist_token:
        raise HTTPException(status_code=503, detail="Todoist token não configurado")

    try:
        counts = await asyncio.to_thread(
            todoist_cache.refresh_project_label_cache, engine, config
        )
    except httpx.HTTPError as exc:
        logger.exception("Falha ao atualizar cache Todoist")
        raise HTTPException(
            status_code=502, detail=f"Todoist API error: {exc}"
        ) from exc
    except Exception as exc:
        logger.exception("Falha ao atualizar cache Todoist")
        raise HTTPException(status_code=500, detail=str(exc)) from exc

    return CacheRefreshResponse(
        projects=counts["projects"],
        labels=counts["labels"],
        refreshed_at=iso_now(),
    )
