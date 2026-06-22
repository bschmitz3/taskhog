from fastapi import APIRouter

router = APIRouter(prefix="/v1", tags=["admin"])


@router.post("/projects/refresh", status_code=501)
def refresh_projects() -> dict[str, str]:
    """Stub — implementado em M4."""
    return {"detail": "Not implemented — milestone M4"}
