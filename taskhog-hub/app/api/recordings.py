from fastapi import APIRouter

router = APIRouter(prefix="/v1", tags=["recordings"])


@router.post("/recordings", status_code=501)
def create_recording() -> dict[str, str]:
    """Stub — implementado em M2."""
    return {"detail": "Not implemented — milestone M2"}
